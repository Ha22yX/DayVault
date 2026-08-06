#include "unity.h"
#include <stdint.h>
#include <string.h>
#include "segmgr.h"
#include "timeutil.h"

static segmgr_t m;
static utc_time_t t0 = {2026, 8, 1, 8, 30, 0};

void setUp(void)
{
    segmgr_init(&m, 900u, 0u);
}

void tearDown(void) {}

void test_not_open_does_not_rotate(void)
{
    utc_time_t t = {2026, 8, 1, 9, 0, 0};
    TEST_ASSERT_FALSE(segmgr_should_rotate(&m, &t));
}

void test_rotation_after_window(void)
{
    utc_time_t t;
    segmgr_open(&m, &t0);
    t = t0; t.minute = 44; t.second = 59;   /* 14:59 elapsed */
    TEST_ASSERT_FALSE(segmgr_should_rotate(&m, &t));
    t = t0; t.minute = 45; t.second = 0;     /* 15:00 elapsed */
    TEST_ASSERT_TRUE(segmgr_should_rotate(&m, &t));
}

void test_open_increments_seq(void)
{
    utc_time_t t = {2026, 8, 1, 8, 45, 0};
    char name[64];
    segmgr_open(&m, &t0);
    TEST_ASSERT_EQUAL_UINT(1u, segmgr_seq(&m));
    segmgr_open(&m, &t);
    TEST_ASSERT_EQUAL_UINT(2u, segmgr_seq(&m));
    segmgr_build_name(&m, &t, name, sizeof(name));
    TEST_ASSERT_EQUAL_STRING("20260801T084500Z_0002.wav", name);
}

void test_open_and_close_resets_rotation(void)
{
    utc_time_t t = {2026, 8, 1, 9, 15, 0};
    segmgr_open(&m, &t0);
    segmgr_close(&m);
    segmgr_open(&m, &t);
    TEST_ASSERT_FALSE(segmgr_should_rotate(&m, &t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_not_open_does_not_rotate);
    RUN_TEST(test_rotation_after_window);
    RUN_TEST(test_open_increments_seq);
    RUN_TEST(test_open_and_close_resets_rotation);
    return UNITY_END();
}
