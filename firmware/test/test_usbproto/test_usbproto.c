#include "unity.h"
#include <string.h>
#include "usbproto.h"

static usbproto_t p;

void setUp(void)
{
    usbproto_init(&p);
}
void tearDown(void)
{
}

static void feed_line(usbproto_t *p, const char *line)
{
    size_t i;
    for (i = 0; line[i]; i++)
        usbproto_feed(p, (uint8_t)line[i]);
}

void test_dfusn_returns_dfu_event(void)
{
    feed_line(&p, "DFU\n");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_DFU, usbproto_poll(&p));
}

void test_dfu_crlf_returns_dfu_event(void)
{
    feed_line(&p, "DFU\r\n");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_DFU, usbproto_poll(&p));
}

void test_unknown_line_returns_unknown(void)
{
    feed_line(&p, "STAT\n");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_UNKNOWN, usbproto_poll(&p));
}

void test_empty_line_returns_none(void)
{
    feed_line(&p, "\n");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_NONE, usbproto_poll(&p));
}

void test_partial_line_returns_none(void)
{
    feed_line(&p, "D");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_NONE, usbproto_poll(&p));
    feed_line(&p, "FU");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_NONE, usbproto_poll(&p));
}

void test_oversize_line_discards_to_newline(void)
{
    uint8_t i;
    for (i = 0; i < 64; i++)
        usbproto_feed(&p, 'X');
    usbproto_feed(&p, '\n');
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_UNKNOWN, usbproto_poll(&p));
    feed_line(&p, "DFU\n");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_DFU, usbproto_poll(&p));
}

void test_discard_newline_resets_for_next_line(void)
{
    uint8_t i;
    for (i = 0; i < 70; i++)
        usbproto_feed(&p, 'X');
    usbproto_feed(&p, '\n');
    feed_line(&p, "DFU\n");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_DFU, usbproto_poll(&p));
}

void test_poll_read_and_clear(void)
{
    feed_line(&p, "DFU\n");
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_DFU, usbproto_poll(&p));
    TEST_ASSERT_EQUAL_UINT(USBPROTO_EVT_NONE, usbproto_poll(&p));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dfusn_returns_dfu_event);
    RUN_TEST(test_dfu_crlf_returns_dfu_event);
    RUN_TEST(test_unknown_line_returns_unknown);
    RUN_TEST(test_empty_line_returns_none);
    RUN_TEST(test_partial_line_returns_none);
    RUN_TEST(test_oversize_line_discards_to_newline);
    RUN_TEST(test_discard_newline_resets_for_next_line);
    RUN_TEST(test_poll_read_and_clear);
    return UNITY_END();
}
