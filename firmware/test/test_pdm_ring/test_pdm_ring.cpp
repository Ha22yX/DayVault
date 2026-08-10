#include <unity.h>

#include "PdmRing.h"

static const uint32_t kBufferSamples = 8192u;
static const uint32_t kFreeMargin = 64u;

void setUp(void) {}
void tearDown(void) {}

void test_transfer_complete_count_preserves_exact_full_wrap(void)
{
    TEST_ASSERT_EQUAL_UINT32(
        8192u, pdm_ring_produced_from_tc(1u, kBufferSamples, kBufferSamples));
}

void test_transfer_complete_count_preserves_multiple_wraps(void)
{
    TEST_ASSERT_EQUAL_UINT32(
        24960u, pdm_ring_produced_from_tc(3u, kBufferSamples, 7808u));
}

void test_transfer_complete_count_handles_cross_wrap_progress(void)
{
    TEST_ASSERT_EQUAL_UINT32(
        16768u, pdm_ring_produced_from_tc(2u, kBufferSamples, 7808u));
}

void test_pair_availability_uses_the_slower_channel(void)
{
    TEST_ASSERT_EQUAL_UINT32(
        336u, pdm_ring_pair_available(10500u, 10400u, 10000u, kFreeMargin));
}

void test_recovery_uses_one_common_consumed_index(void)
{
    const PdmRingRecovery recovery = pdm_ring_recover_pair(
        20000u, 19980u, 10000u, kBufferSamples, kFreeMargin);

    TEST_ASSERT_TRUE(recovery.overrun);
    TEST_ASSERT_TRUE(recovery.has_common_data);
    TEST_ASSERT_EQUAL_UINT32(11872u, recovery.common_consumed);
    TEST_ASSERT_EQUAL_UINT32(8044u, recovery.available);
    TEST_ASSERT_EQUAL_UINT32(
        recovery.common_consumed % kBufferSamples,
        pdm_ring_index(recovery.common_consumed, kBufferSamples));
}

void test_recovery_does_not_invent_samples_for_a_lagging_channel(void)
{
    const PdmRingRecovery recovery = pdm_ring_recover_pair(
        20000u, 10000u, 10000u, kBufferSamples, kFreeMargin);

    TEST_ASSERT_TRUE(recovery.overrun);
    TEST_ASSERT_FALSE(recovery.has_common_data);
    TEST_ASSERT_EQUAL_UINT32(10000u, recovery.common_consumed);
    TEST_ASSERT_EQUAL_UINT32(0u, recovery.available);
}

void test_compatibility_wrapper_limits_each_read_to_128_samples(void)
{
    TEST_ASSERT_EQUAL_UINT32(128u, pdm_compat_read_chunk(257u));
    TEST_ASSERT_EQUAL_UINT32(128u, pdm_compat_read_chunk(129u));
    TEST_ASSERT_EQUAL_UINT32(1u, pdm_compat_read_chunk(1u));
    TEST_ASSERT_EQUAL_UINT32(0u, pdm_compat_read_chunk(0u));
}

void test_frozen_pair_recovery_releases_live_writer_margin(void)
{
    const PdmRingRecovery live = pdm_ring_recover_pair(
        10064u, 10064u, 10000u, kBufferSamples, kFreeMargin);
    const PdmRingRecovery frozen = pdm_ring_recover_frozen_pair(
        10064u, 10064u, 10000u, kBufferSamples);

    TEST_ASSERT_EQUAL_UINT32(0u, live.available);
    TEST_ASSERT_EQUAL_UINT32(64u, frozen.available);
    TEST_ASSERT_EQUAL_UINT32(10000u, frozen.common_consumed);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_transfer_complete_count_preserves_exact_full_wrap);
    RUN_TEST(test_transfer_complete_count_preserves_multiple_wraps);
    RUN_TEST(test_transfer_complete_count_handles_cross_wrap_progress);
    RUN_TEST(test_pair_availability_uses_the_slower_channel);
    RUN_TEST(test_recovery_uses_one_common_consumed_index);
    RUN_TEST(test_recovery_does_not_invent_samples_for_a_lagging_channel);
    RUN_TEST(test_compatibility_wrapper_limits_each_read_to_128_samples);
    RUN_TEST(test_frozen_pair_recovery_releases_live_writer_margin);
    return UNITY_END();
}
