#include <unity.h>

#include "SdProtocol.h"

void setUp(void) {}
void tearDown(void) {}

void test_single_block_high_capacity_uses_cmd17_and_lba(void)
{
    const SdReadPlan plan = sd_make_read_plan(0x123456u, 1, true);
    TEST_ASSERT_EQUAL_UINT8(17, plan.read_command);
    TEST_ASSERT_EQUAL_UINT8(0, plan.stop_command);
    TEST_ASSERT_EQUAL_UINT32(0x123456u, plan.address);
}

void test_multi_block_high_capacity_uses_cmd18_and_cmd12(void)
{
    const SdReadPlan plan = sd_make_read_plan(400u, 32, true);
    TEST_ASSERT_EQUAL_UINT8(18, plan.read_command);
    TEST_ASSERT_EQUAL_UINT8(12, plan.stop_command);
    TEST_ASSERT_EQUAL_UINT32(400u, plan.address);
}

void test_standard_capacity_uses_byte_address(void)
{
    const SdReadPlan plan = sd_make_read_plan(123u, 4, false);
    TEST_ASSERT_EQUAL_UINT32(123u * 512u, plan.address);
}

void test_cmd12_parser_always_discards_the_stuff_byte(void)
{
    SdCmd12Response parser;
    sd_cmd12_response_init(&parser);

    TEST_ASSERT_FALSE(sd_cmd12_response_feed(&parser, 0x00));
    TEST_ASSERT_FALSE(parser.complete);
    TEST_ASSERT_FALSE(sd_cmd12_response_feed(&parser, 0xFF));
    TEST_ASSERT_TRUE(sd_cmd12_response_feed(&parser, 0x00));
    TEST_ASSERT_TRUE(parser.complete);
    TEST_ASSERT_EQUAL_HEX8(0x00, parser.response);
}

void test_cmd12_parser_accepts_the_first_r1_after_stuff(void)
{
    SdCmd12Response parser;
    sd_cmd12_response_init(&parser);

    TEST_ASSERT_FALSE(sd_cmd12_response_feed(&parser, 0xFF));
    TEST_ASSERT_TRUE(sd_cmd12_response_feed(&parser, 0x04));
    TEST_ASSERT_EQUAL_HEX8(0x04, parser.response);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_single_block_high_capacity_uses_cmd17_and_lba);
    RUN_TEST(test_multi_block_high_capacity_uses_cmd18_and_cmd12);
    RUN_TEST(test_standard_capacity_uses_byte_address);
    RUN_TEST(test_cmd12_parser_always_discards_the_stuff_byte);
    RUN_TEST(test_cmd12_parser_accepts_the_first_r1_after_stuff);
    return UNITY_END();
}
