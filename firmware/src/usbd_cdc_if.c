#include "usbd_cdc_if.h"
#include "usbd_cdc.h"
#include "usbd_def.h"
#include "hw_usb.h"

extern USBD_HandleTypeDef hUsbDevice;

static uint8_t cdc_rx_buf[CDC_DATA_FS_OUT_PACKET_SIZE];

static int8_t CDC_Init(void)
{
    USBD_CDC_SetRxBuffer(&hUsbDevice, cdc_rx_buf);
    return 0;
}

static int8_t CDC_DeInit(void)
{
    return 0;
}

static int8_t CDC_Control(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    (void)cmd; (void)pbuf; (void)length;
    return 0;
}

static int8_t CDC_Receive(uint8_t *pbuf, uint32_t *Len)
{
    cdc_rx_bytes(pbuf, *Len);
    USBD_CDC_ReceivePacket(&hUsbDevice);
    return 0;
}

static int8_t CDC_TransmitCplt(uint8_t *pbuf, uint32_t *Len, uint8_t epnum)
{
    (void)pbuf; (void)Len; (void)epnum;
    return 0;
}

static USBD_CDC_ItfTypeDef cdc_if =
{
    CDC_Init,
    CDC_DeInit,
    CDC_Control,
    CDC_Receive,
    CDC_TransmitCplt
};

void usbd_cdc_if_register(USBD_HandleTypeDef *pdev)
{
    USBD_CDC_RegisterInterface(pdev, &cdc_if);
}
