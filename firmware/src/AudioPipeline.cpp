#include "AudioPipeline.h"

#include <string.h>

AudioFusion AudioPipeline::fusion_;
NoiseReduction AudioPipeline::noise_reduction_;
SpeechLeveler AudioPipeline::speech_leveler_;
int16_t AudioPipeline::input_a_[AudioPipeline::kDspBlockSamples];
int16_t AudioPipeline::input_b_[AudioPipeline::kDspBlockSamples];
int16_t AudioPipeline::fused_[AudioPipeline::kDspBlockSamples];
int16_t AudioPipeline::reduced_[AudioPipeline::kDspBlockSamples];
int16_t AudioPipeline::leveled_[AudioPipeline::kDspBlockSamples];
int16_t AudioPipeline::codec_frame_[AudioPipeline::kCodecFrameSamples];
AudioPipeline* AudioPipeline::active_instance_ = 0;

AudioPipeline::AudioPipeline()
    : sink_(0),
      sink_context_(0),
      input_count_(0u),
      delayed_valid_samples_(0u),
      codec_frame_count_(0u),
      delayed_speech_present_(false),
      latency_suppressed_(false),
      initialized_(false),
      finished_(false),
      stats_()
{
}

AudioPipeline::~AudioPipeline()
{
    if (active_instance_ == this) active_instance_ = 0;
}

bool AudioPipeline::reset(uint32_t sample_rate, AudioFrameSink sink, void* sink_context)
{
    if (sink == 0 || (active_instance_ != 0 && active_instance_ != this)) {
        if (active_instance_ == this) active_instance_ = 0;
        sink_ = 0;
        sink_context_ = 0;
        input_count_ = 0u;
        delayed_valid_samples_ = 0u;
        codec_frame_count_ = 0u;
        delayed_speech_present_ = false;
        latency_suppressed_ = false;
        initialized_ = false;
        finished_ = false;
        memset(&stats_, 0, sizeof(stats_));
        stats_.failed = true;
        return false;
    }

    active_instance_ = this;
    sink_ = sink;
    sink_context_ = sink_context;
    input_count_ = 0u;
    delayed_valid_samples_ = 0u;
    codec_frame_count_ = 0u;
    delayed_speech_present_ = false;
    latency_suppressed_ = false;
    initialized_ = true;
    finished_ = false;
    memset(&stats_, 0, sizeof(stats_));
    memset(input_a_, 0, sizeof(input_a_));
    memset(input_b_, 0, sizeof(input_b_));
    memset(codec_frame_, 0, sizeof(codec_frame_));
    fusion_.reset(sample_rate);
    noise_reduction_.reset(sample_rate);
    speech_leveler_.reset(sample_rate);

    return true;
}

bool AudioPipeline::push(const int16_t* a, const int16_t* b, uint32_t count)
{
    if (!initialized_ || finished_ || stats_.failed) return false;
    if (count == 0u) return true;
    if (a == 0 || b == 0) {
        stats_.failed = true;
        return false;
    }

    for (uint32_t offset = 0u; offset < count;) {
        const uint32_t available = kDspBlockSamples - input_count_;
        uint32_t copied = count - offset;
        if (copied > available) copied = available;
        memcpy(input_a_ + input_count_, a + offset, copied * sizeof(input_a_[0]));
        memcpy(input_b_ + input_count_, b + offset, copied * sizeof(input_b_[0]));
        input_count_ += copied;
        offset += copied;
        stats_.captured_samples += copied;

        if (input_count_ == kDspBlockSamples) {
            if (!process_dsp_block(kDspBlockSamples)) return false;
            input_count_ = 0u;
        }
    }
    return true;
}

bool AudioPipeline::finish()
{
    if (!initialized_ || stats_.failed) return false;
    if (finished_) return true;
    if (stats_.captured_samples == 0u) {
        finished_ = true;
        active_instance_ = 0;
        return true;
    }

    if (input_count_ != 0u) {
        const uint32_t valid_samples = input_count_;
        memset(input_a_ + input_count_, 0,
               (kDspBlockSamples - input_count_) * sizeof(input_a_[0]));
        memset(input_b_ + input_count_, 0,
               (kDspBlockSamples - input_count_) * sizeof(input_b_[0]));
        input_count_ = 0u;
        if (!process_dsp_block(valid_samples)) return false;
    }

    memset(input_a_, 0, sizeof(input_a_));
    memset(input_b_, 0, sizeof(input_b_));
    if (!process_dsp_block(0u)) return false;

    if (codec_frame_count_ != 0u) {
        const uint16_t valid_samples = (uint16_t)codec_frame_count_;
        memset(codec_frame_ + codec_frame_count_, 0,
               (kCodecFrameSamples - codec_frame_count_) * sizeof(codec_frame_[0]));
        if (!emit_codec_frame(valid_samples)) return false;
        codec_frame_count_ = 0u;
    }

    finished_ = true;
    active_instance_ = 0;
    return true;
}

AudioPipelineStats AudioPipeline::stats() const
{
    return stats_;
}

bool AudioPipeline::process_dsp_block(uint32_t valid_samples)
{
    fusion_.process(input_a_, input_b_, fused_, kDspBlockSamples);
    const AudioFusionStats fusion_stats = fusion_.stats();
    noise_reduction_.process(fused_, reduced_, kDspBlockSamples, fusion_stats.speech_present);
    speech_leveler_.process(reduced_, leveled_, kDspBlockSamples, delayed_speech_present_);
    stats_.dsp_blocks++;

    if (!latency_suppressed_) {
        latency_suppressed_ = true;
        stats_.suppressed_latency_samples = kDspBlockSamples;
    } else if (!append_codec_samples(leveled_, delayed_valid_samples_)) {
        return false;
    }
    delayed_valid_samples_ = valid_samples;
    delayed_speech_present_ = fusion_stats.speech_present;
    return true;
}

bool AudioPipeline::append_codec_samples(const int16_t* samples, uint32_t valid_samples)
{
    for (uint32_t i = 0u; i < valid_samples; ++i) {
        codec_frame_[codec_frame_count_++] = samples[i];
        if (codec_frame_count_ == kCodecFrameSamples) {
            if (!emit_codec_frame(kCodecFrameSamples)) return false;
            codec_frame_count_ = 0u;
        }
    }
    return true;
}

bool AudioPipeline::emit_codec_frame(uint16_t valid_samples)
{
    if (!sink_(sink_context_, codec_frame_, valid_samples)) {
        stats_.failed = true;
        return false;
    }
    stats_.emitted_frames++;
    stats_.valid_samples += valid_samples;
    return true;
}
