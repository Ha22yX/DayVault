#pragma once

#include <stddef.h>
#include <stdint.h>

enum {
    WINUSB_VENDOR_ID = 0x0483,
    WINUSB_PRODUCT_ID = 0x5741,
    WINUSB_INTERFACE_NUMBER = 0,
    WINUSB_BULK_IN_ENDPOINT = 0x81,
    WINUSB_BULK_OUT_ENDPOINT = 0x02,
    WINUSB_MS_VENDOR_CODE = 0x20,
};

uint8_t* winusb_device_descriptor(uint16_t* length);
uint8_t* winusb_config_descriptor(uint16_t* length);
uint8_t* winusb_qualifier_descriptor(uint16_t* length);
uint8_t* winusb_os_string_descriptor(uint16_t* length);
uint8_t* winusb_compat_id_descriptor(uint16_t* length);
bool winusb_is_compat_id_request(uint8_t request, uint16_t index);
