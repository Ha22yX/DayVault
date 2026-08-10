#include "NoiseReduction.h"

#include <math.h>
#include <string.h>

namespace {

static const float kGainFloor = 0.25f;
static const float kNoiseUpdate = 0.10f;
static const float kTemporalGain = 0.75f;
static const uint32_t kNoiseFramesForReady = 8u;
static bool g_tables_ready = false;
static float g_hann[256];
static float g_twiddle_real[129];
static float g_twiddle_imag[129];

static int16_t saturate_i16(int32_t value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

static float clamp_gain(float gain)
{
    if (!isfinite(gain) || gain < kGainFloor) return kGainFloor;
    if (gain > 1.0f) return 1.0f;
    return gain;
}

static void initialize_tables(void)
{
    if (g_tables_ready) return;
    const float two_pi = 6.28318530717958647692f;
    for (uint32_t length = 2u; length <= 256u; length <<= 1u) {
        const float angle = -two_pi / length;
        g_twiddle_real[length >> 1u] = cosf(angle);
        g_twiddle_imag[length >> 1u] = sinf(angle);
    }
    for (uint32_t i = 0; i < 256u; i++) {
        g_hann[i] = 0.5f - 0.5f * cosf(two_pi * i / 256.0f);
    }
    g_tables_ready = true;
}

static void fft(float* real, float* imag)
{
    for (uint32_t i = 1u, j = 0u; i < 256u; i++) {
        uint32_t bit = 128u;
        for (; (j & bit) != 0u; bit >>= 1u) j ^= bit;
        j ^= bit;
        if (i < j) {
            const float temp_real = real[i];
            const float temp_imag = imag[i];
            real[i] = real[j];
            imag[i] = imag[j];
            real[j] = temp_real;
            imag[j] = temp_imag;
        }
    }
    for (uint32_t length = 2u; length <= 256u; length <<= 1u) {
        const uint32_t half = length >> 1u;
        const float wl_real = g_twiddle_real[half];
        const float wl_imag = g_twiddle_imag[half];
        for (uint32_t offset = 0u; offset < 256u; offset += length) {
            float wr = 1.0f;
            float wi = 0.0f;
            for (uint32_t index = 0u; index < half; index++) {
                const uint32_t lower = offset + index;
                const uint32_t upper = lower + half;
                const float value_real = real[upper] * wr - imag[upper] * wi;
                const float value_imag = real[upper] * wi + imag[upper] * wr;
                const float base_real = real[lower];
                const float base_imag = imag[lower];
                real[lower] = base_real + value_real;
                imag[lower] = base_imag + value_imag;
                real[upper] = base_real - value_real;
                imag[upper] = base_imag - value_imag;
                const float next_wr = wr * wl_real - wi * wl_imag;
                wi = wr * wl_imag + wi * wl_real;
                wr = next_wr;
            }
        }
    }
}

static void inverse_fft(float* real, float* imag)
{
    for (uint32_t i = 0u; i < 256u; i++) imag[i] = -imag[i];
    fft(real, imag);
    for (uint32_t i = 0u; i < 256u; i++) {
        real[i] /= 256.0f;
        imag[i] = -imag[i] / 256.0f;
    }
}

}  // namespace

void NoiseReduction::reset(uint32_t sample_rate)
{
    (void)sample_rate;
    initialize_tables();
    memset(input_history_, 0, sizeof(input_history_));
    memset(overlap_, 0, sizeof(overlap_));
    memset(noise_power_, 0, sizeof(noise_power_));
    memset(gain_history_, 0, sizeof(gain_history_));
    memset(fft_real_, 0, sizeof(fft_real_));
    memset(fft_imag_, 0, sizeof(fft_imag_));
    for (uint32_t bin = 0u; bin < kBins; bin++) gain_history_[bin] = 1.0f;
    noise_model_frames_ = 0u;
    bypass_ = false;
    memset(&stats_, 0, sizeof(stats_));
    stats_.latency_samples = kBlockSamples;
    stats_.average_gain_q15 = 32768;
    stats_.minimum_gain_q15 = 32768;
}

void NoiseReduction::set_bypass(bool bypass)
{
    if (bypass_ != bypass) {
        memset(input_history_, 0, sizeof(input_history_));
        memset(overlap_, 0, sizeof(overlap_));
    }
    bypass_ = bypass;
    stats_.bypass = bypass;
}

void NoiseReduction::process(const int16_t* in, int16_t* out, uint32_t count,
                             bool speech_present)
{
    if (in == 0 || out == 0 || count == 0u) return;
    if (count > kBlockSamples) count = kBlockSamples;
    stats_.bypass = bypass_;
    stats_.speech_present = speech_present;
    if (bypass_ || count != kBlockSamples) {
        for (uint32_t i = 0u; i < count; i++) out[i] = in[i];
        return;
    }

    memmove(input_history_, input_history_ + kBlockSamples,
            kBlockSamples * sizeof(input_history_[0]));
    for (uint32_t i = 0u; i < kBlockSamples; i++) {
        input_history_[kBlockSamples + i] = (float)in[i] / 32768.0f;
    }
    process_frame(speech_present);

    for (uint32_t i = 0u; i < kBlockSamples; i++) {
        const float normalization = g_hann[i] * g_hann[i] +
                                    g_hann[kBlockSamples + i] * g_hann[kBlockSamples + i];
        const float sample = normalization > 0.0f ?
                                 (overlap_[i] + fft_real_[i] * g_hann[i]) / normalization :
                                 0.0f;
        out[i] = saturate_i16((int32_t)(sample * 32768.0f));
        overlap_[i] = fft_real_[kBlockSamples + i] * g_hann[kBlockSamples + i];
    }
    stats_.frames++;
}

void NoiseReduction::process_frame(bool speech_present)
{
    for (uint32_t i = 0u; i < kWindowSamples; i++) {
        fft_real_[i] = input_history_[i] * g_hann[i];
        fft_imag_[i] = 0.0f;
    }
    fft(fft_real_, fft_imag_);

    if (!speech_present) {
        for (uint32_t bin = 0u; bin < kBins; bin++) {
            const float power = fft_real_[bin] * fft_real_[bin] + fft_imag_[bin] * fft_imag_[bin];
            if (noise_model_frames_ == 0u) noise_power_[bin] = power;
            else noise_power_[bin] = (1.0f - kNoiseUpdate) * noise_power_[bin] +
                                     kNoiseUpdate * power;
        }
        noise_model_frames_++;
    }

    const bool model_ready = noise_model_frames_ >= kNoiseFramesForReady;
    float gain_sum = 0.0f;
    float min_gain = 1.0f;
    for (uint32_t bin = 0u; bin < kBins; bin++) {
        float gain = 1.0f;
        if (model_ready) {
            const float power = fft_real_[bin] * fft_real_[bin] + fft_imag_[bin] * fft_imag_[bin];
            const float raw_gain = power > 1.0e-12f ?
                                       clamp_gain((power - noise_power_[bin]) / power) :
                                       kGainFloor;
            const float previous = bin == 0u ? raw_gain : gain_history_[bin - 1u];
            const float adjacent = bin + 1u == kBins ? raw_gain :
                                   (previous + raw_gain + gain_history_[bin + 1u]) / 3.0f;
            gain = clamp_gain(kTemporalGain * gain_history_[bin] +
                              (1.0f - kTemporalGain) * adjacent);
        }
        gain_history_[bin] = gain;
        gain_sum += gain;
        if (gain < min_gain) min_gain = gain;
        fft_real_[bin] *= gain;
        fft_imag_[bin] *= gain;
        if (bin != 0u && bin + 1u != kBins) {
            const uint32_t mirror = kWindowSamples - bin;
            fft_real_[mirror] *= gain;
            fft_imag_[mirror] *= gain;
        }
    }
    inverse_fft(fft_real_, fft_imag_);

    stats_.noise_model_frames = noise_model_frames_;
    stats_.noise_model_ready = model_ready;
    stats_.average_gain_q15 = (int32_t)(gain_sum * 32768.0f / kBins + 0.5f);
    stats_.minimum_gain_q15 = (int32_t)(min_gain * 32768.0f + 0.5f);
}

NoiseReductionStats NoiseReduction::stats() const
{
    return stats_;
}
