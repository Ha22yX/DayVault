#include <unity.h>

#include <string.h>

#include "RecordingName.h"

void setUp(void) {}
void tearDown(void) {}

void test_timestamp_temp_name_keeps_stable_stem_and_collision(void)
{
    char path[64];
    TEST_ASSERT_TRUE(recording_format_timestamp_path(
        path, sizeof(path), "20260811-143025", 0u, false, 0u, "OPUS"));
    TEST_ASSERT_EQUAL_STRING("0:/REC-20260811-143025.OPUS", path);

    TEST_ASSERT_TRUE(recording_format_timestamp_path(
        path, sizeof(path), "20260811-143025", 3u, false, 0u, "OPUS"));
    TEST_ASSERT_EQUAL_STRING("0:/REC-20260811-143025_3.OPUS", path);
}

void test_final_collision_precedes_duration_suffix(void)
{
    char path[64];
    TEST_ASSERT_TRUE(recording_format_timestamp_path(
        path, sizeof(path), "20260811-143025", 0u, true, 62u, "OPUS"));
    TEST_ASSERT_EQUAL_STRING("0:/REC-20260811-143025_1m02s.OPUS", path);

    TEST_ASSERT_TRUE(recording_format_timestamp_path(
        path, sizeof(path), "20260811-143025", 2u, true, 62u, "OPUS"));
    TEST_ASSERT_EQUAL_STRING("0:/REC-20260811-143025_2_1m02s.OPUS", path);
}

void test_timestamp_path_rejects_truncation(void)
{
    char path[16];
    TEST_ASSERT_FALSE(recording_format_timestamp_path(
        path, sizeof(path), "20260811-143025", 0u, true, 62u, "OPUS"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_timestamp_temp_name_keeps_stable_stem_and_collision);
    RUN_TEST(test_final_collision_precedes_duration_suffix);
    RUN_TEST(test_timestamp_path_rejects_truncation);
    return UNITY_END();
}
