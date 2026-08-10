#pragma once

#include <stdint.h>

typedef struct {
    uint32_t frames;
    uint32_t noise_model_frames;
    uint32_t latency_samples;
    int32_t average_gain_q15;
    int32_t minimum_gain_q15;
    bool noise_model_ready;
    bool bypass;
    bool speech_present;
} NoiseReductionStats;

class NoiseReduction {
public:
    void reset(uint32_t sample_rate);
    void set_bypass(bool bypass);
    void process(const int16_t* in, int16_t* out, uint32_t count, bool speech_present);
    NoiseReductionStats stats() const;

private:
    static const uint32_t kWindowSamples = 256u;
    static const uint32_t kBlockSamples = 128u;
    static const uint32_t kBins = kWindowSamples / 2u + 1u;

    float input_history_[kWindowSamples];
    float overlap_[kBlockSamples];
    float noise_power_[kBins];
    float gain_history_[kBins];
    float fft_real_[kWindowSamples];
    float fft_imag_[kWindowSamples];
    uint32_t noise_model_frames_;
    bool bypass_;
    NoiseReductionStats stats_;

    void process_frame(bool speech_present);
};
