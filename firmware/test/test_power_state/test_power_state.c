#include "unity.h"
#include <stdint.h>
#include "power_state.h"

static pstate_t last_from, last_to;
static pevent_t last_evt;
static int trans_count;

static void on_transition(pstate_t from, pstate_t to, pevent_t evt)
{
    last_from = from;
    last_to = to;
    last_evt = evt;
    trans_count++;
}

static pstate_actions_t actions = { on_transition };
static pstate_machine_t m;

void setUp(void)
{
    pstate_init(&m, &actions);
    last_from = last_to = (pstate_t)0;
    last_evt = (pevent_t)0;
    trans_count = 0;
}

void tearDown(void) {}

void test_boot_to_recording(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    TEST_ASSERT_EQUAL_UINT(PSTATE_RECORDING, pstate_get(&m));
    TEST_ASSERT_EQUAL_UINT(1, trans_count);
}

void test_boot_usb_attach_goes_idle(void)
{
    pstate_handle(&m, PEVT_USB_ATTACH);
    TEST_ASSERT_EQUAL_UINT(PSTATE_IDLE, pstate_get(&m));
}

void test_recording_to_low_and_back(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    pstate_handle(&m, PEVT_BATTERY_WARNING);
    TEST_ASSERT_EQUAL_UINT(PSTATE_RECORDING_LOW, pstate_get(&m));
    pstate_handle(&m, PEVT_BATTERY_OK);
    TEST_ASSERT_EQUAL_UINT(PSTATE_RECORDING, pstate_get(&m));
}

void test_recording_to_stopping_on_critical(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    pstate_handle(&m, PEVT_BATTERY_CRITICAL);
    TEST_ASSERT_EQUAL_UINT(PSTATE_STOPPING, pstate_get(&m));
}

void test_stopping_to_standby(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    pstate_handle(&m, PEVT_BATTERY_CRITICAL);
    pstate_handle(&m, PEVT_CARD_FAIL);   /* stop completes -> standby */
    TEST_ASSERT_EQUAL_UINT(PSTATE_STANDBY, pstate_get(&m));
}

void test_standby_wakeup_boots(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    pstate_handle(&m, PEVT_BATTERY_CRITICAL);
    pstate_handle(&m, PEVT_CARD_FAIL);
    pstate_handle(&m, PEVT_WAKEUP);
    TEST_ASSERT_EQUAL_UINT(PSTATE_BOOT, pstate_get(&m));
}

void test_idle_detach_resumes(void)
{
    pstate_handle(&m, PEVT_USB_ATTACH);
    pstate_handle(&m, PEVT_USB_DETACH);
    TEST_ASSERT_EQUAL_UINT(PSTATE_RECORDING, pstate_get(&m));
}

void test_boot_critical_goes_stopping(void)
{
    pstate_handle(&m, PEVT_BATTERY_CRITICAL);
    TEST_ASSERT_EQUAL_UINT(PSTATE_STOPPING, pstate_get(&m));
}

void test_recording_usb_attach_goes_idle(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    pstate_handle(&m, PEVT_USB_ATTACH);
    TEST_ASSERT_EQUAL_UINT(PSTATE_IDLE, pstate_get(&m));
}

void test_recording_card_fail_goes_stopping(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    pstate_handle(&m, PEVT_CARD_FAIL);
    TEST_ASSERT_EQUAL_UINT(PSTATE_STOPPING, pstate_get(&m));
}

void test_recording_low_critical_goes_stopping(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    pstate_handle(&m, PEVT_BATTERY_WARNING);
    pstate_handle(&m, PEVT_BATTERY_CRITICAL);
    TEST_ASSERT_EQUAL_UINT(PSTATE_STOPPING, pstate_get(&m));
}

void test_recording_low_card_fail_goes_stopping(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    pstate_handle(&m, PEVT_BATTERY_WARNING);
    pstate_handle(&m, PEVT_CARD_FAIL);
    TEST_ASSERT_EQUAL_UINT(PSTATE_STOPPING, pstate_get(&m));
}

void test_recording_low_usb_attach_goes_idle(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    pstate_handle(&m, PEVT_BATTERY_WARNING);
    pstate_handle(&m, PEVT_USB_ATTACH);
    TEST_ASSERT_EQUAL_UINT(PSTATE_IDLE, pstate_get(&m));
}

void test_ignored_event_stays_in_state(void)
{
    pstate_handle(&m, PEVT_CARD_FAIL);
    TEST_ASSERT_EQUAL_UINT(PSTATE_BOOT, pstate_get(&m));
    TEST_ASSERT_EQUAL_UINT(0, trans_count);
    pstate_handle(&m, PEVT_BOOT_OK);
    pstate_handle(&m, PEVT_BOOT_OK);
    TEST_ASSERT_EQUAL_UINT(PSTATE_RECORDING, pstate_get(&m));
    TEST_ASSERT_EQUAL_UINT(1, trans_count);
}

void test_transition_callback_reports_source_and_event(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    TEST_ASSERT_EQUAL_UINT(PSTATE_BOOT, last_from);
    TEST_ASSERT_EQUAL_UINT(PSTATE_RECORDING, last_to);
    TEST_ASSERT_EQUAL_UINT(PEVT_BOOT_OK, last_evt);
}

void test_null_actions_tolerated(void)
{
    pstate_machine_t noop;
    pstate_init(&noop, NULL);
    pstate_handle(&noop, PEVT_BOOT_OK);
    TEST_ASSERT_EQUAL_UINT(PSTATE_RECORDING, pstate_get(&noop));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_boot_to_recording);
    RUN_TEST(test_boot_usb_attach_goes_idle);
    RUN_TEST(test_recording_to_low_and_back);
    RUN_TEST(test_recording_to_stopping_on_critical);
    RUN_TEST(test_stopping_to_standby);
    RUN_TEST(test_standby_wakeup_boots);
    RUN_TEST(test_idle_detach_resumes);
    RUN_TEST(test_boot_critical_goes_stopping);
    RUN_TEST(test_recording_usb_attach_goes_idle);
    RUN_TEST(test_recording_card_fail_goes_stopping);
    RUN_TEST(test_recording_low_critical_goes_stopping);
    RUN_TEST(test_recording_low_card_fail_goes_stopping);
    RUN_TEST(test_recording_low_usb_attach_goes_idle);
    RUN_TEST(test_ignored_event_stays_in_state);
    RUN_TEST(test_transition_callback_reports_source_and_event);
    RUN_TEST(test_null_actions_tolerated);
    return UNITY_END();
}
