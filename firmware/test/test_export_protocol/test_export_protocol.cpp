#include <unity.h>
#include <string.h>

#include "ExportProtocol.h"

void setUp(void) {}
void tearDown(void) {}

void test_parse_get2_request(void)
{
    ExportRequest request;
    TEST_ASSERT_TRUE(export_parse_get2(
        "GET2 32768 REC-20260810-1200_1h00m00s.WAV", &request));
    TEST_ASSERT_EQUAL_UINT32(32768u, request.offset);
    TEST_ASSERT_EQUAL_STRING("REC-20260810-1200_1h00m00s.WAV", request.filename);
}

void test_parse_get2_allows_zero_offset(void)
{
    ExportRequest request;
    TEST_ASSERT_TRUE(export_parse_get2("GET2 0 REC001.WAV", &request));
    TEST_ASSERT_EQUAL_UINT32(0u, request.offset);
}

void test_parse_get2_rejects_invalid_requests(void)
{
    ExportRequest request;
    TEST_ASSERT_FALSE(export_parse_get2("GET2 REC001.WAV", &request));
    TEST_ASSERT_FALSE(export_parse_get2("GET2 -1 REC001.WAV", &request));
    TEST_ASSERT_FALSE(export_parse_get2("GET2 4294967296 REC001.WAV", &request));
    TEST_ASSERT_FALSE(export_parse_get2("GET2 0 ../REC001.WAV", &request));
    TEST_ASSERT_FALSE(export_parse_get2("GET2 0 folder/REC001.WAV", &request));
    TEST_ASSERT_FALSE(export_parse_get2("GET2 0 REC 001.WAV", &request));
    TEST_ASSERT_FALSE(export_parse_get2("GET2 0", &request));
}

void test_format_completion_contains_crc_and_measured_rate(void)
{
    char output[160];
    const size_t length = export_format_completion(
        output, sizeof(output), "bulk", 2048u, 10u, 0x89ABCDEFu);

    TEST_ASSERT_EQUAL_UINT32(strlen(output), length);
    TEST_ASSERT_EQUAL_STRING(
        "GET2END sent=2048 crc32=89ABCDEF\r\n"
        "BENCH bulk bytes=2048 ms=10 kib_s=200 crc32=89ABCDEF\r\n",
        output);
}

void test_format_completion_rejects_truncated_output(void)
{
    char output[16];
    TEST_ASSERT_EQUAL_UINT32(
        0u,
        export_format_completion(
            output, sizeof(output), "bulk", 2048u, 10u, 0x89ABCDEFu));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_get2_request);
    RUN_TEST(test_parse_get2_allows_zero_offset);
    RUN_TEST(test_parse_get2_rejects_invalid_requests);
    RUN_TEST(test_format_completion_contains_crc_and_measured_rate);
    RUN_TEST(test_format_completion_rejects_truncated_output);
    return UNITY_END();
}
