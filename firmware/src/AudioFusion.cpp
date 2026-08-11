#include "AudioFusion.h"

#include <string.h>

namespace {

static const int32_t kQ15One = 32768;
static const int32_t kHealthyMinWeightQ15 = 3277;
static const int32_t kHealthyMaxWeightQ15 = kQ15One - kHealthyMinWeightQ15;
static const uint32_t kHighPassDropNumerator = 20588742u;

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static int16_t saturate_i16(int64_t value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

static uint32_t isqrt_u64(uint64_t value)
{
    uint64_t bit = (uint64_t)1 << 62;
    uint64_t result = 0;
    while (bit > value) bit >>= 2;
    while (bit != 0u) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

static uint8_t consecutive_blocks(bool condition, uint8_t previous)
{
    if (!condition) return 0u;
    return previous == 255u ? 255u : (uint8_t)(previous + 1u);
}

}  // namespace

void AudioFusion::reset(uint32_t sample_rate)
{
    memset(&a_, 0, sizeof(a_));
    memset(&b_, 0, sizeof(b_));
    memset(history_a_, 0, sizeof(history_a_));
    memset(history_b_, 0, sizeof(history_b_));
    history_write_ = 0u;
    history_count_ = 0u;
    const uint32_t alpha_drop = sample_rate ? (kHighPassDropNumerator / sample_rate) : 1284u;
    hp_alpha_q15_ = alpha_drop < 32767u ? kQ15One - (int32_t)alpha_drop : 0u;
    weight_a_q15_ = kQ15One / 2;
    memset(&stats_, 0, sizeof(stats_));
    stats_.weight_a_q15 = weight_a_q15_;
    stats_.weight_b_q15 = kQ15One - weight_a_q15_;
}

int16_t AudioFusion::condition(ChannelState* state, int16_t sample) const
{
    const int32_t input = sample;
    const int64_t filtered = (int64_t)hp_alpha_q15_ *
                             (state->hp_y + input - state->hp_x);
    state->hp_x = input;
    state->hp_y = (int32_t)(filtered >> 15);
    if (state->hp_y > 32767) state->hp_y = 32767;
    if (state->hp_y < -32768) state->hp_y = -32768;
    return (int16_t)state->hp_y;
}

void AudioFusion::update_correlation()
{
    const uint32_t window = history_count_ < 128u ? history_count_ : 128u;
    if (window < 16u) {
        stats_.best_lag = 0;
        stats_.correlation_q15 = 0;
        stats_.lag_alignment_active = false;
        return;
    }

    const uint32_t first = (history_write_ + kHistorySamples - window) % kHistorySamples;
    int32_t best_correlation = 0;
    int32_t best_lag = 0;
    int32_t best_magnitude = -1;

    for (int32_t lag = -8; lag <= 8; lag++) {
        const uint32_t skip = (uint32_t)(lag < 0 ? -lag : lag);
        const uint32_t usable = window - skip;
        int64_t cross = 0;
        uint64_t energy_a = 0;
        uint64_t energy_b = 0;
        for (uint32_t i = 0; i < usable; i++) {
            const uint32_t a_offset = i + (lag < 0 ? skip : 0u);
            const uint32_t b_offset = i + (lag > 0 ? skip : 0u);
            const int32_t a_value = history_a_[(first + a_offset) % kHistorySamples];
            const int32_t b_value = history_b_[(first + b_offset) % kHistorySamples];
            cross += (int64_t)a_value * b_value;
            energy_a += (uint64_t)((int64_t)a_value * a_value);
            energy_b += (uint64_t)((int64_t)b_value * b_value);
        }
        const uint32_t root_a = isqrt_u64(energy_a);
        const uint32_t root_b = isqrt_u64(energy_b);
        const uint64_t denominator = (uint64_t)root_a * root_b;
        if (denominator == 0u) continue;
        int32_t correlation = (int32_t)((cross * 32767) / (int64_t)denominator);
        if (correlation > 32767) correlation = 32767;
        if (correlation < -32767) correlation = -32767;
        const int32_t magnitude = abs_i32(correlation);
        if (magnitude > best_magnitude) {
            best_magnitude = magnitude;
            best_correlation = correlation;
            best_lag = lag;
        }
    }

    stats_.best_lag = best_lag;
    stats_.correlation_q15 = best_correlation;
    // Keep lag diagnostic-only until alignment uses a continuous delay line.
    stats_.lag_alignment_active = false;
}

void AudioFusion::process(const int16_t* a, const int16_t* b, int16_t* mono,
                          uint32_t count)
{
    if (a == 0 || b == 0 || mono == 0 || count == 0u) return;
    if (count > 128u) count = 128u;

    int16_t conditioned_a[128];
    int16_t conditioned_b[128];
    uint64_t energy_a = 0;
    uint64_t energy_b = 0;
    uint32_t rough_a = 0;
    uint32_t rough_b = 0;
    uint32_t clipped_a = 0;
    uint32_t clipped_b = 0;
    uint32_t impulse_a = 0;
    uint32_t impulse_b = 0;
    int16_t min_a = a[0], max_a = a[0], min_b = b[0], max_b = b[0];

    for (uint32_t i = 0; i < count; i++) {
        if (a[i] < min_a) min_a = a[i];
        if (a[i] > max_a) max_a = a[i];
        if (b[i] < min_b) min_b = b[i];
        if (b[i] > max_b) max_b = b[i];
        if (abs_i32(a[i]) >= 32000) clipped_a++;
        if (abs_i32(b[i]) >= 32000) clipped_b++;

        conditioned_a[i] = condition(&a_, a[i]);
        conditioned_b[i] = condition(&b_, b[i]);
        const int32_t delta_a = abs_i32(conditioned_a[i] - a_.previous_conditioned);
        const int32_t delta_b = abs_i32(conditioned_b[i] - b_.previous_conditioned);
        a_.previous_conditioned = conditioned_a[i];
        b_.previous_conditioned = conditioned_b[i];
        rough_a += (uint32_t)delta_a;
        rough_b += (uint32_t)delta_b;
        if (delta_a > 14000) impulse_a++;
        if (delta_b > 14000) impulse_b++;
        energy_a += (uint64_t)((int64_t)conditioned_a[i] * conditioned_a[i]);
        energy_b += (uint64_t)((int64_t)conditioned_b[i] * conditioned_b[i]);
        history_a_[history_write_] = conditioned_a[i];
        history_b_[history_write_] = conditioned_b[i];
        history_write_ = (history_write_ + 1u) % kHistorySamples;
        if (history_count_ < kHistorySamples) history_count_++;
    }

    const uint32_t block_rough_a = rough_a / count;
    const uint32_t block_rough_b = rough_b / count;
    a_.noise_estimate = (a_.noise_estimate * 7u + block_rough_a) / 8u;
    b_.noise_estimate = (b_.noise_estimate * 7u + block_rough_b) / 8u;
    const int32_t rms_a = (int32_t)isqrt_u64(energy_a / count);
    const int32_t rms_b = (int32_t)isqrt_u64(energy_b / count);

    a_.clipped_blocks = consecutive_blocks(clipped_a > count / 16u, a_.clipped_blocks);
    b_.clipped_blocks = consecutive_blocks(clipped_b > count / 16u, b_.clipped_blocks);
    a_.stuck_blocks = consecutive_blocks((int32_t)max_a - min_a < 16, a_.stuck_blocks);
    b_.stuck_blocks = consecutive_blocks((int32_t)max_b - min_b < 16, b_.stuck_blocks);
    a_.silent_blocks = consecutive_blocks(rms_a < 32, a_.silent_blocks);
    b_.silent_blocks = consecutive_blocks(rms_b < 32, b_.silent_blocks);

    uint32_t faults = 0u;
    if (a_.clipped_blocks >= 3u || a_.stuck_blocks >= 12u || a_.silent_blocks >= 16u) {
        faults |= AUDIO_FUSION_FAULT_A;
    }
    if (b_.clipped_blocks >= 3u || b_.stuck_blocks >= 12u || b_.silent_blocks >= 16u) {
        faults |= AUDIO_FUSION_FAULT_B;
    }

    const uint32_t score_a = a_.noise_estimate + block_rough_a + impulse_a * 2048u +
                             clipped_a * 1024u;
    const uint32_t score_b = b_.noise_estimate + block_rough_b + impulse_b * 2048u +
                             clipped_b * 1024u;
    int32_t target_weight_a = kQ15One / 2;
    bool returning_to_neutral = true;
    if ((faults & AUDIO_FUSION_FAULT_A) != 0u && (faults & AUDIO_FUSION_FAULT_B) == 0u) {
        target_weight_a = 0;
        returning_to_neutral = false;
    } else if ((faults & AUDIO_FUSION_FAULT_B) != 0u &&
               (faults & AUDIO_FUSION_FAULT_A) == 0u) {
        target_weight_a = kQ15One;
        returning_to_neutral = false;
    } else if (score_a * 100u < score_b * 70u) {
        target_weight_a = kHealthyMaxWeightQ15;
        returning_to_neutral = false;
    } else if (score_b * 100u < score_a * 70u) {
        target_weight_a = kHealthyMinWeightQ15;
        returning_to_neutral = false;
    }

    const int32_t difference = target_weight_a - weight_a_q15_;
    const int32_t step_limit = returning_to_neutral ? kMaxWeightStepQ15 / 2 :
                                                      kMaxWeightStepQ15;
    if (difference > step_limit) weight_a_q15_ += step_limit;
    else if (difference < -step_limit) weight_a_q15_ -= step_limit;
    else weight_a_q15_ = target_weight_a;

    update_correlation();
    const int32_t weight_b_q15 = kQ15One - weight_a_q15_;
    for (uint32_t i = 0; i < count; i++) {
        const int64_t mixed = (int64_t)conditioned_a[i] * weight_a_q15_ +
                              (int64_t)conditioned_b[i] * weight_b_q15;
        mono[i] = saturate_i16((mixed + 16384) >> 15);
    }

    stats_.rms_a = rms_a;
    stats_.rms_b = rms_b;
    stats_.roughness_a = (int32_t)a_.noise_estimate;
    stats_.roughness_b = (int32_t)b_.noise_estimate;
    stats_.weight_a_q15 = weight_a_q15_;
    stats_.weight_b_q15 = weight_b_q15;
    stats_.fault_flags = faults;
    stats_.speech_present = rms_a > 500 || rms_b > 500;
}

AudioFusionStats AudioFusion::stats() const
{
    return stats_;
}
