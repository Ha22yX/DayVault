#include "unity.h"
#include <stdint.h>
#include <string.h>
#include "eventlog.h"

void setUp(void) {}
void tearDown(void) {}

void test_format_boot_event(void)
{
    utc_time_t t = {2026, 8, 1, 8, 30, 0};
    char buf[96];
    eventlog_format(&t, "boot", "rcc=0x40000", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2026-08-01T08:30:00Z,boot,rcc=0x40000", buf);
}

void test_format_without_detail(void)
{
    utc_time_t t = {2026, 8, 1, 8, 30, 5};
    char buf[96];
    eventlog_format(&t, "usb_attach", "", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2026-08-01T08:30:05Z,usb_attach", buf);
}

void test_format_null_detail_treated_as_omitted(void)
{
    utc_time_t t = {2026, 8, 1, 8, 30, 10};
    char buf[96];
    eventlog_format(&t, "rtc_sync", NULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2026-08-01T08:30:10Z,rtc_sync", buf);
}

void test_oversize_detail_truncated_safely(void)
{
    utc_time_t t = {2026, 8, 1, 8, 30, 15};
    char buf[64];
    char detail[200];
    memset(detail, 'x', sizeof(detail) - 1);
    detail[sizeof(detail) - 1] = 0;
    eventlog_format(&t, "long", detail, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(sizeof(buf) - 1, strlen(buf));
    TEST_ASSERT_EQUAL_UINT(0u, (uint8_t)buf[sizeof(buf) - 1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_format_boot_event);
    RUN_TEST(test_format_without_detail);
    RUN_TEST(test_format_null_detail_treated_as_omitted);
    RUN_TEST(test_oversize_detail_truncated_safely);
    return UNITY_END();
}
