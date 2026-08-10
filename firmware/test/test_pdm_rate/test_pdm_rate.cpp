#include <unity.h>

#include "PdmRate.h"

void setUp(void) {}
void tearDown(void) {}

void test_divider_52_keeps_mic_in_normal_mode(void)
{
    TEST_ASSERT_EQUAL_UINT32(1538461u, pdm_clock_hz(80000000u, 52u));
    TEST_ASSERT_TRUE(pdm_clock_is_sph0655_normal(80000000u, 52u));
}

void test_osr_96_produces_near_16khz(void)
{
    TEST_ASSERT_EQUAL_UINT32(16026u, pdm_pcm_rate_hz(80000000u, 52u, 96u));
}

void test_invalid_divider_is_rejected(void)
{
    TEST_ASSERT_FALSE(pdm_clock_is_sph0655_normal(80000000u, 13u));
    TEST_ASSERT_EQUAL_UINT32(0u, pdm_clock_hz(80000000u, 0u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_divider_52_keeps_mic_in_normal_mode);
    RUN_TEST(test_osr_96_produces_near_16khz);
    RUN_TEST(test_invalid_divider_is_rejected);
    return UNITY_END();
}
