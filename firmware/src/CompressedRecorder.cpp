#include "CompressedRecorder.h"

#include <Arduino.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "Config.h"
#include "DeviceTime.h"
#include "Fs.h"
#include "TransferBuffer.h"

namespace {

size_t align_up_8(size_t value)
{
    return (value + 7u) & ~(size_t)7u;
}

void duration_suffix(char* output, size_t length, uint32_t seconds)
{
    const uint32_t hours = seconds / 3600u;
    const uint32_t minutes = (seconds / 60u) % 60u;
    const uint32_t remaining = seconds % 60u;
    if (hours > 0u) {
        snprintf(output, length, "_%luh%02lum%02lus", (unsigned long)hours,
                 (unsigned long)minutes, (unsigned long)remaining);
    } else {
        snprintf(output, length, "_%lum%02lus", (unsigned long)minutes,
                 (unsigned long)remaining);
    }
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
      name_(),
      sequence_(1u),
      synced_page_count_(0u),
      file_open_(false),
      file_created_(false),
      pdm_initialized_(false),
      pdm_started_(false),
      pipeline_started_(false),
      timestamp_name_(false),
      active_(false),
      callback_result_(COMPRESSED_RECORDER_OK),
      stats_()
{
    stats_.bitrate = kOpusBitrate;
    stats_.sample_rate = kOpusSampleRate;
    stats_.last_result = COMPRESSED_RECORDER_OK;
}

CompressedRecorderResult CompressedRecorder::start(RingBuf* pdm_sink,
                                                    uint64_t minimum_free_bytes)
{
    if (active_) return COMPRESSED_RECORDER_ERR_BUSY;

    memset(&stats_, 0, sizeof(stats_));
    stats_.bitrate = kOpusBitrate;
    stats_.sample_rate = kOpusSampleRate;
    name_[0] = '\0';
    sequence_ = 1u;
    synced_page_count_ = 0u;
    file_open_ = false;
    file_created_ = false;
    pdm_initialized_ = false;
    pdm_started_ = false;
    pipeline_started_ = false;
    timestamp_name_ = false;
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
        pdm_stop();
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
        file_open_ = false;
        if (close_result != FR_OK && result == COMPRESSED_RECORDER_OK) {
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
        char stem[16];
        dt_format_stem(stem, sizeof(stem));
        for (uint8_t collision = 0u; collision < 10u; ++collision) {
            if (collision == 0u) {
                snprintf(name_, sizeof(name_), "0:/REC-%s.%s", stem, REC_EXT_STR);
            } else {
                snprintf(name_, sizeof(name_), "0:/REC-%s_%u.%s", stem,
                         (unsigned)collision, REC_EXT_STR);
            }
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
    if (file_open_) {
        f_close(&file_);
        file_open_ = false;
    }
    if (file_created_ && name_[0] != '\0') f_unlink(name_);
    file_created_ = false;
    active_ = false;
    set_result(result);
    return result;
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
    char suffix[24];
    char renamed[sizeof(name_)];
    duration_suffix(suffix, sizeof(suffix), seconds);
    const char* const dot = strrchr(name_, '.');
    if (dot == nullptr) return false;
    snprintf(renamed, sizeof(renamed), "%.*s%s.%s", (int)(dot - name_), name_,
             suffix, REC_EXT_STR);
    if (f_rename(name_, renamed) != FR_OK) return false;
    strncpy(name_, renamed, sizeof(name_) - 1u);
    name_[sizeof(name_) - 1u] = '\0';
    return true;
}
