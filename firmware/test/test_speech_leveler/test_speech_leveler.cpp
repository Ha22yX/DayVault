#include <unity.h>

#include "SpeechLeveler.h"

static const uint32_t kRate = 16000u;
static const uint32_t kFrames = 128u;
static int16_t input[kFrames];
static int16_t output[kFrames];
static int16_t expected[kFrames];

void setUp(void) {}
void tearDown(void) {}

static void make_square(int16_t amplitude)
{
    for (uint32_t i = 0; i < kFrames; i++) input[i] = (i & 1u) == 0u ? amplitude : -amplitude;
}

static uint32_t xorshift32(uint32_t* state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void make_low_noise(uint32_t* state)
{
    for (uint32_t i = 0; i < kFrames; i++) {
        input[i] = (int16_t)(((int32_t)(xorshift32(state) & 0xffffu) - 32768) / 128);
    }
}

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static void process_blocks(SpeechLeveler* leveler, int16_t amplitude, bool speech,
                           uint32_t blocks)
{
    for (uint32_t block = 0; block < blocks; block++) {
        make_square(amplitude);
        leveler->process(input, output, kFrames, speech);
    }
}

void test_silence_non_speech_never_rises_above_unity(void)
{
    SpeechLeveler leveler;
    leveler.reset(kRate);
    process_blocks(&leveler, 0, false, 32u);

    TEST_ASSERT_TRUE(leveler.stats().gain_q16 <= 65536u);
    for (uint32_t i = 0; i < kFrames; i++) TEST_ASSERT_EQUAL_INT16(0, output[i]);
}

void test_quiet_speech_ramps_gradually_without_exceeding_four_times(void)
{
    SpeechLeveler leveler;
    leveler.reset(kRate);
    uint32_t previous_gain = leveler.stats().gain_q16;
    for (uint32_t block = 0; block < 40u; block++) {
        make_square(1000);
        leveler.process(input, output, kFrames, true);
        const uint32_t gain = leveler.stats().gain_q16;
        TEST_ASSERT_TRUE(gain >= previous_gain);
        TEST_ASSERT_TRUE(gain - previous_gain <= SpeechLeveler::kQuietRiseStepQ16);
        TEST_ASSERT_TRUE(gain <= 262144u);
        previous_gain = gain;
    }
    TEST_ASSERT_TRUE(leveler.stats().gain_q16 > 65536u);
}

void test_quiet_speech_reaches_four_times_inside_half_a_second(void)
{
    SpeechLeveler leveler;
    leveler.reset(kRate);
    process_blocks(&leveler, 1000, true, 48u);

    TEST_ASSERT_EQUAL_UINT32(262144u, leveler.stats().gain_q16);
}

void test_loud_speech_reduces_gain_faster_than_quiet_speech_raises_it(void)
{
    SpeechLeveler quiet;
    SpeechLeveler loud;
    quiet.reset(kRate);
    loud.reset(kRate);
    make_square(1000);
    quiet.process(input, output, kFrames, true);
    const uint32_t quiet_rise = quiet.stats().gain_q16 - 65536u;

    process_blocks(&loud, 1000, true, 24u);
    const uint32_t before_loud = loud.stats().gain_q16;
    make_square(24000);
    loud.process(input, output, kFrames, true);
    const uint32_t loud_drop = before_loud - loud.stats().gain_q16;

    TEST_ASSERT_TRUE(loud_drop > quiet_rise);
}

void test_full_scale_transients_are_limited_without_wrap(void)
{
    SpeechLeveler leveler;
    leveler.reset(kRate);
    process_blocks(&leveler, 1000, true, 32u);
    make_square(32767);
    leveler.process(input, output, kFrames, true);

    for (uint32_t i = 0; i < kFrames; i++) {
        TEST_ASSERT_TRUE(output[i] <= 32700 && output[i] >= -32700);
    }
    TEST_ASSERT_TRUE(leveler.stats().limiter_activations > 0u);
    TEST_ASSERT_TRUE(leveler.stats().prevented_clip_events > 0u);
}

void test_non_speech_release_toward_unity_is_gradual(void)
{
    SpeechLeveler leveler;
    leveler.reset(kRate);
    process_blocks(&leveler, 1000, true, 32u);
    const uint32_t before_release = leveler.stats().gain_q16;
    make_square(0);
    leveler.process(input, output, kFrames, false);
    const uint32_t after_release = leveler.stats().gain_q16;

    TEST_ASSERT_TRUE(after_release < before_release);
    TEST_ASSERT_TRUE(before_release - after_release <= SpeechLeveler::kReleaseStepQ16);
    TEST_ASSERT_TRUE(after_release >= 65536u);
}

void test_non_speech_low_noise_is_not_amplified_while_gain_releases(void)
{
    SpeechLeveler leveler;
    leveler.reset(kRate);
    process_blocks(&leveler, 1000, true, 24u);
    TEST_ASSERT_TRUE(leveler.stats().gain_q16 > 65536u);

    uint32_t noise_state = 0x5a5a5a5au;
    make_low_noise(&noise_state);
    for (uint32_t i = 0; i < kFrames; i++) expected[i] = input[i];
    leveler.process(input, output, kFrames, false);

    TEST_ASSERT_TRUE(leveler.stats().gain_q16 > 65536u);
    TEST_ASSERT_EQUAL_UINT32(65536u, leveler.stats().applied_gain_q16);
    for (uint32_t i = 0; i < kFrames; i++) {
        TEST_ASSERT_TRUE(abs_i32(output[i]) <= abs_i32(expected[i]) + 1);
    }
}

void test_bypass_is_bit_identical_and_reset_is_deterministic(void)
{
    SpeechLeveler leveler;
    leveler.reset(kRate);
    leveler.set_bypass(true);
    make_square(12000);
    leveler.process(input, output, kFrames, true);
    TEST_ASSERT_EQUAL_INT16_ARRAY(input, output, kFrames);

    leveler.reset(kRate);
    TEST_ASSERT_EQUAL_UINT32(65536u, leveler.stats().gain_q16);
    TEST_ASSERT_EQUAL_UINT32(0u, leveler.stats().limiter_activations);
    TEST_ASSERT_EQUAL_UINT32(0u, leveler.stats().prevented_clip_events);
    TEST_ASSERT_FALSE(leveler.stats().bypass);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_silence_non_speech_never_rises_above_unity);
    RUN_TEST(test_quiet_speech_ramps_gradually_without_exceeding_four_times);
    RUN_TEST(test_quiet_speech_reaches_four_times_inside_half_a_second);
    RUN_TEST(test_loud_speech_reduces_gain_faster_than_quiet_speech_raises_it);
    RUN_TEST(test_full_scale_transients_are_limited_without_wrap);
    RUN_TEST(test_non_speech_release_toward_unity_is_gradual);
    RUN_TEST(test_non_speech_low_noise_is_not_amplified_while_gain_releases);
    RUN_TEST(test_bypass_is_bit_identical_and_reset_is_deterministic);
    return UNITY_END();
}
