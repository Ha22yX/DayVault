#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AudioPipeline.h"
#include "DayVaultOpusEncoder.h"
#include "OggOpusWriter.h"
#include "OpusFinalization.h"
#include "PdmCapture.h"
#include "RingBuf.h"
#include "ff.h"

enum CompressedRecorderResult {
    COMPRESSED_RECORDER_OK = 0,
    COMPRESSED_RECORDER_CHECKPOINT_NOT_READY = 1,
    COMPRESSED_RECORDER_ERR_BUSY = 100,
    COMPRESSED_RECORDER_ERR_MOUNT,
    COMPRESSED_RECORDER_ERR_SPACE,
    COMPRESSED_RECORDER_ERR_OPEN,
    COMPRESSED_RECORDER_ERR_WORKSPACE,
    COMPRESSED_RECORDER_ERR_ENCODER,
    COMPRESSED_RECORDER_ERR_HEADER,
    COMPRESSED_RECORDER_ERR_HEADER_SYNC,
    COMPRESSED_RECORDER_ERR_PIPELINE,
    COMPRESSED_RECORDER_ERR_PDM_START,
    COMPRESSED_RECORDER_ERR_STARTUP_DISCARD,
    COMPRESSED_RECORDER_ERR_ENCODE,
    COMPRESSED_RECORDER_ERR_OGG,
    COMPRESSED_RECORDER_ERR_WRITE,
    COMPRESSED_RECORDER_ERR_SYNC,
    COMPRESSED_RECORDER_ERR_CLOSE,
    COMPRESSED_RECORDER_ERR_RENAME,
    COMPRESSED_RECORDER_ERR_CLEANUP_CLOSE,
    COMPRESSED_RECORDER_ERR_CLEANUP_UNLINK,
    COMPRESSED_RECORDER_ERR_CLEANUP_CLOSE_UNLINK,
};

struct CompressedRecorderStats {
    uint32_t bitrate;
    uint32_t sample_rate;
    uint32_t max_encode_us;
    size_t workspace_used;
    uint32_t sync_count;
    CompressedRecorderResult primary_result;
    CompressedRecorderResult cleanup_result;
    CompressedRecorderResult last_result;
    DayVaultOpusStats encoder;
    OggOpusStats ogg;
    AudioPipelineStats pipeline;
    PdmCaptureStats pdm;
};

class CompressedRecorder {
public:
    CompressedRecorder();

    CompressedRecorderResult start(RingBuf* pdm_sink, uint64_t minimum_free_bytes);
    CompressedRecorderResult poll();
    CompressedRecorderResult checkpoint();
    CompressedRecorderResult stop();

    bool active() const;
    const char* name() const;
    uint32_t sequence() const;
    CompressedRecorderStats stats() const;
    static const char* result_name(CompressedRecorderResult result);

private:
    static const size_t kOggPageBytes = 8192u;
    static const uint32_t kStartupDiscardSamples = 32u;
    static const uint32_t kStartupDiscardTimeoutMs = 250u;

    static bool write_sink(void* context, const uint8_t* bytes, size_t length);
    static bool frame_sink(void* context, const int16_t* pcm, uint16_t valid_samples);

    bool open_recording();
    bool write_page(const uint8_t* bytes, size_t length);
    bool encode_frame(const int16_t* pcm, uint16_t valid_samples);
    bool drain_encoder_lookahead();
    CompressedRecorderResult fail_start(CompressedRecorderResult result);
    CompressedRecorderResult cleanup_partial_file();
    void set_result(CompressedRecorderResult result);
    void refresh_stats();
    bool rename_with_duration();

    FIL file_;
    DayVaultOpusEncoder encoder_;
    OggOpusWriter ogg_;
    AudioPipeline pipeline_;
    int16_t input_a_[128];
    int16_t input_b_[128];
    uint8_t packet_[kOpusMaxPacketBytes];
    int16_t zero_frame_[kOpusFrameSamples];
    char name_[64];
    char timestamp_stem_[16];
    uint32_t sequence_;
    uint64_t synced_page_count_;
    bool file_open_;
    bool file_created_;
    bool pdm_initialized_;
    bool pdm_started_;
    bool pipeline_started_;
    bool has_encoded_frame_;
    uint16_t last_valid_samples_;
    bool timestamp_name_;
    bool cleanup_pending_;
    bool active_;
    CompressedRecorderResult callback_result_;
    CompressedRecorderStats stats_;
};
