#include "usbd_desc.h"
#include "usbd_def.h"
#include "usbd_core.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>

static uint8_t USBD_DeviceDesc[18] = {
    0x12,                        /* bLength */
    USB_DESC_TYPE_DEVICE,        /* bDescriptorType */
    0x00, 0x02,                  /* bcdUSB 2.00 */
    0x00,                        /* bDeviceClass: 0, class at interface level */
    0x00,                        /* bDeviceSubClass */
    0x00,                        /* bDeviceProtocol */
    USB_MAX_EP0_SIZE,            /* bMaxPacketSize0 */
    0x83, 0x04,                  /* idVendor (0x0483, ST) */
    0x11, 0x00,                  /* idProduct (0x0011) */
    0x00, 0x02,                  /* bcdDevice */
    1,                           /* iManufacturer */
    2,                           /* iProduct */
    3,                           /* iSerialNumber */
    0x01                         /* bNumConfigurations */
};

static uint8_t *USBD_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(USBD_DeviceDesc);
    return USBD_DeviceDesc;
}

static uint8_t *USBD_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    static uint8_t langid[4] = { 4, USB_DESC_TYPE_STRING, 0x09, 0x04 };
    (void)speed;
    *length = sizeof(langid);
    return langid;
}

static uint8_t *USBD_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    static uint8_t str[USB_SIZ_STRING];
    (void)speed;
    USBD_GetString((uint8_t *)"DayVault", str, length);
    return str;
}

static uint8_t *USBD_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    static uint8_t str[USB_SIZ_STRING];
    (void)speed;
    USBD_GetString((uint8_t *)"DayVault Recorder", str, length);
    return str;
}

static uint8_t *USBD_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    static uint8_t str[USB_SIZ_STRING];
    uint8_t hex[25];
    (void)speed;
    (void)snprintf((char *)hex, sizeof(hex), "%08lx%08lx%08lx",
                   (unsigned long)HAL_GetUIDw0(),
                   (unsigned long)HAL_GetUIDw1(),
                   (unsigned long)HAL_GetUIDw2());
    USBD_GetString(hex, str, length);
    return str;
}

static uint8_t *USBD_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed; (void)length;
    return 0;
}

static uint8_t *USBD_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed; (void)length;
    return 0;
}

USBD_DescriptorsTypeDef DayVault_Desc =
{
    USBD_DeviceDescriptor,
    USBD_LangIDStrDescriptor,
    USBD_ManufacturerStrDescriptor,
    USBD_ProductStrDescriptor,
    USBD_SerialStrDescriptor,
    USBD_ConfigStrDescriptor,
    USBD_InterfaceStrDescriptor
};
