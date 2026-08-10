#include "CompressedRecorder.h"

#include <Arduino.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "Config.h"
#include "DeviceTime.h"
#include "Fs.h"
#include "RecordingName.h"
#include "TransferBuffer.h"

namespace {

size_t align_up_8(size_t value)
{
    return (value + 7u) & ~(size_t)7u;
}

}  // namespace

CompressedRecorder::CompressedRecorder()
    : file_(),
      encoder_(),
      ogg_(),
      pipeline_(),
      input_a_(),
      input_b_(),
      packet_(),
      zero_frame_(),
      name_(),
      timestamp_stem_(),
      sequence_(1u),
      synced_page_count_(0u),
      file_open_(false),
      file_created_(false),
      pdm_initialized_(false),
      pdm_started_(false),
      pipeline_started_(false),
      has_encoded_frame_(false),
      last_valid_samples_(0u),
      timestamp_name_(false),
      cleanup_pending_(false),
      active_(false),
      callback_result_(COMPRESSED_RECORDER_OK),
      stats_()
{
    stats_.bitrate = kOpusBitrate;
    stats_.sample_rate = kOpusSampleRate;
    stats_.primary_result = COMPRESSED_RECORDER_OK;
    stats_.cleanup_result = COMPRESSED_RECORDER_OK;
    stats_.last_result = COMPRESSED_RECORDER_OK;
}

CompressedRecorderResult CompressedRecorder::start(RingBuf* pdm_sink,
                                                    uint64_t minimum_free_bytes)
{
    if (active_) return COMPRESSED_RECORDER_ERR_BUSY;

    if (cleanup_pending_) {
        const CompressedRecorderResult cleanup = cleanup_partial_file();
        stats_.cleanup_result = cleanup;
        if (cleanup != COMPRESSED_RECORDER_OK) {
            set_result(cleanup);
            return cleanup;
        }
    }
    if (file_open_) {
        if (f_close(&file_) != FR_OK) {
            set_result(COMPRESSED_RECORDER_ERR_CLOSE);
            return COMPRESSED_RECORDER_ERR_CLOSE;
        }
        file_open_ = false;
    }

    memset(&stats_, 0, sizeof(stats_));
    stats_.bitrate = kOpusBitrate;
    stats_.sample_rate = kOpusSampleRate;
    name_[0] = '\0';
    timestamp_stem_[0] = '\0';
    sequence_ = 1u;
    synced_page_count_ = 0u;
    file_open_ = false;
    file_created_ = false;
    pdm_initialized_ = false;
    pdm_started_ = false;
    pipeline_started_ = false;
    has_encoded_frame_ = false;
    last_valid_samples_ = 0u;
    timestamp_name_ = false;
    cleanup_pending_ = false;
    callback_result_ = COMPRESSED_RECORDER_OK;

    if (pdm_sink == nullptr) return fail_start(COMPRESSED_RECORDER_ERR_PIPELINE);
    if (!fs_mount()) return fail_start(COMPRESSED_RECORDER_ERR_MOUNT);
    fs_make_space(minimum_free_bytes, nullptr);
    if (fs_free_bytes() < minimum_free_bytes) {
        return fail_start(COMPRESSED_RECORDER_ERR_SPACE);
    }
    if (!open_recording()) return fail_start(COMPRESSED_RECORDER_ERR_OPEN);

    uint8_t* const workspace = transfer_workspace();
    const size_t workspace_bytes = transfer_workspace_size();
    if (workspace == nullptr || workspace_bytes <= kOggPageBytes) {
        return fail_start(COMPRESSED_RECORDER_ERR_WORKSPACE);
    }
    const size_t encoder_limit = workspace_bytes - kOggPageBytes;
    if (!encoder_.begin(workspace, encoder_limit)) {
        return fail_start(COMPRESSED_RECORDER_ERR_ENCODER);
    }
    const size_t page_offset = align_up_8(encoder_.workspace_used());
    if (page_offset > encoder_limit || page_offset + kOggPageBytes > workspace_bytes) {
        return fail_start(COMPRESSED_RECORDER_ERR_WORKSPACE);
    }

    uint32_t serial = dt_get_unix() ^ millis() ^ (uint32_t)(uintptr_t)this;
    if (serial == 0u) serial = 1u;
    OggOpusSink sink = {write_sink, this};
    if (!ogg_.begin(sink, workspace + page_offset, kOggPageBytes,
                    serial, encoder_.pre_skip_48k())) {
        return fail_start(callback_result_ == COMPRESSED_RECORDER_OK
            ? COMPRESSED_RECORDER_ERR_HEADER : callback_result_);
    }
    if (f_sync(&file_) != FR_OK) {
        return fail_start(COMPRESSED_RECORDER_ERR_HEADER_SYNC);
    }
    synced_page_count_ = ogg_.stats().page_count;
    stats_.sync_count = 1u;

    if (!pipeline_.reset(kOpusSampleRate, frame_sink, this)) {
        return fail_start(COMPRESSED_RECORDER_ERR_PIPELINE);
    }
    pipeline_started_ = true;
    pdm_init(pdm_sink);
    pdm_initialized_ = true;
    pdm_start();
    pdm_started_ = true;
    if (pdm_start_result() != HAL_OK) {
        return fail_start(COMPRESSED_RECORDER_ERR_PDM_START);
    }

    uint32_t discarded = 0u;
    const uint32_t discard_started = millis();
    while (discarded < kStartupDiscardSamples) {
        const int wanted = (int)(kStartupDiscardSamples - discarded);
        const int read = pdm_dma_read_dual(input_a_, input_b_, wanted);
        if (read > 0) {
            discarded += (uint32_t)read;
        } else if ((millis() - discard_started) >= kStartupDiscardTimeoutMs) {
            return fail_start(COMPRESSED_RECORDER_ERR_STARTUP_DISCARD);
        } else {
            delay(1);
        }
    }

    active_ = true;
    set_result(COMPRESSED_RECORDER_OK);
    refresh_stats();
    return COMPRESSED_RECORDER_OK;
}

