#include <unity.h>

#include "PdmRate.h"

void setUp(void) {}
void tearDown(void) {}

void test_divider_52_keeps_mic_in_normal_mode(void)
{
    TEST_ASSERT_EQUAL_UINT32(
        1538461u,
        pdm_clock_hz(PDM_DFSDM_SOURCE_HZ, PDM_PRODUCTION_CKOUT_DIVIDER));
    TEST_ASSERT_TRUE(pdm_clock_is_sph0655_normal(
        PDM_DFSDM_SOURCE_HZ, PDM_PRODUCTION_CKOUT_DIVIDER));
}

void test_osr_96_produces_near_16khz(void)
{
    TEST_ASSERT_EQUAL_UINT32(
        16026u,
        pdm_pcm_rate_hz(PDM_DFSDM_SOURCE_HZ, PDM_PRODUCTION_CKOUT_DIVIDER,
                        PDM_PRODUCTION_OSR));
}

void test_invalid_divider_is_rejected(void)
{
    TEST_ASSERT_FALSE(pdm_clock_is_sph0655_normal(PDM_DFSDM_SOURCE_HZ, 13u));
    TEST_ASSERT_EQUAL_UINT32(0u, pdm_clock_hz(PDM_DFSDM_SOURCE_HZ, 0u));
    TEST_ASSERT_EQUAL_UINT32(
        0u,
        pdm_pcm_rate_hz(PDM_DFSDM_SOURCE_HZ, PDM_PRODUCTION_CKOUT_DIVIDER, 0u));
}

void test_production_clock_and_rate_contract(void)
{
    const uint32_t clock = pdm_clock_hz(
        PDM_DFSDM_SOURCE_HZ, PDM_PRODUCTION_CKOUT_DIVIDER);
    TEST_ASSERT_EQUAL_UINT32(1538461u, clock);
    TEST_ASSERT_EQUAL_UINT32(
        16026u,
        pdm_pcm_rate_hz(PDM_DFSDM_SOURCE_HZ, PDM_PRODUCTION_CKOUT_DIVIDER,
                        PDM_PRODUCTION_OSR));
    TEST_ASSERT_TRUE(pdm_clock_is_sph0655_normal(
        PDM_DFSDM_SOURCE_HZ, PDM_PRODUCTION_CKOUT_DIVIDER));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_divider_52_keeps_mic_in_normal_mode);
    RUN_TEST(test_osr_96_produces_near_16khz);
    RUN_TEST(test_invalid_divider_is_rejected);
    RUN_TEST(test_production_clock_and_rate_contract);
    return UNITY_END();
}
