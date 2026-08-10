#pragma once

#include <stdint.h>

enum {
    AUDIO_FUSION_FAULT_A = 1u << 0,
    AUDIO_FUSION_FAULT_B = 1u << 1,
};

typedef struct {
    int32_t rms_a;
    int32_t rms_b;
    int32_t roughness_a;
    int32_t roughness_b;
    int32_t weight_a_q15;
    int32_t weight_b_q15;
    int32_t best_lag;
    int32_t correlation_q15;
    uint32_t fault_flags;
    bool speech_present;
    bool lag_alignment_active;
} AudioFusionStats;

class AudioFusion {
public:
    static const int32_t kMaxWeightStepQ15 = 2048;

    void reset(uint32_t sample_rate);
    void process(const int16_t* a, const int16_t* b, int16_t* mono, uint32_t count);
    AudioFusionStats stats() const;

private:
    struct ChannelState {
        int32_t hp_x;
        int32_t hp_y;
        int32_t previous_conditioned;
        uint32_t noise_estimate;
        uint8_t clipped_blocks;
        uint8_t stuck_blocks;
        uint8_t silent_blocks;
    };

    static const uint32_t kHistorySamples = 256u;

    ChannelState a_;
    ChannelState b_;
    int16_t history_a_[kHistorySamples];
    int16_t history_b_[kHistorySamples];
    uint32_t history_write_;
    uint32_t history_count_;
    uint32_t hp_alpha_q15_;
    int32_t weight_a_q15_;
    AudioFusionStats stats_;

    int16_t condition(ChannelState* state, int16_t sample) const;
    void update_correlation();
};
