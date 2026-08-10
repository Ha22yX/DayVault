#include <unity.h>
#include "WinUsbDescriptors.h"

void setUp(void) {}
void tearDown(void) {}

static uint16_t u16(const uint8_t* data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8u);
}

static void test_device_has_distinct_st_vid_pid(void)
{
    uint16_t length = 0;
    uint8_t* descriptor = winusb_device_descriptor(&length);
    TEST_ASSERT_EQUAL_UINT16(18, length);
    TEST_ASSERT_EQUAL_HEX16(WINUSB_VENDOR_ID, u16(descriptor + 8));
    TEST_ASSERT_EQUAL_HEX16(WINUSB_PRODUCT_ID, u16(descriptor + 10));
}

static void test_configuration_has_bulk_in_and_ack_out_endpoints(void)
{
    uint16_t length = 0;
    uint8_t* descriptor = winusb_config_descriptor(&length);
    TEST_ASSERT_EQUAL_UINT16(32, length);
    TEST_ASSERT_EQUAL_UINT16(length, u16(descriptor + 2));
    TEST_ASSERT_EQUAL_UINT8(1, descriptor[4]);
    TEST_ASSERT_EQUAL_UINT8(2, descriptor[13]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, descriptor[14]);
    TEST_ASSERT_EQUAL_HEX8(WINUSB_BULK_IN_ENDPOINT, descriptor[20]);
    TEST_ASSERT_EQUAL_HEX8(0x02, descriptor[21]);
    TEST_ASSERT_EQUAL_UINT16(64, u16(descriptor + 22));
    TEST_ASSERT_EQUAL_HEX8(WINUSB_BULK_OUT_ENDPOINT, descriptor[27]);
    TEST_ASSERT_EQUAL_HEX8(0x02, descriptor[28]);
    TEST_ASSERT_EQUAL_UINT16(64, u16(descriptor + 29));
}

static void test_os_descriptors_request_winusb_for_interface_zero(void)
{
    uint16_t os_length = 0;
    uint8_t* os = winusb_os_string_descriptor(&os_length);
    TEST_ASSERT_EQUAL_UINT16(18, os_length);
    TEST_ASSERT_EQUAL_HEX8(WINUSB_MS_VENDOR_CODE, os[16]);

    uint16_t compat_length = 0;
    uint8_t* compat = winusb_compat_id_descriptor(&compat_length);
    TEST_ASSERT_EQUAL_UINT16(40, compat_length);
    TEST_ASSERT_EQUAL_UINT32(40, (uint32_t)compat[0]);
    TEST_ASSERT_EQUAL_UINT8(WINUSB_INTERFACE_NUMBER, compat[16]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("WINUSB", compat + 18, 6);
}

static void test_only_expected_vendor_request_is_accepted(void)
{
    TEST_ASSERT_TRUE(winusb_is_compat_id_request(WINUSB_MS_VENDOR_CODE, 0x0004));
    TEST_ASSERT_FALSE(winusb_is_compat_id_request(WINUSB_MS_VENDOR_CODE, 0x0005));
    TEST_ASSERT_FALSE(winusb_is_compat_id_request(0x21, 0x0004));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_device_has_distinct_st_vid_pid);
    RUN_TEST(test_configuration_has_bulk_in_and_ack_out_endpoints);
    RUN_TEST(test_os_descriptors_request_winusb_for_interface_zero);
    RUN_TEST(test_only_expected_vendor_request_is_accepted);
    return UNITY_END();
}
