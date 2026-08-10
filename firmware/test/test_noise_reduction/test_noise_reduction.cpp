#include <math.h>
#include <unity.h>

#include "NoiseReduction.h"

static const uint32_t kRate = 16000u;
static const uint32_t kFrames = 128u;
static int16_t input[kFrames];
static int16_t output[kFrames];
static int16_t reference[kFrames];
static int16_t previous_reference[kFrames];

void setUp(void) {}
void tearDown(void) {}

static uint32_t xorshift32(uint32_t* state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void make_noise(int16_t* samples, uint32_t* state, int16_t amplitude)
{
    for (uint32_t i = 0; i < kFrames; i++) {
        const int32_t value = (int32_t)(xorshift32(state) & 0xffffu) - 32768;
        samples[i] = (int16_t)((value * amplitude) / 32768);
    }
}

static void make_one_khz(int16_t* samples, uint32_t* phase, int16_t amplitude)
{
    for (uint32_t i = 0; i < kFrames; i++) {
        const uint32_t step = (*phase)++ & 15u;
        const int32_t triangle = step < 8u ? (int32_t)step : (int32_t)(15u - step);
        samples[i] = (int16_t)(((triangle * 2 - 7) * amplitude) / 7);
    }
}

static int16_t saturate_i16(int32_t value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

static int32_t rms(const int16_t* samples)
{
    uint64_t energy = 0u;
    for (uint32_t i = 0; i < kFrames; i++) {
        energy += (uint64_t)((int32_t)samples[i] * samples[i]);
    }
    return (int32_t)sqrt((double)energy / kFrames);
}

static double correlation(const int16_t* a, const int16_t* b)
{
    int64_t cross = 0;
    uint64_t energy_a = 0u;
    uint64_t energy_b = 0u;
    for (uint32_t i = 0; i < kFrames; i++) {
        cross += (int32_t)a[i] * (int32_t)b[i];
        energy_a += (uint64_t)((int32_t)a[i] * a[i]);
        energy_b += (uint64_t)((int32_t)b[i] * b[i]);
    }
    return cross / sqrt((double)energy_a * energy_b);
}

static void learn_noise(NoiseReduction* nr, uint32_t* noise_state, uint32_t blocks)
{
    for (uint32_t block = 0; block < blocks; block++) {
        make_noise(input, noise_state, 3000);
        nr->process(input, output, kFrames, false);
    }
}

void test_bypass_is_bit_identical(void)
{
    NoiseReduction nr;
    nr.reset(kRate);
    nr.set_bypass(true);
    uint32_t noise_state = 0x12345678u;
    make_noise(input, &noise_state, 15000);
    nr.process(input, output, kFrames, true);

    TEST_ASSERT_EQUAL_INT16_ARRAY(input, output, kFrames);
    TEST_ASSERT_TRUE(nr.stats().bypass);
}

void test_bypass_transition_discards_stale_overlap_before_reenabling(void)
{
    NoiseReduction nr;
    nr.reset(kRate);
    uint32_t phase = 0u;
    make_one_khz(input, &phase, 16000);
    nr.process(input, output, kFrames, false);
    make_one_khz(input, &phase, 16000);
    nr.process(input, output, kFrames, false);

    nr.set_bypass(true);
    uint32_t noise_state = 0x42424242u;
    make_noise(input, &noise_state, 5000);
    for (uint32_t i = 0; i < kFrames; i++) reference[i] = input[i];
    nr.process(input, output, kFrames, false);
    TEST_ASSERT_EQUAL_INT16_ARRAY(reference, output, kFrames);

    nr.set_bypass(false);
    for (uint32_t i = 0; i < kFrames; i++) input[i] = 0;
    nr.process(input, output, kFrames, false);
    for (uint32_t i = 0; i < kFrames; i++) TEST_ASSERT_TRUE(abs(output[i]) <= 1);
}

void test_speech_without_noise_model_is_not_muted(void)
{
    NoiseReduction nr;
    nr.reset(kRate);
    uint32_t phase = 0u;
    for (uint32_t block = 0; block < 3u; block++) {
        make_one_khz(input, &phase, 9000);
        nr.process(input, output, kFrames, true);
    }

    TEST_ASSERT_FALSE(nr.stats().noise_model_ready);
    TEST_ASSERT_TRUE(rms(output) > 3000);
}

void test_learned_noise_is_reduced_by_at_least_six_db(void)
{
    NoiseReduction nr;
    nr.reset(kRate);
    uint32_t noise_state = 0x99887766u;
    learn_noise(&nr, &noise_state, 32u);
    make_noise(input, &noise_state, 3000);
    nr.process(input, output, kFrames, false);
    make_noise(input, &noise_state, 3000);
    nr.process(input, output, kFrames, false);

    TEST_ASSERT_TRUE(nr.stats().noise_model_ready);
    TEST_ASSERT_TRUE(rms(output) * 2 <= rms(input));
}

void test_learned_noise_preserves_clean_one_khz_correlation_after_latency(void)
{
    NoiseReduction nr;
    nr.reset(kRate);
    uint32_t noise_state = 0xa1b2c3d4u;
    learn_noise(&nr, &noise_state, 32u);
    uint32_t phase = 0u;
    double last_correlation = 0.0;

    for (uint32_t block = 0; block < 6u; block++) {
        make_one_khz(reference, &phase, 10000);
        make_noise(input, &noise_state, 1000);
        for (uint32_t i = 0; i < kFrames; i++) {
            input[i] = saturate_i16((int32_t)input[i] + reference[i]);
        }
        nr.process(input, output, kFrames, true);
        if (block > 0u) last_correlation = correlation(previous_reference, output);
        for (uint32_t i = 0; i < kFrames; i++) previous_reference[i] = reference[i];
    }

    TEST_ASSERT_EQUAL_UINT32(128u, nr.stats().latency_samples);
    TEST_ASSERT_TRUE(last_correlation > 0.95);
}

void test_speech_freezes_the_noise_model(void)
{
    NoiseReduction nr;
    nr.reset(kRate);
    uint32_t noise_state = 0x10203040u;
    learn_noise(&nr, &noise_state, 16u);
    const uint32_t noise_frames = nr.stats().noise_model_frames;
    uint32_t phase = 0u;
    for (uint32_t block = 0; block < 8u; block++) {
        make_one_khz(input, &phase, 9000);
        nr.process(input, output, kFrames, true);
    }

    TEST_ASSERT_EQUAL_UINT32(noise_frames, nr.stats().noise_model_frames);
}

void test_smoothing_prevents_an_isolated_full_scale_output_spike(void)
{
    NoiseReduction nr;
    nr.reset(kRate);
    uint32_t noise_state = 0x55667788u;
    learn_noise(&nr, &noise_state, 32u);
    make_noise(input, &noise_state, 3000);
    input[64] = 32767;
    nr.process(input, output, kFrames, false);
    nr.process(input, output, kFrames, false);

    for (uint32_t i = 0; i < kFrames; i++) TEST_ASSERT_TRUE(abs(output[i]) < 32000);
}

void test_gain_never_drops_below_one_quarter(void)
{
    NoiseReduction nr;
    nr.reset(kRate);
    uint32_t noise_state = 0x31415926u;
    learn_noise(&nr, &noise_state, 32u);
    make_noise(input, &noise_state, 3000);
    nr.process(input, output, kFrames, false);

    TEST_ASSERT_TRUE(nr.stats().minimum_gain_q15 >= 8192);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bypass_is_bit_identical);
    RUN_TEST(test_bypass_transition_discards_stale_overlap_before_reenabling);
    RUN_TEST(test_speech_without_noise_model_is_not_muted);
    RUN_TEST(test_learned_noise_is_reduced_by_at_least_six_db);
    RUN_TEST(test_learned_noise_preserves_clean_one_khz_correlation_after_latency);
    RUN_TEST(test_speech_freezes_the_noise_model);
    RUN_TEST(test_smoothing_prevents_an_isolated_full_scale_output_spike);
    RUN_TEST(test_gain_never_drops_below_one_quarter);
    return UNITY_END();
}
