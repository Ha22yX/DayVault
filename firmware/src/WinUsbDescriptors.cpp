#include "WinUsbDescriptors.h"

static uint8_t device_descriptor[] = {
    0x12, 0x01,             // bLength, DEVICE descriptor
    0x00, 0x02,             // USB 2.00
    0x00, 0x00, 0x00,       // class is declared by the interface
    0x40,                   // EP0 max packet size
    0x83, 0x04,             // VID 0x0483
    0x41, 0x57,             // PID 0x5741
    0x00, 0x02,             // device release 2.00
    0x01, 0x02, 0x03,       // manufacturer, product, serial strings
    0x01,                   // one configuration
};

static uint8_t config_descriptor[] = {
    0x09, 0x02,             // configuration descriptor
    0x20, 0x00,             // total length: 32 bytes
    0x01, 0x01, 0x00,       // one interface, configuration 1
    0x80, 0x32,             // bus powered, 100 mA

    0x09, 0x04,             // interface descriptor
    WINUSB_INTERFACE_NUMBER,
    0x00, 0x02,             // alternate 0, two endpoints
    0xFF, 0x00, 0x00,       // vendor-specific interface
    0x00,

    0x07, 0x05,             // endpoint descriptor
    WINUSB_BULK_IN_ENDPOINT,
    0x02,                   // bulk
    0x40, 0x00,             // 64-byte Full Speed packet
    0x00,

    0x07, 0x05,             // endpoint descriptor
    WINUSB_BULK_OUT_ENDPOINT,
    0x02,                   // bulk
    0x40, 0x00,             // 64-byte Full Speed packet
    0x00,
};

static uint8_t qualifier_descriptor[] = {
    0x0A, 0x06, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x40,
    0x01, 0x00,
};

// Microsoft OS 1.0 string descriptor at index 0xEE: "MSFT100".
static uint8_t os_string_descriptor[] = {
    0x12, 0x03,
    'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0,
    WINUSB_MS_VENDOR_CODE, 0x00,
};

// Microsoft Extended Compat ID descriptor for interface 0.
static uint8_t compat_id_descriptor[] = {
    0x28, 0x00, 0x00, 0x00, // dwLength
    0x00, 0x01,             // bcdVersion 1.00
    0x04, 0x00,             // wIndex EXTENDED_COMPAT_ID
    0x01,                   // one function section
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    WINUSB_INTERFACE_NUMBER,
    0x01,
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static uint8_t* descriptor(uint8_t* data, size_t size, uint16_t* length)
{
    *length = (uint16_t)size;
    return data;
}

uint8_t* winusb_device_descriptor(uint16_t* length)
{
    return descriptor(device_descriptor, sizeof(device_descriptor), length);
}

uint8_t* winusb_config_descriptor(uint16_t* length)
{
    return descriptor(config_descriptor, sizeof(config_descriptor), length);
}

uint8_t* winusb_qualifier_descriptor(uint16_t* length)
{
    return descriptor(qualifier_descriptor, sizeof(qualifier_descriptor), length);
}

uint8_t* winusb_os_string_descriptor(uint16_t* length)
{
    return descriptor(os_string_descriptor, sizeof(os_string_descriptor), length);
}

uint8_t* winusb_compat_id_descriptor(uint16_t* length)
{
    return descriptor(compat_id_descriptor, sizeof(compat_id_descriptor), length);
}

bool winusb_is_compat_id_request(uint8_t request, uint16_t index)
{
    return request == WINUSB_MS_VENDOR_CODE && index == 0x0004u;
}
