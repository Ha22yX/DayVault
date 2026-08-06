#include "stm32l4xx_hal.h"
#include "usbd_core.h"
#include "usbd_composite_builder.h"
#include "usbd_cdc.h"
#include "usbd_msc.h"
#include "hw_usb.h"
#include "hw_rtc.h"
#include "hw_adc.h"
#include "usbd_desc.h"
#include "usbd_cdc_if.h"
#include "usbd_msc_storage.h"
#include <stdio.h>
#include <string.h>

static PCD_HandleTypeDef hpcd;
USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t tx_buf[128];

/* Endpoint address arrays consumed by the composite builder. Order matters:
   slot 0 must be the IN bulk endpoint, slot 1 the OUT bulk endpoint,
   slot 2 (CDC only) the IN interrupt (command) endpoint. */
static uint8_t cdc_ep_addr[3] = { 0x81U, 0x01U, 0x82U };
static uint8_t msc_ep_addr[2] = { 0x83U, 0x03U };

static void usb_msp_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USB_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_EnableVddUSB();

    g.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF10_USB_FS;
    HAL_GPIO_Init(GPIOA, &g);
}

void HAL_PCD_MspInit(PCD_HandleTypeDef *hpcd_instance)
{
    (void)hpcd_instance;
    usb_msp_init();
}

