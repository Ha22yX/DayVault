#include "unity.h"
#include "rec_mgr.h"

static rec_mgr_t mgr;
static int start_capture_calls, stop_capture_calls, finalize_calls;
static int mount_calls, unmount_calls, start_msc_calls, stop_msc_calls;

static void act_start_capture(void) { start_capture_calls++; }
static void act_stop_capture(void) { stop_capture_calls++; }
static void act_finalize(void) { finalize_calls++; }
static void act_mount(void) { mount_calls++; }
static void act_unmount(void) { unmount_calls++; }
static void act_start_msc(void) { start_msc_calls++; }
static void act_stop_msc(void) { stop_msc_calls++; }

static const rec_actions_t actions = {
    act_start_capture, act_stop_capture, act_finalize,
    act_mount, act_unmount, act_start_msc, act_stop_msc
};

void setUp(void)
{
    rec_mgr_init(&mgr, &actions);
    start_capture_calls = 0;
    stop_capture_calls = 0;
    finalize_calls = 0;
    mount_calls = 0;
    unmount_calls = 0;
    start_msc_calls = 0;
    stop_msc_calls = 0;
}
void tearDown(void)
{
}

void test_init_state_idle(void)
{
    TEST_ASSERT_EQUAL_UINT(REC_IDLE, rec_mgr_state(&mgr));
}

void test_detach_starts_recording(void)
{
    rec_mgr_event(&mgr, REC_EVT_USB_DETACH);
    TEST_ASSERT_EQUAL_UINT(REC_RECORDING, rec_mgr_state(&mgr));
    TEST_ASSERT_EQUAL_INT(1, start_capture_calls);
}

void test_attach_stops_and_goes_msc(void)
{
    rec_mgr_event(&mgr, REC_EVT_USB_DETACH);   /* -> RECORDING */
    rec_mgr_event(&mgr, REC_EVT_CAPTURE_STARTED);
    rec_mgr_event(&mgr, REC_EVT_USB_ATTACH);   /* -> STOPPING */
    TEST_ASSERT_EQUAL_UINT(REC_STOPPING, rec_mgr_state(&mgr));
    TEST_ASSERT_EQUAL_INT(1, stop_capture_calls);
    rec_mgr_event(&mgr, REC_EVT_FINALIZE_DONE); /* -> MSC */
    TEST_ASSERT_EQUAL_UINT(REC_MSC, rec_mgr_state(&mgr));
    TEST_ASSERT_EQUAL_INT(1, finalize_calls);
    TEST_ASSERT_EQUAL_INT(1, unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, start_msc_calls);
}

void test_msc_detach_resumes_recording(void)
{
    rec_mgr_event(&mgr, REC_EVT_USB_DETACH);
    rec_mgr_event(&mgr, REC_EVT_CAPTURE_STARTED);
    rec_mgr_event(&mgr, REC_EVT_USB_ATTACH);
    rec_mgr_event(&mgr, REC_EVT_FINALIZE_DONE);
    TEST_ASSERT_EQUAL_UINT(REC_MSC, rec_mgr_state(&mgr));
    rec_mgr_event(&mgr, REC_EVT_USB_DETACH);   /* -> RECORDING, new file */
    TEST_ASSERT_EQUAL_UINT(REC_RECORDING, rec_mgr_state(&mgr));
    TEST_ASSERT_EQUAL_INT(1, stop_msc_calls);
    TEST_ASSERT_EQUAL_INT(1, mount_calls);
}

void test_attach_in_idle_goes_msc(void)
{
    rec_mgr_event(&mgr, REC_EVT_USB_ATTACH);
    TEST_ASSERT_EQUAL_UINT(REC_STOPPING, rec_mgr_state(&mgr));
    rec_mgr_event(&mgr, REC_EVT_FINALIZE_DONE);
    TEST_ASSERT_EQUAL_UINT(REC_MSC, rec_mgr_state(&mgr));
}

void test_null_actions_tolerated(void)
{
    rec_mgr_t m;
    rec_mgr_init(&m, 0);
    rec_mgr_event(&m, REC_EVT_USB_DETACH);
    TEST_ASSERT_EQUAL_UINT(REC_RECORDING, rec_mgr_state(&m));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_state_idle);
    RUN_TEST(test_detach_starts_recording);
    RUN_TEST(test_attach_stops_and_goes_msc);
    RUN_TEST(test_msc_detach_resumes_recording);
    RUN_TEST(test_attach_in_idle_goes_msc);
    RUN_TEST(test_null_actions_tolerated);
    return UNITY_END();
}
