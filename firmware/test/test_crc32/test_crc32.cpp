#include <unity.h>

#include "Crc32.h"

void setUp(void) {}
void tearDown(void) {}

void test_crc32_empty_is_zero(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, crc32_compute(nullptr, 0));
}

void test_crc32_matches_ieee_standard_vector(void)
{
    static const uint8_t text[] = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32_compute(text, sizeof(text) - 1));
}

void test_crc32_chunked_matches_single_pass(void)
{
    static const uint8_t text[] = "DayVault high speed transfer";
    uint32_t chunked = crc32_update(0, text, 8);
    chunked = crc32_update(chunked, text + 8, sizeof(text) - 1 - 8);
    TEST_ASSERT_EQUAL_HEX32(crc32_compute(text, sizeof(text) - 1), chunked);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_empty_is_zero);
    RUN_TEST(test_crc32_matches_ieee_standard_vector);
    RUN_TEST(test_crc32_chunked_matches_single_pass);
    return UNITY_END();
}