CompressedRecorderResult CompressedRecorder::poll()
{
    if (!active_) return COMPRESSED_RECORDER_ERR_BUSY;
    const int read = pdm_dma_read_dual(input_a_, input_b_, 128);
    if (read > 0 && !pipeline_.push(input_a_, input_b_, (uint32_t)read)) {
        set_result(callback_result_ == COMPRESSED_RECORDER_OK
            ? COMPRESSED_RECORDER_ERR_PIPELINE : callback_result_);
        refresh_stats();
        return stats_.last_result;
    }
    refresh_stats();
    return COMPRESSED_RECORDER_OK;
}

CompressedRecorderResult CompressedRecorder::checkpoint()
{
    if (!active_) return COMPRESSED_RECORDER_ERR_BUSY;
    const uint64_t page_count = ogg_.stats().page_count;
    if (page_count <= synced_page_count_) return COMPRESSED_RECORDER_CHECKPOINT_NOT_READY;
    if (f_sync(&file_) != FR_OK) {
        set_result(COMPRESSED_RECORDER_ERR_SYNC);
        return COMPRESSED_RECORDER_ERR_SYNC;
    }
    synced_page_count_ = page_count;
    ++stats_.sync_count;
    refresh_stats();
    return COMPRESSED_RECORDER_OK;
}

CompressedRecorderResult CompressedRecorder::stop()
{
    if (!active_) return stats_.last_result;
    CompressedRecorderResult result = stats_.last_result;

    if (pdm_started_) {
        pdm_stop_and_freeze();
        pdm_started_ = false;
    }
    if (result == COMPRESSED_RECORDER_OK) {
        for (;;) {
            const int read = pdm_dma_read_dual(input_a_, input_b_, 128);
            if (read <= 0) break;
            if (!pipeline_.push(input_a_, input_b_, (uint32_t)read)) {
                result = callback_result_ == COMPRESSED_RECORDER_OK
                    ? COMPRESSED_RECORDER_ERR_PIPELINE : callback_result_;
                break;
            }
        }
    }
    if (pipeline_started_) {
        if (result == COMPRESSED_RECORDER_OK && !pipeline_.finish()) {
            result = callback_result_ == COMPRESSED_RECORDER_OK
                ? COMPRESSED_RECORDER_ERR_PIPELINE : callback_result_;
        } else if (result != COMPRESSED_RECORDER_OK) {
            pipeline_.finish();
        }
        pipeline_started_ = false;
    }
    if (result == COMPRESSED_RECORDER_OK && !drain_encoder_lookahead()) {
        result = callback_result_ == COMPRESSED_RECORDER_OK
            ? COMPRESSED_RECORDER_ERR_ENCODE : callback_result_;
    }
    if (result == COMPRESSED_RECORDER_OK && !ogg_.finish()) {
        result = callback_result_ == COMPRESSED_RECORDER_OK
            ? COMPRESSED_RECORDER_ERR_OGG : callback_result_;
    }
    if (file_open_) {
        const FRESULT sync_result = f_sync(&file_);
        if (sync_result != FR_OK && result == COMPRESSED_RECORDER_OK) {
            result = COMPRESSED_RECORDER_ERR_SYNC;
        } else if (sync_result == FR_OK) {
            ++stats_.sync_count;
        }
    }

    refresh_stats();
    if (file_open_) {
        const FRESULT close_result = f_close(&file_);
        if (close_result == FR_OK) {
            file_open_ = false;
        } else if (result == COMPRESSED_RECORDER_OK) {
            result = COMPRESSED_RECORDER_ERR_CLOSE;
        }
    }
    encoder_.end();
    active_ = false;
    set_result(result);

    if (result == COMPRESSED_RECORDER_OK && timestamp_name_ && !rename_with_duration()) {
        set_result(COMPRESSED_RECORDER_ERR_RENAME);
    }
    return stats_.last_result;
}

