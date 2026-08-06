#include "usbd_cdc.h"
#include "usbd_composite_builder.h"
#include "usbproto.h"
#include "hw_usb.h"
#include <string.h>

extern USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t cdc_rx_buf[256];
static usbproto_parser_t proto;

void usbd_cdc_if_init_parser(void)
{
    usbproto_init(&proto);
}

static int8_t cdc_Init(void)
{
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, cdc_rx_buf);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return 0;
}

static int8_t cdc_DeInit(void) { return 0; }
static int8_t cdc_Control(uint8_t cmd, uint8_t *pbuf, uint16_t length) { (void)cmd; (void)pbuf; (void)length; return 0; }
static int8_t cdc_Receive(uint8_t *pbuf, uint32_t *len)
{
    uint32_t i;
    for (i = 0; i < *len; i++)
    {
        usbproto_msg_t msg;
        if (usbproto_feed(&proto, pbuf[i], &msg) == USBPROTO_OK)
            hw_usb_handle_command(&msg);
    }
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, pbuf);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return 0;
}

static int8_t cdc_TransmitCplt(uint8_t *pbuf, uint32_t *len, uint8_t epnum) { (void)pbuf; (void)len; (void)epnum; return 0; }

static USBD_CDC_ItfTypeDef cdc_if =
{
    cdc_Init, cdc_DeInit, cdc_Control, cdc_Receive, cdc_TransmitCplt
};

void usbd_cdc_if_register(USBD_HandleTypeDef *pdev)
{
    USBD_CDC_RegisterInterface(pdev, &cdc_if);
}
