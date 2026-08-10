#pragma once

#include <stdint.h>

#include "AudioFusion.h"
#include "NoiseReduction.h"
#include "SpeechLeveler.h"

typedef bool (*AudioFrameSink)(void*, const int16_t*, uint16_t valid_samples);

typedef struct {
    uint64_t captured_samples;
    uint64_t valid_samples;
    uint32_t emitted_frames;
    uint32_t dsp_blocks;
    uint32_t suppressed_latency_samples;
    AudioFusionStats fusion;
    NoiseReductionStats noise_reduction;
    SpeechLevelerStats leveler;
    bool failed;
} AudioPipelineStats;

class AudioPipeline {
public:
    AudioPipeline();
    ~AudioPipeline();

    AudioPipeline(const AudioPipeline&) = delete;
    AudioPipeline& operator=(const AudioPipeline&) = delete;
    AudioPipeline(AudioPipeline&&) = delete;
    AudioPipeline& operator=(AudioPipeline&&) = delete;

    bool reset(uint32_t sample_rate, AudioFrameSink sink, void* sink_context);
    bool push(const int16_t* a, const int16_t* b, uint32_t count);
    bool finish();
    AudioPipelineStats stats() const;

private:
    static const uint32_t kDspBlockSamples = 128u;
    static const uint32_t kCodecFrameSamples = 320u;

    bool process_dsp_block(uint32_t valid_samples);
    bool append_codec_samples(const int16_t* samples, uint32_t valid_samples);
    bool emit_codec_frame(uint16_t valid_samples);
    bool fail();
    void release_active();

    static AudioFusion fusion_;
    static NoiseReduction noise_reduction_;
    static SpeechLeveler speech_leveler_;
    static int16_t input_a_[kDspBlockSamples];
    static int16_t input_b_[kDspBlockSamples];
    static int16_t fused_[kDspBlockSamples];
    static int16_t reduced_[kDspBlockSamples];
    static int16_t leveled_[kDspBlockSamples];
    static int16_t codec_frame_[kCodecFrameSamples];
    // Shared fixed storage is reserved by one reset pipeline at a time.
    static AudioPipeline* active_instance_;

    AudioFrameSink sink_;
    void* sink_context_;
    uint32_t input_count_;
    uint32_t delayed_valid_samples_;
    uint32_t codec_frame_count_;
    bool delayed_speech_present_;
    bool latency_suppressed_;
    bool initialized_;
    bool finished_;
    AudioPipelineStats stats_;
};
