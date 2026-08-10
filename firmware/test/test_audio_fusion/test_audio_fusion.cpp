#include <unity.h>

#include "AudioFusion.h"

static const uint32_t kRate = 16026u;
static const uint32_t kFrames = 128u;

static int16_t channel_a[kFrames];
static int16_t channel_b[kFrames];
static int16_t mono[kFrames];

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

static void make_tone(int16_t* output, uint32_t count, uint32_t* phase, int16_t amplitude)
{
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t step = (*phase)++ & 31u;
        const int32_t triangle = step < 16u ? (int32_t)step : (int32_t)(31u - step);
        output[i] = (int16_t)(((triangle * 2 - 15) * amplitude) / 15);
    }
}

static void make_noise(int16_t* output, uint32_t count, uint32_t* state, int16_t amplitude)
{
    for (uint32_t i = 0; i < count; i++) {
        const int32_t value = (int32_t)(xorshift32(state) & 0xffffu) - 32768;
        output[i] = (int16_t)((value * amplitude) / 32768);
    }
}

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static void process_blocks(AudioFusion* fusion, bool clean_a, uint32_t blocks)
{
    uint32_t tone_phase = 0u;
    uint32_t noise_state = 0x12345678u;
    for (uint32_t block = 0; block < blocks; block++) {
        make_tone(clean_a ? channel_a : channel_b, kFrames, &tone_phase, 9000);
        make_noise(clean_a ? channel_b : channel_a, kFrames, &noise_state, 12000);
        fusion->process(channel_a, channel_b, mono, kFrames);
    }
}

void test_equal_clean_channels_settle_near_balance_and_preserve_level(void)
{
    AudioFusion fusion;
    fusion.reset(kRate);
    uint32_t phase = 0u;
    for (uint32_t block = 0; block < 40u; block++) {
        make_tone(channel_a, kFrames, &phase, 9000);
        for (uint32_t i = 0; i < kFrames; i++) channel_b[i] = channel_a[i];
        fusion.process(channel_a, channel_b, mono, kFrames);
    }

    const AudioFusionStats stats = fusion.stats();
    TEST_ASSERT_INT_WITHIN(2048, 16384, stats.weight_a_q15);
    TEST_ASSERT_EQUAL_INT32(32768, stats.weight_a_q15 + stats.weight_b_q15);
    TEST_ASSERT_TRUE(abs_i32(mono[64]) > 3000);
}

void test_clean_a_receives_at_least_seventy_percent_after_settling(void)
{
    AudioFusion fusion;
    fusion.reset(kRate);
    process_blocks(&fusion, true, 40u);

    const AudioFusionStats stats = fusion.stats();
    TEST_ASSERT_TRUE(stats.weight_a_q15 >= 22938);
    TEST_ASSERT_TRUE(stats.weight_b_q15 >= 3277);
}

void test_clean_b_receives_at_least_seventy_percent_after_settling(void)
{
    AudioFusion fusion;
    fusion.reset(kRate);
    process_blocks(&fusion, false, 40u);

    const AudioFusionStats stats = fusion.stats();
    TEST_ASSERT_TRUE(stats.weight_b_q15 >= 22938);
    TEST_ASSERT_TRUE(stats.weight_a_q15 >= 3277);
}

void test_clipped_channel_falls_back_to_healthy_channel(void)
{
    AudioFusion fusion;
    fusion.reset(kRate);
    uint32_t phase = 0u;
    for (uint32_t block = 0; block < 40u; block++) {
        for (uint32_t i = 0; i < kFrames; i++) channel_a[i] = 32767;
        make_tone(channel_b, kFrames, &phase, 9000);
        fusion.process(channel_a, channel_b, mono, kFrames);
    }

    const AudioFusionStats stats = fusion.stats();
    TEST_ASSERT_TRUE((stats.fault_flags & AUDIO_FUSION_FAULT_A) != 0u);
    TEST_ASSERT_EQUAL_INT32(0, stats.weight_a_q15);
    TEST_ASSERT_EQUAL_INT32(32768, stats.weight_b_q15);
}

void test_persistent_fault_does_not_clear_when_block_counter_wraps(void)
{
    AudioFusion fusion;
    fusion.reset(kRate);
    uint32_t phase = 0u;
    for (uint32_t block = 0; block < 256u; block++) {
        for (uint32_t i = 0; i < kFrames; i++) {
            channel_a[i] = (i & 1u) == 0u ? 32767 : -32768;
        }
        make_tone(channel_b, kFrames, &phase, 9000);
        fusion.process(channel_a, channel_b, mono, kFrames);
    }

    const AudioFusionStats stats = fusion.stats();
    TEST_ASSERT_TRUE((stats.fault_flags & AUDIO_FUSION_FAULT_A) != 0u);
    TEST_ASSERT_EQUAL_INT32(0, stats.weight_a_q15);
}

