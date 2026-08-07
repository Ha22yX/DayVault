#include "stm32l4xx_hal.h"
#include "usbd_def.h"
#include "usbd_core.h"
#include "usbd_cdc.h"
#include "usbd_desc.h"
#include "usbd_cdc_if.h"
#include "hw_usb.h"
#include "dayvault_config.h"
#include <string.h>

static PCD_HandleTypeDef hpcd;
USBD_HandleTypeDef hUsbDevice;

static rx_line_cb rx_cb = 0;
static char line_buf[USB_CDC_RX_LINE_MAX];
static size_t line_len = 0;

#define USBD_MEM_POOL_SIZE 1024U
static uint32_t usbd_mem_pool[USBD_MEM_POOL_SIZE / 4U];
static uint32_t usbd_mem_offset = 0U;

void *USBD_static_malloc(uint32_t size)
{
    uint32_t aligned = (size + 3U) & ~3U;
    void *p = NULL;
    if (usbd_mem_offset + aligned <= USBD_MEM_POOL_SIZE)
    {
        p = (void *)&usbd_mem_pool[usbd_mem_offset / 4U];
        usbd_mem_offset += aligned;
    }
    return p;
}

void USBD_static_free(void *p)
{
    (void)p;
}

void hw_usb_set_rx_line_callback(rx_line_cb cb)
{
    rx_cb = cb;
}

void hw_usb_init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_USB_CLK_ENABLE();

    /* PA11 = USB_DM, PA12 = USB_DP */
    g.Pin = PIN_USB_DM | PIN_USB_DP;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF10_USB_FS;
    HAL_GPIO_Init(GPIOA, &g);

    HAL_PWREx_EnableVddUSB();

    USBD_Init(&hUsbDevice, &DayVault_Desc, 0);
    USBD_RegisterClass(&hUsbDevice, &USBD_CDC);
    USBD_CDC_RegisterInterface(&hUsbDevice, &usbd_cdc_if_fops);
    USBD_Start(&hUsbDevice);
}

void hw_usb_poll(void)
{
    /* PCD IRQ is handled by HAL_PCD_IRQHandler from the USB_IRQHandler;
       this poll is reserved for non-ISR processing if needed. */
}

void cdc_rx_bytes(const uint8_t *data, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
    {
        if (line_len < sizeof(line_buf))
            line_buf[line_len++] = (char)data[i];
        if (data[i] == '\n')
        {
            if (rx_cb)
                rx_cb(line_buf, line_len);
            line_len = 0;
            memset(line_buf, 0, sizeof(line_buf));
        }
    }
}

void USB_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd);
}

void HAL_PCD_MspInit(PCD_HandleTypeDef *hpcd)
{
    if (hpcd->Instance == USB)
    {
        HAL_NVIC_SetPriority(USB_IRQn, 1, 0);
        HAL_NVIC_EnableIRQ(USB_IRQn);
    }
}

/* HAL PCD callbacks -> USBD core (USBD_LL_* stage functions live in usbd_core.c) */
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_SetupStage((USBD_HandleTypeDef *)hpcd->pData, (uint8_t *)hpcd->Setup);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_DataOutStage((USBD_HandleTypeDef *)hpcd->pData, epnum,
                         hpcd->OUT_ep[epnum].xfer_buff);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_DataInStage((USBD_HandleTypeDef *)hpcd->pData, epnum,
                        hpcd->IN_ep[epnum].xfer_buff);
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_Reset((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_SOF((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_Suspend((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_Resume((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef *)hpcd->pData, epnum);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_IsoINIncomplete((USBD_HandleTypeDef *)hpcd->pData, epnum);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_DevConnected((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_DevDisconnected((USBD_HandleTypeDef *)hpcd->pData);
}

/* USBD low-level driver -> HAL PCD */
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
    hpcd.pData = pdev;
    hpcd.Instance = USB;
    hpcd.Init.dev_endpoints = 8;
    hpcd.Init.speed = PCD_SPEED_FULL;
    hpcd.Init.phy_itface = PCD_PHY_EMBEDDED;
    hpcd.Init.ep0_mps = USB_MAX_EP0_SIZE;
    hpcd.Init.low_power_enable = DISABLE;
    hpcd.Init.lpm_enable = DISABLE;
    hpcd.Init.battery_charging_enable = DISABLE;
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
    uint8_t epnum = ep_addr & 0x7FU;
    (void)pdev;
    if ((ep_addr & 0x80U) != 0U)
        return (uint8_t)PCD_GET_EP_TX_STALL_STATUS(USB, epnum);
    return (uint8_t)PCD_GET_EP_RX_STALL_STATUS(USB, epnum);
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

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                          uint8_t *pbuf, uint32_t size)
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
