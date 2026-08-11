#include "SpeechLeveler.h"

#include <string.h>

namespace {

static const uint32_t kUnityQ16 = 65536u;
static const uint32_t kMaxGainQ16 = 262144u;
static const uint32_t kMinGainQ16 = 16384u;
static const uint32_t kRmsTarget = 5000u;
static const int32_t kLimiterPeak = 32700;

static uint32_t isqrt_u64(uint64_t value)
{
    uint64_t bit = (uint64_t)1 << 62;
    uint64_t result = 0u;
    while (bit > value) bit >>= 2u;
    while (bit != 0u) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1u) + bit;
        } else {
            result >>= 1u;
        }
        bit >>= 2u;
    }
    return (uint32_t)result;
}

static uint32_t saturating_increment(uint32_t value)
{
    return value == 0xffffffffu ? value : value + 1u;
}

}  // namespace

void SpeechLeveler::reset(uint32_t sample_rate)
{
    gain_q16_ = kUnityQ16;
    speech_hangover_samples_ = 0u;
    speech_hangover_reload_samples_ =
        (uint32_t)(((uint64_t)sample_rate * kSpeechHangoverMs) / 1000u);
    bypass_ = false;
    memset(&stats_, 0, sizeof(stats_));
    stats_.gain_q16 = gain_q16_;
    stats_.applied_gain_q16 = gain_q16_;
}

void SpeechLeveler::set_bypass(bool bypass)
{
    bypass_ = bypass;
    stats_.bypass = bypass;
}

void SpeechLeveler::process(const int16_t* in, int16_t* out, uint32_t count,
                             bool speech_present)
{
    if (in == 0 || out == 0 || count == 0u) return;
    stats_.bypass = bypass_;
    stats_.speech_present = speech_present;
    if (bypass_) {
        for (uint32_t i = 0u; i < count; i++) out[i] = in[i];
        stats_.applied_gain_q16 = kUnityQ16;
        return;
    }

    uint64_t energy = 0u;
    for (uint32_t i = 0u; i < count; i++) {
        energy += (uint64_t)((int32_t)in[i] * in[i]);
    }
    const uint32_t block_rms = isqrt_u64(energy / count);
    bool speech_held = speech_present;
    if (speech_present) {
        speech_hangover_samples_ = speech_hangover_reload_samples_;
    } else if (speech_hangover_samples_ != 0u) {
        speech_held = true;
        speech_hangover_samples_ = count >= speech_hangover_samples_
            ? 0u : speech_hangover_samples_ - count;
    }
    if (speech_present && block_rms > 0u) {
        uint32_t target_gain = (uint32_t)(((uint64_t)kRmsTarget << 16u) / block_rms);
        if (target_gain > kMaxGainQ16) target_gain = kMaxGainQ16;
        if (target_gain < kMinGainQ16) target_gain = kMinGainQ16;
        if (target_gain > gain_q16_) {
            const uint32_t rise = target_gain - gain_q16_;
            gain_q16_ += rise > kQuietRiseStepQ16 ? kQuietRiseStepQ16 : rise;
        } else if (target_gain < gain_q16_) {
            const uint32_t fall = gain_q16_ - target_gain;
            gain_q16_ -= fall > kLoudFallStepQ16 ? kLoudFallStepQ16 : fall;
        }
    } else if (gain_q16_ > kUnityQ16) {
        const uint32_t release = gain_q16_ - kUnityQ16;
        gain_q16_ -= release > kReleaseStepQ16 ? kReleaseStepQ16 : release;
    } else if (gain_q16_ < kUnityQ16) {
        const uint32_t release = kUnityQ16 - gain_q16_;
        gain_q16_ += release > kReleaseStepQ16 ? kReleaseStepQ16 : release;
    }

    const uint32_t applied_gain = !speech_held && gain_q16_ > kUnityQ16 ?
                                      kUnityQ16 : gain_q16_;
    for (uint32_t i = 0u; i < count; i++) {
        const int64_t product = (int64_t)in[i] * applied_gain;
        const int64_t scaled = product >= 0 ? (product + 32768) >> 16u :
                                              -(((-product) + 32768) >> 16u);
        if (scaled > kLimiterPeak) {
            out[i] = (int16_t)kLimiterPeak;
            stats_.limiter_activations = saturating_increment(stats_.limiter_activations);
            stats_.prevented_clip_events = saturating_increment(stats_.prevented_clip_events);
        } else if (scaled < -kLimiterPeak) {
            out[i] = (int16_t)-kLimiterPeak;
            stats_.limiter_activations = saturating_increment(stats_.limiter_activations);
            stats_.prevented_clip_events = saturating_increment(stats_.prevented_clip_events);
        } else {
            out[i] = (int16_t)scaled;
        }
    }
    stats_.gain_q16 = gain_q16_;
    stats_.applied_gain_q16 = applied_gain;
}

SpeechLevelerStats SpeechLeveler::stats() const
{
    return stats_;
}
