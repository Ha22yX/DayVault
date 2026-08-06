#include "unity.h"
#include <stdint.h>
#include <string.h>
#include "timeutil.h"

static utc_time_t t_2026_08_01 = {2026, 8, 1, 8, 30, 0};

void setUp(void) {}
void tearDown(void) {}

void test_leap_years(void)
{
    TEST_ASSERT_TRUE(timeutil_is_leap(2000));
    TEST_ASSERT_TRUE(timeutil_is_leap(2024));
    TEST_ASSERT_FALSE(timeutil_is_leap(2100));
    TEST_ASSERT_FALSE(timeutil_is_leap(2025));
}

void test_days_in_month(void)
{
    TEST_ASSERT_EQUAL_UINT(31u, timeutil_days_in_month(2026, 1));
    TEST_ASSERT_EQUAL_UINT(28u, timeutil_days_in_month(2026, 2));
    TEST_ASSERT_EQUAL_UINT(29u, timeutil_days_in_month(2024, 2));
    TEST_ASSERT_EQUAL_UINT(0u, timeutil_days_in_month(2026, 0));
    TEST_ASSERT_EQUAL_UINT(0u, timeutil_days_in_month(2026, 13));
}

void test_validity(void)
{
    utc_time_t t = t_2026_08_01;
    TEST_ASSERT_TRUE(timeutil_is_valid(&t));
    t.hour = 24; TEST_ASSERT_FALSE(timeutil_is_valid(&t)); t = t_2026_08_01;
    t.month = 2; t.day = 30; TEST_ASSERT_FALSE(timeutil_is_valid(&t)); t = t_2026_08_01;
    t.year = 2099; t.month = 12; t.day = 31; TEST_ASSERT_TRUE(timeutil_is_valid(&t));
}

void test_epoch_days_reference(void)
{
    utc_time_t t = {1970, 1, 1, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT(0u, timeutil_to_epoch_days(&t));
    t = (utc_time_t){2000, 1, 1, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT(10957u, timeutil_to_epoch_days(&t));
}

void test_diff_seconds(void)
{
    utc_time_t later = {2026, 8, 1, 9, 0, 0};
    TEST_ASSERT_EQUAL_INT64(1800, timeutil_diff_seconds(&later, &t_2026_08_01));
    TEST_ASSERT_EQUAL_INT64(-1800, timeutil_diff_seconds(&t_2026_08_01, &later));
}

void test_format_ts(void)
{
    char buf[32];
    timeutil_format_ts(&t_2026_08_01, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("20260801T083000Z", buf);
}

void test_format_iso(void)
{
    char buf[32];
    timeutil_format_iso(&t_2026_08_01, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2026-08-01T08:30:00Z", buf);
}

void test_day_path(void)
{
    char buf[64];
    timeutil_make_day_path(&t_2026_08_01, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("DAYVAULT/2026/08/01", buf);
}

void test_unsynced_path(void)
{
    char buf[64];
    timeutil_make_unsynced_path(7u, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("DAYVAULT/UNSYNCED/BOOT0007", buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_leap_years);
    RUN_TEST(test_days_in_month);
    RUN_TEST(test_validity);
    RUN_TEST(test_epoch_days_reference);
    RUN_TEST(test_diff_seconds);
    RUN_TEST(test_format_ts);
    RUN_TEST(test_format_iso);
    RUN_TEST(test_day_path);
    RUN_TEST(test_unsynced_path);
    return UNITY_END();
}
