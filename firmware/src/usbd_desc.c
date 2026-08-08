#include "usbd_desc.h"
#include "usbd_conf.h"
#include <string.h>
#include <stdio.h>

static uint8_t dev_desc[USB_LEN_DEV_DESC];
static uint8_t str_desc[USBD_MAX_STR_DESC_SIZ];

static void fill_serial(void)
{
    uint32_t s1 = *(volatile uint32_t *)0x1FFF7590u;   /* UID[0] on STM32L4x2 */
    uint32_t s2 = *(volatile uint32_t *)0x1FFF7594u;
    uint32_t s3 = *(volatile uint32_t *)0x1FFF7598u;
    char buf[25];
    snprintf(buf, sizeof(buf), "%08lX%08lX%08lX", (unsigned long)s1, (unsigned long)s2, (unsigned long)s3);
    uint8_t* p = str_desc + 2; uint8_t n = 0;
    for (const char* c = buf; *c && n < USBD_MAX_STR_DESC_SIZ - 3; c++) { p[n++] = (uint8_t)*c; p[n++] = 0; }
    str_desc[0] = (uint8_t)(n + 2); str_desc[1] = USB_DESC_TYPE_STRING;
}

static uint8_t* dev_desc_ptr(USBD_SpeedTypeDef speed, uint16_t* len)
{
    (void)speed;
    memset(dev_desc, 0, USB_LEN_DEV_DESC);
    dev_desc[0] = USB_LEN_DEV_DESC; dev_desc[1] = USB_DESC_TYPE_DEVICE;
    dev_desc[2] = 0x00; dev_desc[3] = 0x02;
    dev_desc[4] = 0xEF; dev_desc[5] = 0x02; dev_desc[6] = 0x01;   /* composite */
    dev_desc[7] = USB_MAX_EP0_SIZE;
    dev_desc[8] = (uint8_t)USBD_VID; dev_desc[9] = (uint8_t)(USBD_VID >> 8);
    dev_desc[10] = (uint8_t)USBD_PID; dev_desc[11] = (uint8_t)(USBD_PID >> 8);
    dev_desc[12] = 0x00; dev_desc[13] = 0x02;
    dev_desc[14] = 0x01; dev_desc[15] = 0x02; dev_desc[16] = 0x03; dev_desc[17] = 0x01;
    *len = USB_LEN_DEV_DESC;
    return dev_desc;
}

static uint8_t* langid_ptr(USBD_SpeedTypeDef speed, uint16_t* len)
{
    (void)speed;
    str_desc[0] = 4; str_desc[1] = USB_DESC_TYPE_STRING;
    str_desc[2] = (uint8_t)USBD_LANGID_STRING; str_desc[3] = (uint8_t)(USBD_LANGID_STRING >> 8);
    *len = 4; return str_desc;
}

static uint8_t* str_ptr(USBD_SpeedTypeDef speed, uint16_t* len, const char* s)
{
    (void)speed;
    uint8_t n = 0; uint8_t* p = str_desc + 2;
    for (const char* c = s; *c && n < USBD_MAX_STR_DESC_SIZ - 3; c++) { p[n++] = (uint8_t)*c; p[n++] = 0; }
    str_desc[0] = (uint8_t)(n + 2); str_desc[1] = USB_DESC_TYPE_STRING;
    *len = (uint16_t)(n + 2); return str_desc;
}

static uint8_t* mfr_ptr(USBD_SpeedTypeDef speed, uint16_t* len) { return str_ptr(speed, len, USBD_MANUFACTURER_STRING); }
static uint8_t* prod_ptr(USBD_SpeedTypeDef speed, uint16_t* len) { return str_ptr(speed, len, USBD_PRODUCT_STRING); }
static uint8_t* ser_ptr(USBD_SpeedTypeDef speed, uint16_t* len) { (void)speed; fill_serial(); *len = str_desc[0]; return str_desc; }
static uint8_t* cfg_ptr(USBD_SpeedTypeDef speed, uint16_t* len) { return str_ptr(speed, len, USBD_CONFIGURATION_STRING); }
static uint8_t* intf_ptr(USBD_SpeedTypeDef speed, uint16_t* len) { return str_ptr(speed, len, USBD_INTERFACE_STRING); }

USBD_DescriptorsTypeDef FS_Desc = {
    dev_desc_ptr, langid_ptr, mfr_ptr, prod_ptr, ser_ptr, cfg_ptr, intf_ptr
};