bool CompressedRecorder::active() const
{
    return active_;
}

const char* CompressedRecorder::name() const
{
    return name_;
}

uint32_t CompressedRecorder::sequence() const
{
    return sequence_;
}

CompressedRecorderStats CompressedRecorder::stats() const
{
    return stats_;
}

const char* CompressedRecorder::result_name(CompressedRecorderResult result)
{
    switch (result) {
    case COMPRESSED_RECORDER_OK: return "ok";
    case COMPRESSED_RECORDER_CHECKPOINT_NOT_READY: return "checkpoint-pending";
    case COMPRESSED_RECORDER_ERR_BUSY: return "busy";
    case COMPRESSED_RECORDER_ERR_MOUNT: return "mount";
    case COMPRESSED_RECORDER_ERR_SPACE: return "space";
    case COMPRESSED_RECORDER_ERR_OPEN: return "open";
    case COMPRESSED_RECORDER_ERR_WORKSPACE: return "workspace";
    case COMPRESSED_RECORDER_ERR_ENCODER: return "encoder";
    case COMPRESSED_RECORDER_ERR_HEADER: return "header";
    case COMPRESSED_RECORDER_ERR_HEADER_SYNC: return "header-sync";
    case COMPRESSED_RECORDER_ERR_PIPELINE: return "pipeline";
    case COMPRESSED_RECORDER_ERR_PDM_START: return "pdm-start";
    case COMPRESSED_RECORDER_ERR_STARTUP_DISCARD: return "startup-discard";
    case COMPRESSED_RECORDER_ERR_ENCODE: return "encode";
    case COMPRESSED_RECORDER_ERR_OGG: return "ogg";
    case COMPRESSED_RECORDER_ERR_WRITE: return "write";
    case COMPRESSED_RECORDER_ERR_SYNC: return "sync";
    case COMPRESSED_RECORDER_ERR_CLOSE: return "close";
    case COMPRESSED_RECORDER_ERR_RENAME: return "rename";
    case COMPRESSED_RECORDER_ERR_CLEANUP_CLOSE: return "cleanup-close";
    case COMPRESSED_RECORDER_ERR_CLEANUP_UNLINK: return "cleanup-unlink";
    case COMPRESSED_RECORDER_ERR_CLEANUP_CLOSE_UNLINK: return "cleanup-close-unlink";
    }
    return "unknown";
}

bool CompressedRecorder::write_sink(void* context, const uint8_t* bytes, size_t length)
{
    return static_cast<CompressedRecorder*>(context)->write_page(bytes, length);
}

bool CompressedRecorder::frame_sink(void* context, const int16_t* pcm,
                                    uint16_t valid_samples)
{
    return static_cast<CompressedRecorder*>(context)->encode_frame(pcm, valid_samples);
}

