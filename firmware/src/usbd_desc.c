#include "usbd_desc.h"
#include "usbd_composite_builder.h"
#include <string.h>
#include <stdio.h>

static uint8_t USBD_FS_DeviceDesc[USB_LEN_DEV_DESC];
static uint8_t USBD_FS_StrDesc[USBD_MAX_STR_DESC_SIZ];

static void Get_SerialNum(void)
{
    uint32_t s1 = *(volatile uint32_t *)DEVICE_ID1;
    uint32_t s2 = *(volatile uint32_t *)DEVICE_ID2;
    uint32_t s3 = *(volatile uint32_t *)DEVICE_ID3;
    char buf[25];
    snprintf(buf, sizeof(buf), "%08lX%08lX%08lX",
             (unsigned long)s1, (unsigned long)s2, (unsigned long)s3);
    uint8_t *p = USBD_FS_StrDesc + 2;
    uint8_t len = 0;
    const char *c = buf;
    while (*c && len < USBD_MAX_STR_DESC_SIZ - 3)
    {
        p[len++] = (uint8_t)*c;
        p[len++] = 0;
        c++;
    }
    USBD_FS_StrDesc[0] = (uint8_t)(len + 2);
    USBD_FS_StrDesc[1] = USB_DESC_TYPE_STRING;
}

static uint8_t *USBD_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    memset(USBD_FS_DeviceDesc, 0, USB_LEN_DEV_DESC);
    USBD_FS_DeviceDesc[0] = USB_LEN_DEV_DESC;
    USBD_FS_DeviceDesc[1] = USB_DESC_TYPE_DEVICE;
    USBD_FS_DeviceDesc[2] = 0x00; USBD_FS_DeviceDesc[3] = 0x02;
    USBD_FS_DeviceDesc[4] = 0xEF; /* composite */
    USBD_FS_DeviceDesc[5] = 0x02;
    USBD_FS_DeviceDesc[6] = 0x01;
    USBD_FS_DeviceDesc[7] = USB_MAX_EP0_SIZE;
    USBD_FS_DeviceDesc[8] = (uint8_t)USBD_VID;
    USBD_FS_DeviceDesc[9] = (uint8_t)(USBD_VID >> 8);
    USBD_FS_DeviceDesc[10] = (uint8_t)USBD_PID;
    USBD_FS_DeviceDesc[11] = (uint8_t)(USBD_PID >> 8);
    USBD_FS_DeviceDesc[12] = 0x00;
    USBD_FS_DeviceDesc[13] = 0x02;
    USBD_FS_DeviceDesc[14] = 0x01;   /* iManufacturer: index 1 */
    USBD_FS_DeviceDesc[15] = 0x02;   /* iProduct: index 2 */
    USBD_FS_DeviceDesc[16] = 0x03;   /* iSerialNumber: index 3 */
    USBD_FS_DeviceDesc[17] = 0x01;   /* bNumConfigurations */
    *length = USB_LEN_DEV_DESC;
    return USBD_FS_DeviceDesc;
}

static uint8_t *USBD_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_FS_StrDesc[0] = 4;
    USBD_FS_StrDesc[1] = USB_DESC_TYPE_STRING;
    USBD_FS_StrDesc[2] = (uint8_t)USBD_LANGID_STRING;
    USBD_FS_StrDesc[3] = (uint8_t)(USBD_LANGID_STRING >> 8);
    *length = 4;
    return USBD_FS_StrDesc;
}

static uint8_t *USBD_FS_StrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length, const char *str)
{
    (void)speed;
    uint8_t len = 0;
    uint8_t *p = USBD_FS_StrDesc + 2;
    while (*str && len < USBD_MAX_STR_DESC_SIZ - 3)
    {
        p[len++] = (uint8_t)*str;
        p[len++] = 0;
        str++;
    }
    USBD_FS_StrDesc[0] = (uint8_t)(len + 2);
    USBD_FS_StrDesc[1] = USB_DESC_TYPE_STRING;
    *length = len + 2;
    return USBD_FS_StrDesc;
}

static uint8_t *USBD_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    return USBD_FS_StrDescriptor(speed, length, USBD_MANUFACTURER_STRING);
}

static uint8_t *USBD_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    return USBD_FS_StrDescriptor(speed, length, USBD_PRODUCT_STRING);
}

static uint8_t *USBD_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    Get_SerialNum();
    *length = USBD_FS_StrDesc[0];
    return USBD_FS_StrDesc;
}

static uint8_t *USBD_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    return USBD_FS_StrDescriptor(speed, length, USBD_CONFIGURATION_STRING);
}

static uint8_t *USBD_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    return USBD_FS_StrDescriptor(speed, length, USBD_INTERFACE_STRING);
}

USBD_DescriptorsTypeDef FS_Desc =
{
    USBD_FS_DeviceDescriptor,
    USBD_FS_LangIDStrDescriptor,
    USBD_FS_ManufacturerStrDescriptor,
    USBD_FS_ProductStrDescriptor,
    USBD_FS_SerialStrDescriptor,
    USBD_FS_ConfigStrDescriptor,
    USBD_FS_InterfaceStrDescriptor
};
