#include "unity.h"
#include <stdint.h>
#include <string.h>
#include "usbproto.h"

static usbproto_parser_t p;

void setUp(void) { usbproto_init(&p); }

void tearDown(void) {}

void test_time_command(void)
{
    usbproto_msg_t m;
    const char *line = "TIME\r\n";
    size_t i;
    usbproto_result_t r = USBPROTO_NEED_MORE;
    for (i = 0; line[i]; i++)
        r = usbproto_feed(&p, (uint8_t)line[i], &m);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_OK, r);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_CMD_TIME, m.cmd);
}

void test_sync_command(void)
{
    usbproto_msg_t m;
    const char *line = "SYNC 1750000000\n";
    size_t i;
    usbproto_result_t r = USBPROTO_NEED_MORE;
    for (i = 0; line[i]; i++)
        r = usbproto_feed(&p, (uint8_t)line[i], &m);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_OK, r);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_CMD_SYNC, m.cmd);
    TEST_ASSERT_EQUAL_UINT32(1750000000u, m.arg);
}

void test_unknown_command(void)
{
    usbproto_msg_t m;
    const char *line = "BOGUS\r\n";
    size_t i;
    usbproto_result_t r = USBPROTO_NEED_MORE;
    for (i = 0; line[i]; i++)
        r = usbproto_feed(&p, (uint8_t)line[i], &m);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_UNKNOWN, r);
}

void test_partial_line_needs_more(void)
{
    usbproto_msg_t m;
    usbproto_result_t r = usbproto_feed(&p, 'T', &m);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_NEED_MORE, r);
    r = usbproto_feed(&p, 'I', &m);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_NEED_MORE, r);
}

void test_oversize_line_unknown(void)
{
    usbproto_msg_t m;
    char line[80];
    size_t i;
    memset(line, 'X', sizeof(line) - 2);
    line[sizeof(line) - 2] = '\n';
    line[sizeof(line) - 1] = 0;
    usbproto_result_t r = USBPROTO_NEED_MORE;
    for (i = 0; line[i]; i++)
        r = usbproto_feed(&p, (uint8_t)line[i], &m);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_UNKNOWN, r);
}

void test_oversize_tail_cannot_parse_command(void)
{
    usbproto_msg_t m;
    char line[80];
    size_t i;
    memset(line, 'X', 64);
    strcpy(line + 64, "TIME\n");
    usbproto_result_t r = USBPROTO_NEED_MORE;
    for (i = 0; line[i]; i++)
        r = usbproto_feed(&p, (uint8_t)line[i], &m);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_UNKNOWN, r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_time_command);
    RUN_TEST(test_sync_command);
    RUN_TEST(test_unknown_command);
    RUN_TEST(test_partial_line_needs_more);
    RUN_TEST(test_oversize_line_unknown);
    RUN_TEST(test_oversize_tail_cannot_parse_command);
    return UNITY_END();
}