bool CompressedRecorder::open_recording()
{
    timestamp_name_ = false;
    if (dt_time_is_set()) {
        dt_format_stem(timestamp_stem_, sizeof(timestamp_stem_));
        for (uint16_t collision = 0u; collision < 100u; ++collision) {
            if (!recording_format_timestamp_path(
                    name_, sizeof(name_), timestamp_stem_, collision, false, 0u,
                    REC_EXT_STR)) return false;
            const FRESULT open_result = f_open(&file_, name_, FA_CREATE_NEW | FA_WRITE);
            if (open_result == FR_OK) {
                file_open_ = true;
                file_created_ = true;
                timestamp_name_ = true;
                sequence_ = 0u;
                return true;
            }
            if (open_result != FR_EXIST) return false;
        }
    }

    sequence_ = fs_next_sequence();
    snprintf(name_, sizeof(name_), "0:/%s%03lu.%s", REC_DIR_STR,
             (unsigned long)sequence_, REC_EXT_STR);
    if (f_open(&file_, name_, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    file_open_ = true;
    file_created_ = true;
    return true;
}

bool CompressedRecorder::write_page(const uint8_t* bytes, size_t length)
{
    if (!file_open_ || bytes == nullptr || length > UINT_MAX) {
        callback_result_ = COMPRESSED_RECORDER_ERR_WRITE;
        return false;
    }
    UINT written = 0u;
    if (f_write(&file_, bytes, (UINT)length, &written) != FR_OK || written != length) {
        callback_result_ = COMPRESSED_RECORDER_ERR_WRITE;
        return false;
    }
    return true;
}

bool CompressedRecorder::encode_frame(const int16_t* pcm, uint16_t valid_samples)
{
    const uint32_t started = micros();
    const int length = encoder_.encode(pcm, packet_);
    const uint32_t elapsed = micros() - started;
    if (elapsed > stats_.max_encode_us) stats_.max_encode_us = elapsed;
    if (length <= 0) {
        callback_result_ = COMPRESSED_RECORDER_ERR_ENCODE;
        return false;
    }
    if (!ogg_.add_packet(packet_, (uint16_t)length, valid_samples)) {
        if (callback_result_ == COMPRESSED_RECORDER_OK) {
            callback_result_ = COMPRESSED_RECORDER_ERR_OGG;
        }
        return false;
    }
    has_encoded_frame_ = true;
    last_valid_samples_ = valid_samples;
    return true;
}

bool CompressedRecorder::drain_encoder_lookahead()
{
    const OpusFinalizationPlan plan = opus_finalization_plan(
        encoder_.lookahead_samples_16k(), has_encoded_frame_, last_valid_samples_);
    for (uint16_t frame = 0u; frame < plan.additional_zero_frames; ++frame) {
        if (!encode_frame(zero_frame_, 0u)) return false;
    }
    return true;
}

CompressedRecorderResult CompressedRecorder::fail_start(CompressedRecorderResult result)
{
    if (pdm_started_) {
        pdm_stop();
        pdm_started_ = false;
    }
    if (pipeline_started_) {
        pipeline_.finish();
        pipeline_started_ = false;
    }
    refresh_stats();
    encoder_.end();
    active_ = false;
    stats_.primary_result = result;
    cleanup_pending_ = file_open_ || file_created_;
    const CompressedRecorderResult cleanup = cleanup_partial_file();
    stats_.cleanup_result = cleanup;
    const CompressedRecorderResult reported = cleanup == COMPRESSED_RECORDER_OK
        ? result : cleanup;
    set_result(reported);
    return reported;
}

CompressedRecorderResult CompressedRecorder::cleanup_partial_file()
{
    bool close_failed = false;
    bool unlink_failed = false;
    if (file_open_) {
        if (f_close(&file_) == FR_OK) {
            file_open_ = false;
        } else {
            close_failed = true;
        }
    }
    if (file_created_ && name_[0] != '\0') {
        if (f_unlink(name_) == FR_OK) {
            file_created_ = false;
        } else {
            unlink_failed = true;
        }
    }
    cleanup_pending_ = file_open_ || file_created_;
    if (close_failed && unlink_failed) {
        return COMPRESSED_RECORDER_ERR_CLEANUP_CLOSE_UNLINK;
    }
    if (close_failed) return COMPRESSED_RECORDER_ERR_CLEANUP_CLOSE;
    if (unlink_failed) return COMPRESSED_RECORDER_ERR_CLEANUP_UNLINK;
    return COMPRESSED_RECORDER_OK;
}

void CompressedRecorder::set_result(CompressedRecorderResult result)
{
    stats_.last_result = result;
}

void CompressedRecorder::refresh_stats()
{
    stats_.encoder = encoder_.stats();
    stats_.ogg = ogg_.stats();
    stats_.pipeline = pipeline_.stats();
    if (pdm_initialized_) stats_.pdm = pdm_capture_stats();
    stats_.workspace_used = encoder_.workspace_used();
}

bool CompressedRecorder::rename_with_duration()
{
    const uint32_t seconds = (uint32_t)(stats_.ogg.valid_input_samples / kOpusSampleRate);
    char renamed[sizeof(name_)];
    FILINFO info;
    for (uint16_t collision = 0u; collision < 100u; ++collision) {
        if (!recording_format_timestamp_path(
                renamed, sizeof(renamed), timestamp_stem_, collision, true, seconds,
                REC_EXT_STR)) return false;
        const FRESULT stat_result = f_stat(renamed, &info);
        if (stat_result == FR_OK) continue;
        if (stat_result != FR_NO_FILE) return false;
        if (f_rename(name_, renamed) != FR_OK) return false;
        strncpy(name_, renamed, sizeof(name_) - 1u);
        name_[sizeof(name_) - 1u] = '\0';
        return true;
    }
    return false;
}