/* ------------------------------------------------------------------ */
/* Low-level USBD <-> PCD glue (normally lives in usbd_conf.c)        */
/* ------------------------------------------------------------------ */

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
    hpcd.pData = pdev;
    HAL_PCD_Init(&hpcd);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
    (void)pdev;
    HAL_PCD_DeInit(&hpcd);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
    (void)pdev;
    HAL_PCD_Start(&hpcd);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
    (void)pdev;
    HAL_PCD_Stop(&hpcd);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                  uint8_t ep_type, uint16_t ep_mps)
{
    (void)pdev;
    HAL_PCD_EP_Open(&hpcd, ep_addr, ep_mps, ep_type);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    (void)pdev;
    HAL_PCD_EP_Close(&hpcd, ep_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    (void)pdev;
    HAL_PCD_EP_Flush(&hpcd, ep_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    (void)pdev;
    HAL_PCD_EP_SetStall(&hpcd, ep_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    (void)pdev;
    HAL_PCD_EP_ClrStall(&hpcd, ep_addr);
    return USBD_OK;
}

uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    (void)pdev;
    if ((ep_addr & 0x80U) != 0U)
    {
        return PCD_GET_EP_TX_STALL_STATUS(hpcd.Instance, (ep_addr & 0x7FU));
    }
    else
    {
        return PCD_GET_EP_RX_STALL_STATUS(hpcd.Instance, ep_addr);
    }
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t dev_addr)
{
    (void)pdev;
    HAL_PCD_SetAddress(&hpcd, dev_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                    uint8_t *pbuf, uint32_t size)
{
    (void)pdev;
    HAL_PCD_EP_Transmit(&hpcd, ep_addr, pbuf, size);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev,
                                          uint8_t ep_addr, uint8_t *pbuf,
                                          uint32_t size)
{
    (void)pdev;
    HAL_PCD_EP_Receive(&hpcd, ep_addr, pbuf, size);
    return USBD_OK;
}

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    (void)pdev;
    return HAL_PCD_EP_GetRxCount(&hpcd, ep_addr);
}

void USBD_LL_Delay(uint32_t delay)
{
    HAL_Delay(delay);
}

void *USBD_static_malloc(uint32_t size)
{
    /* Non-freeing bump allocator. Must hold both class handles simultaneously:
       USBD_CDC_HandleTypeDef ~552 B (data[512]) + USBD_MSC_BOT_HandleTypeDef
       ~664 B (bot_data[512]) = ~1216 B. Pool of 1408 B leaves ~192 B headroom.
       uint32_t base keeps every allocation 4-byte aligned. */
    static uint32_t mem[352];   /* 1408 bytes */
    static uint32_t used = 0U;
    uint32_t n = (size + 3U) & ~3U;
    if ((used + n) > sizeof(mem))
        return NULL;
    void *p = (void *)&mem[used / 4U];
    used += n;
    return p;
}

void USBD_static_free(void *p)
{
    (void)p;
}

/* ------------------------------------------------------------------ */

static void cdc_send_line(const char *line)
{
    hw_usb_cdc_send((const uint8_t *)line, (uint16_t)strlen(line));
}

void hw_usb_init(void)
{
    USBD_StatusTypeDef ret;

    hpcd.Instance = USB;
    hpcd.Init.dev_endpoints = 8;
    hpcd.Init.speed = PCD_SPEED_FULL;
    hpcd.Init.phy_itface = PCD_PHY_EMBEDDED;
    hpcd.Init.low_power_enable = DISABLE;
    hpcd.Init.lpm_enable = DISABLE;
    hpcd.Init.battery_charging_enable = DISABLE;

    ret = USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS);
    if (ret != USBD_OK)
        return;

    usbd_cdc_if_init_parser();

    USBD_RegisterClass(&hUsbDeviceFS, &USBD_CMPSIT);
    hUsbDeviceFS.tclasslist[0].EpAdd = cdc_ep_addr;
    USBD_CMPSIT_AddClass(&hUsbDeviceFS, &USBD_CDC, CLASS_TYPE_CDC, 0);
    usbd_cdc_if_register(&hUsbDeviceFS);
    hUsbDeviceFS.classId++;
    hUsbDeviceFS.NumClasses++;
    hUsbDeviceFS.tclasslist[1].EpAdd = msc_ep_addr;
    USBD_CMPSIT_AddClass(&hUsbDeviceFS, &USBD_MSC, CLASS_TYPE_MSC, 0);
    USBD_MSC_RegisterStorage(&hUsbDeviceFS, &USBD_STORAGE_fops);
    USBD_Start(&hUsbDeviceFS);
}

void hw_usb_deinit(void)
{
    USBD_Stop(&hUsbDeviceFS);
    USBD_DeInit(&hUsbDeviceFS);
}

void hw_usb_poll(void)
{
    /* handled via IRQ callbacks */
}

int hw_usb_cdc_send(const uint8_t *buf, uint16_t len)
{
    uint8_t cid;
    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
        return 0;
    if (len > sizeof(tx_buf))
        return 0;
    cid = (uint8_t)USBD_CMPSIT_GetClassID(&hUsbDeviceFS, CLASS_TYPE_CDC, 0);
    memcpy(tx_buf, buf, len);
    if (USBD_CDC_SetTxBuffer(&hUsbDeviceFS, tx_buf, len, cid) != USBD_OK)
        return 0;
    return (int)USBD_CDC_TransmitPacket(&hUsbDeviceFS, cid);
}

void hw_usb_handle_command(const usbproto_msg_t *msg)
{
    switch (msg->cmd)
    {
    case USBPROTO_CMD_TIME:
    {
        char line[64];
        utc_time_t t;
        hw_rtc_get_time(&t);
        timeutil_format_iso(&t, line + 3, sizeof(line) - 3);
        memcpy(line, "OK ", 3);
        cdc_send_line(line);
        break;
    }
    case USBPROTO_CMD_SYNC:
    {
        /* msg->arg is unix seconds; convert to utc_time_t via RTC BCD path
           (a host tool sends SYNC; the device sets the calendar). */
        char line[64];
        utc_time_t t = {1970, 1, 1, 0, 0, 0};
        uint32_t days = msg->arg / 86400u;
        uint32_t rem = msg->arg % 86400u;
        /* NOTE: full civil-from-days conversion lives in app/timeutil;
           placeholder path keeps compile-only sanity. */
        (void)days; (void)rem;
        hw_rtc_set_time(&t);
        snprintf(line, sizeof(line), "OK old=%lu new=%lu", (unsigned long)0, (unsigned long)msg->arg);
        cdc_send_line(line);
        break;
    }
    case USBPROTO_CMD_STAT:
    {
        char line[96];
        uint16_t mv = hw_adc_read_battery_mv();
        snprintf(line, sizeof(line), "batt=%umV", mv);
        cdc_send_line(line);
        break;
    }
    case USBPROTO_CMD_FLUSH:
        cdc_send_line("OK flush");
        break;
    }
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpdev)
{
    USBD_LL_SetupStage(hpdev->pData, (uint8_t *)hpdev->Setup);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpdev, uint8_t epnum)
{
    USBD_LL_DataOutStage(hpdev->pData, epnum, hpdev->OUT_ep[epnum].xfer_buff);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpdev, uint8_t epnum)
{
    USBD_LL_DataInStage(hpdev->pData, epnum, hpdev->IN_ep[epnum].xfer_buff);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpdev)
{
    USBD_LL_SOF(hpdev->pData);
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpdev)
{
    USBD_LL_SetSpeed(hpdev->pData, USBD_SPEED_FULL);
    USBD_LL_Reset(hpdev->pData);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpdev)
{
    USBD_LL_Suspend(hpdev->pData);
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpdev)
{
    USBD_LL_Resume(hpdev->pData);
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpdev, uint8_t epnum)
{
    USBD_LL_IsoOUTIncomplete(hpdev->pData, epnum);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpdev, uint8_t epnum)
{
    USBD_LL_IsoINIncomplete(hpdev->pData, epnum);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpdev)
{
    USBD_LL_DevConnected(hpdev->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpdev)
{
    USBD_LL_DevDisconnected(hpdev->pData);
}

void USB_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd);
}
