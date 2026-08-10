#include "WinUsbDevice.h"
#include "WinUsbDescriptors.h"

#include "stm32l4xx_hal.h"

extern "C" {
#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"
}

#if defined(USBCON) && defined(USB)

static const uint8_t kBulkInEndpoint = WINUSB_BULK_IN_ENDPOINT;
static const uint8_t kBulkOutEndpoint = WINUSB_BULK_OUT_ENDPOINT;
static const uint16_t kBulkPacketSize = 64u;
static const uint16_t kBulkInPmaAddress = 352u;
static const uint16_t kBulkOutPmaAddress = 480u;

static USBD_HandleTypeDef usb_device;
static volatile bool tx_busy;
static volatile bool ack_received;
static uint8_t ack_buffer[kBulkPacketSize];
static uint8_t interface_alt;
static uint16_t status_word;

extern "C" PCD_HandleTypeDef g_hpcd;
extern "C" uint8_t* USBD_SerialStrDescriptor(
    USBD_SpeedTypeDef speed, uint16_t* length);

static uint8_t string_descriptor[64];
static uint8_t lang_descriptor[] = {0x04, 0x03, 0x09, 0x04};

static uint8_t* fixed_string(const char* text, uint16_t* length)
{
    USBD_GetString((uint8_t*)text, string_descriptor, length);
    return string_descriptor;
}

static uint8_t* get_device(USBD_SpeedTypeDef, uint16_t* length)
{
    return winusb_device_descriptor(length);
}

static uint8_t* get_lang(USBD_SpeedTypeDef, uint16_t* length)
{
    *length = sizeof(lang_descriptor);
    return lang_descriptor;
}

static uint8_t* get_manufacturer(USBD_SpeedTypeDef, uint16_t* length)
{
    return fixed_string("DayVault", length);
}

static uint8_t* get_product(USBD_SpeedTypeDef, uint16_t* length)
{
    return fixed_string("DayVault Bulk Export", length);
}

static uint8_t* get_serial(USBD_SpeedTypeDef speed, uint16_t* length)
{
    return USBD_SerialStrDescriptor(speed, length);
}

static uint8_t* get_configuration_string(USBD_SpeedTypeDef, uint16_t* length)
{
    return fixed_string("DayVault Bulk", length);
}

static uint8_t* get_interface_string(USBD_SpeedTypeDef, uint16_t* length)
{
    return fixed_string("DayVault WinUSB", length);
}

static uint8_t* get_user_string(
    USBD_SpeedTypeDef, uint8_t index, uint16_t* length)
{
    if (index == 0xEEu) return winusb_os_string_descriptor(length);
    *length = 0;
    return nullptr;
}

static USBD_DescriptorsTypeDef descriptors = {
    get_device,
    get_lang,
    get_manufacturer,
    get_product,
    get_serial,
    get_configuration_string,
    get_interface_string,
    get_user_string,
};

static uint8_t class_init(USBD_HandleTypeDef* device, uint8_t)
{
    if (USBD_LL_OpenEP(device, kBulkInEndpoint, USBD_EP_TYPE_BULK,
                       kBulkPacketSize) != USBD_OK) {
        return USBD_FAIL;
    }
    device->ep_in[kBulkInEndpoint & 0x0Fu].is_used = 1u;
    if (USBD_LL_OpenEP(device, kBulkOutEndpoint, USBD_EP_TYPE_BULK,
                       kBulkPacketSize) != USBD_OK) {
        USBD_LL_CloseEP(device, kBulkInEndpoint);
        device->ep_in[kBulkInEndpoint & 0x0Fu].is_used = 0u;
        return USBD_FAIL;
    }
    device->ep_out[kBulkOutEndpoint & 0x0Fu].is_used = 1u;
    tx_busy = false;
    ack_received = false;
    interface_alt = 0;
    USBD_LL_PrepareReceive(device, kBulkOutEndpoint, ack_buffer,
                           sizeof(ack_buffer));
    return USBD_OK;
}

static uint8_t class_deinit(USBD_HandleTypeDef* device, uint8_t)
{
    USBD_LL_CloseEP(device, kBulkInEndpoint);
    USBD_LL_CloseEP(device, kBulkOutEndpoint);
    device->ep_in[kBulkInEndpoint & 0x0Fu].is_used = 0u;
    device->ep_out[kBulkOutEndpoint & 0x0Fu].is_used = 0u;
    tx_busy = false;
    ack_received = false;
    return USBD_OK;
}