void test_weight_change_is_bounded_per_block(void)
{
    AudioFusion fusion;
    fusion.reset(kRate);
    uint32_t tone_phase = 0u;
    uint32_t noise_state = 0x9e3779b9u;
    int32_t previous_weight = fusion.stats().weight_a_q15;
    for (uint32_t block = 0; block < 32u; block++) {
        make_tone(channel_a, kFrames, &tone_phase, 9000);
        make_noise(channel_b, kFrames, &noise_state, 14000);
        fusion.process(channel_a, channel_b, mono, kFrames);
        const int32_t current_weight = fusion.stats().weight_a_q15;
        TEST_ASSERT_TRUE(abs_i32(current_weight - previous_weight) <=
                         AudioFusion::kMaxWeightStepQ15);
        previous_weight = current_weight;
    }
}

void test_delayed_correlated_channels_report_lag_and_high_correlation(void)
{
    AudioFusion fusion;
    fusion.reset(kRate);
    uint32_t noise_state = 0x5a17c0deu;
    for (uint32_t i = 0; i < kFrames; i++) {
        channel_a[i] = (int16_t)(((int32_t)(xorshift32(&noise_state) & 0xffffu) - 32768) / 4);
        channel_b[i] = i >= 3u ? channel_a[i - 3u] : 0;
    }
    fusion.process(channel_a, channel_b, mono, kFrames);

    const AudioFusionStats stats = fusion.stats();
    TEST_ASSERT_INT_WITHIN(1, 3, stats.best_lag);
    TEST_ASSERT_TRUE(stats.correlation_q15 > 24000);
    TEST_ASSERT_TRUE(stats.lag_alignment_active);
}

void test_weak_correlation_does_not_enable_coherent_alignment(void)
{
    AudioFusion fusion;
    fusion.reset(kRate);
    uint32_t a_state = 0x12345678u;
    uint32_t b_state = 0x87654321u;
    make_noise(channel_a, kFrames, &a_state, 10000);
    make_noise(channel_b, kFrames, &b_state, 10000);
    fusion.process(channel_a, channel_b, mono, kFrames);

    const AudioFusionStats stats = fusion.stats();
    TEST_ASSERT_TRUE(abs_i32(stats.correlation_q15) < 24000);
    TEST_ASSERT_FALSE(stats.lag_alignment_active);
}

void test_opposing_and_adding_full_scale_inputs_saturate_safely(void)
{
    AudioFusion fusion;
    fusion.reset(kRate);
    for (uint32_t i = 0; i < kFrames; i++) {
        channel_a[i] = 32767;
        channel_b[i] = 32767;
    }
    fusion.process(channel_a, channel_b, mono, kFrames);
    for (uint32_t i = 0; i < kFrames; i++) {
        TEST_ASSERT_TRUE(mono[i] <= 32767 && mono[i] >= -32768);
    }

    for (uint32_t i = 0; i < kFrames; i++) channel_b[i] = -32768;
    fusion.process(channel_a, channel_b, mono, kFrames);
    for (uint32_t i = 0; i < kFrames; i++) {
        TEST_ASSERT_TRUE(mono[i] <= 32767 && mono[i] >= -32768);
    }
}

void test_reset_restores_neutral_deterministic_state(void)
{
    AudioFusion fusion;
    fusion.reset(kRate);
    process_blocks(&fusion, true, 16u);
    fusion.reset(kRate);

    const AudioFusionStats stats = fusion.stats();
    TEST_ASSERT_EQUAL_INT32(16384, stats.weight_a_q15);
    TEST_ASSERT_EQUAL_INT32(16384, stats.weight_b_q15);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.fault_flags);
    TEST_ASSERT_EQUAL_INT32(0, stats.correlation_q15);
    TEST_ASSERT_FALSE(stats.speech_present);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_equal_clean_channels_settle_near_balance_and_preserve_level);
    RUN_TEST(test_clean_a_receives_at_least_seventy_percent_after_settling);
    RUN_TEST(test_clean_b_receives_at_least_seventy_percent_after_settling);
    RUN_TEST(test_clipped_channel_falls_back_to_healthy_channel);
    RUN_TEST(test_persistent_fault_does_not_clear_when_block_counter_wraps);
    RUN_TEST(test_weight_change_is_bounded_per_block);
    RUN_TEST(test_delayed_correlated_channels_report_lag_and_high_correlation);
    RUN_TEST(test_weak_correlation_does_not_enable_coherent_alignment);
    RUN_TEST(test_opposing_and_adding_full_scale_inputs_saturate_safely);
    RUN_TEST(test_reset_restores_neutral_deterministic_state);
    return UNITY_END();
}