static uint8_t class_setup(
    USBD_HandleTypeDef* device, USBD_SetupReqTypedef* request)
{
    if ((request->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_VENDOR) {
        if ((request->bmRequest & 0x80u) != 0u &&
            winusb_is_compat_id_request(request->bRequest, request->wIndex)) {
            uint16_t length = 0;
            uint8_t* data = winusb_compat_id_descriptor(&length);
            if (request->wLength < length) length = request->wLength;
            return (uint8_t)USBD_CtlSendData(device, data, length);
        }
        USBD_CtlError(device, request);
        return USBD_FAIL;
    }

    if ((request->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_STANDARD) {
        switch (request->bRequest) {
        case USB_REQ_GET_STATUS:
            return (uint8_t)USBD_CtlSendData(
                device, (uint8_t*)&status_word, sizeof(status_word));
        case USB_REQ_GET_INTERFACE:
            return (uint8_t)USBD_CtlSendData(device, &interface_alt, 1u);
        case USB_REQ_SET_INTERFACE:
            interface_alt = (uint8_t)request->wValue;
            return USBD_OK;
        case USB_REQ_CLEAR_FEATURE:
            USBD_LL_FlushEP(device, (uint8_t)request->wIndex);
            return USBD_OK;
        default:
            break;
        }
    }

    USBD_CtlError(device, request);
    return USBD_FAIL;
}

static uint8_t class_data_in(USBD_HandleTypeDef*, uint8_t endpoint)
{
    if ((endpoint & 0x0Fu) == (kBulkInEndpoint & 0x0Fu)) tx_busy = false;
    return USBD_OK;
}

static uint8_t class_data_out(USBD_HandleTypeDef* device, uint8_t endpoint)
{
    if ((endpoint & 0x0Fu) == (kBulkOutEndpoint & 0x0Fu) &&
        USBD_LL_GetRxDataSize(device, endpoint) != 0u) {
        ack_received = true;
    }
    return USBD_OK;
}

static uint8_t* get_config(uint16_t* length)
{
    return winusb_config_descriptor(length);
}

static uint8_t* get_qualifier(uint16_t* length)
{
    return winusb_qualifier_descriptor(length);
}

static USBD_ClassTypeDef winusb_class = {
    class_init,
    class_deinit,
    class_setup,
    nullptr,
    nullptr,
    class_data_in,
    class_data_out,
    nullptr,
    nullptr,
    nullptr,
    get_config,
    get_config,
    get_config,
    get_qualifier,
};

bool winusb_start(void)
{
    tx_busy = false;
    if (USBD_Init(&usb_device, &descriptors, 0u) != USBD_OK) return false;

    // The STM32L4 HAL double-buffer path corrupts data when consecutive large
    // transfers reuse the endpoint. Single-buffer bulk IN is deterministic and
    // still keeps the USB pipe full while the ISR refills each ACKed packet.
    if (HAL_PCDEx_PMAConfig(&g_hpcd, kBulkInEndpoint, PCD_SNG_BUF,
                            kBulkInPmaAddress) != HAL_OK) {
        USBD_DeInit(&usb_device);
        return false;
    }
    if (HAL_PCDEx_PMAConfig(&g_hpcd, kBulkOutEndpoint, PCD_SNG_BUF,
                            kBulkOutPmaAddress) != HAL_OK) {
        USBD_DeInit(&usb_device);
        return false;
    }
    if (USBD_RegisterClass(&usb_device, &winusb_class) != USBD_OK) {
        USBD_DeInit(&usb_device);
        return false;
    }
    if (USBD_Start(&usb_device) != USBD_OK) {
        USBD_DeInit(&usb_device);
        return false;
    }
    return true;
}

void winusb_stop(void)
{
    USBD_Stop(&usb_device);
    USBD_DeInit(&usb_device);
    tx_busy = false;
    ack_received = false;
}

bool winusb_is_configured(void)
{
    return usb_device.dev_state == USBD_STATE_CONFIGURED;
}

bool winusb_tx_busy(void)
{
    return tx_busy;
}

bool winusb_ack_received(void)
{
    return ack_received;
}

bool winusb_send(const uint8_t* data, size_t length)
{
    if (!winusb_is_configured() || tx_busy || length > 0xFFFFFFFFu) return false;
    tx_busy = true;
    usb_device.ep_in[kBulkInEndpoint & 0x0Fu].total_length = (uint32_t)length;
    if (USBD_LL_Transmit(&usb_device, kBulkInEndpoint,
                         (uint8_t*)data, (uint32_t)length) != USBD_OK) {
        tx_busy = false;
        return false;
    }
    return true;
}

#else

bool winusb_start(void) { return false; }
void winusb_stop(void) {}
bool winusb_is_configured(void) { return false; }
bool winusb_tx_busy(void) { return false; }
bool winusb_send(const uint8_t*, size_t) { return false; }
bool winusb_ack_received(void) { return false; }

#endif
