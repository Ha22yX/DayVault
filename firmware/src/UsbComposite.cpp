#include "UsbComposite.h"
#include "usbd_conf.h"
#include "usbd_core.h"
#include "usbd_cdc.h"
#include "usbd_desc.h"
#include "Config.h"
#include "stm32l4xx_hal.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

USBD_HandleTypeDef hUsbDevice;
static PCD_HandleTypeDef hpcd;

#define USBD_MEM_POOL_SIZE 896u
static uint32_t usbd_mem_pool[USBD_MEM_POOL_SIZE / 4u];
static uint32_t usbd_mem_offset = 0u;

void* usbd_static_malloc(uint32_t size) {
    uint32_t aligned = (size + 3u) & ~3u;
    void* p = NULL;
    if (usbd_mem_offset + aligned <= USBD_MEM_POOL_SIZE) {
        p = (void *)&usbd_mem_pool[usbd_mem_offset / 4u];
        usbd_mem_offset += aligned;
    }
    return p;
}
void usbd_static_free(void* p) { if (p != NULL) usbd_mem_offset = 0u; }

/* CDC RX line framing + TX handled in the CDC fops below; no static TX buffer needed */

/* ---- CDC RX line framing (complete) ---- */
static void (*cdc_line_cb)(const char* line, size_t len) = NULL;
static char cdc_line[CDC_RX_LINE_MAX];
static size_t cdc_line_len = 0;

void usb_composite_set_line_cb(void (*cb)(const char*, size_t)) { cdc_line_cb = cb; }

static int8_t cdc_init(void)
{
    /* This middleware revision requires hcdc->RxBuffer to be non-NULL for
       USBD_CDC_Init to succeed; wire the static RX buffer here (called from
       USBD_CDC_Init after the class data handle is allocated). */
    static uint8_t cdc_rx_buf[CDC_DATA_FS_OUT_PACKET_SIZE];
    USBD_CDC_SetRxBuffer(&hUsbDevice, cdc_rx_buf);
    return 0;
}
static int8_t cdc_deinit(void) { return 0; }
static int8_t cdc_control(uint8_t cmd, uint8_t* pbuf, uint16_t len) { (void)cmd; (void)pbuf; (void)len; return 0; }
static int8_t cdc_receive(uint8_t* pbuf, uint32_t* len)
{
    for (uint32_t i = 0; i < *len; i++) {
        if (cdc_line_len < sizeof(cdc_line)) cdc_line[cdc_line_len++] = (char)pbuf[i];
        if (pbuf[i] == '\n') {
            if (cdc_line_cb) cdc_line_cb(cdc_line, cdc_line_len);
            cdc_line_len = 0;
        }
    }
    USBD_CDC_ReceivePacket(&hUsbDevice);   /* re-arm */
    return 0;
}
static int8_t cdc_transmit_cplt(uint8_t* pbuf, uint32_t* len, uint8_t epnum)
{
    (void)pbuf; (void)len; (void)epnum;
    return 0;   /* TX is driven by cdc_printf only */
}
static USBD_CDC_ItfTypeDef cdc_fops = { cdc_init, cdc_deinit, cdc_control, cdc_receive, cdc_transmit_cplt };

void cdc_printf(const char* fmt, ...)
{
    static char tmp[128];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    USBD_CDC_HandleTypeDef* h = (USBD_CDC_HandleTypeDef*)hUsbDevice.pClassData;
    if (h == NULL || h->TxState != 0u) return;   /* not ready or busy: drop */
    USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)tmp, (uint16_t)n);
    USBD_CDC_TransmitPacket(&hUsbDevice);
}

/* ---- USB device start (CDC only; MSC added in Task 10) ---- */

bool usb_composite_init(void)
{
    USBD_StatusTypeDef ret = USBD_Init(&hUsbDevice, &FS_Desc, DEVICE_FS);
    if (ret != USBD_OK) return false;
    USBD_RegisterClass(&hUsbDevice, &USBD_CDC);
    USBD_CDC_RegisterInterface(&hUsbDevice, &cdc_fops);
    USBD_Start(&hUsbDevice);
    USBD_CDC_ReceivePacket(&hUsbDevice);
    return true;
}

void usb_composite_deinit(void)
{
    USBD_Stop(&hUsbDevice);
    USBD_DeInit(&hUsbDevice);
}

void usb_composite_poll(void) { }

/* ---- PCD low-level: IRQ + USBD_LL_* ---- */
void USB_IRQHandler(void) { HAL_PCD_IRQHandler(&hpcd); }

void HAL_PCD_MspInit(PCD_HandleTypeDef* h) {
    if (h->Instance == USB) {
        __HAL_RCC_USB_CLK_ENABLE();
        HAL_NVIC_SetPriority(USB_IRQn, 1, 0);
        HAL_NVIC_EnableIRQ(USB_IRQn);
    }
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef* h) { USBD_LL_SetupStage((USBD_HandleTypeDef*)h->pData, (uint8_t*)h->Setup); }
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef* h, uint8_t ep) { USBD_LL_DataOutStage((USBD_HandleTypeDef*)h->pData, ep, h->OUT_ep[ep].xfer_buff); }
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef* h, uint8_t ep) { USBD_LL_DataInStage((USBD_HandleTypeDef*)h->pData, ep, h->IN_ep[ep].xfer_buff); }
void HAL_PCD_ResetCallback(PCD_HandleTypeDef* h) { USBD_LL_SetSpeed((USBD_HandleTypeDef*)h->pData, USBD_SPEED_FULL); USBD_LL_Reset((USBD_HandleTypeDef*)h->pData); }
void HAL_PCD_SOFCallback(PCD_HandleTypeDef* h) { USBD_LL_SOF((USBD_HandleTypeDef*)h->pData); }
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef* h) { USBD_LL_Suspend((USBD_HandleTypeDef*)h->pData); }
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef* h) { USBD_LL_Resume((USBD_HandleTypeDef*)h->pData); }
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef* h, uint8_t ep) { USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef*)h->pData, ep); }
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef* h, uint8_t ep) { USBD_LL_IsoINIncomplete((USBD_HandleTypeDef*)h->pData, ep); }
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef* h) { USBD_LL_DevConnected((USBD_HandleTypeDef*)h->pData); }
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef* h) { USBD_LL_DevDisconnected((USBD_HandleTypeDef*)h->pData); }

void USBD_LL_Delay(uint32_t d) { HAL_Delay(d); }
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef* pdev) {
    hpcd.pData = pdev; hpcd.Instance = USB;
    hpcd.Init.dev_endpoints = 8; hpcd.Init.speed = PCD_SPEED_FULL;
    hpcd.Init.phy_itface = PCD_PHY_EMBEDDED; hpcd.Init.ep0_mps = USB_MAX_EP0_SIZE;
    hpcd.Init.low_power_enable = DISABLE; hpcd.Init.lpm_enable = DISABLE;
    hpcd.Init.battery_charging_enable = DISABLE;
    HAL_PCD_Init(&hpcd);
    return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef* pdev) { (void)pdev; HAL_PCD_DeInit(&hpcd); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef* pdev) { (void)pdev; HAL_PCD_Start(&hpcd); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef* pdev) { (void)pdev; HAL_PCD_Stop(&hpcd); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef* pdev, uint8_t ep, uint8_t type, uint16_t mps) { (void)pdev; HAL_PCD_EP_Open(&hpcd, ep, mps, type); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef* pdev, uint8_t ep) { (void)pdev; HAL_PCD_EP_Close(&hpcd, ep); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef* pdev, uint8_t ep) { (void)pdev; HAL_PCD_EP_Flush(&hpcd, ep); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef* pdev, uint8_t ep) { (void)pdev; HAL_PCD_EP_SetStall(&hpcd, ep); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef* pdev, uint8_t ep) { (void)pdev; HAL_PCD_EP_ClrStall(&hpcd, ep); return USBD_OK; }
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef* pdev, uint8_t ep) { (void)pdev; uint8_t n = ep & 0x7Fu; return (ep & 0x80u) ? PCD_GET_EP_TX_STALL_STATUS(USB, n) : PCD_GET_EP_RX_STALL_STATUS(USB, n); }
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef* pdev, uint8_t addr) { (void)pdev; HAL_PCD_SetAddress(&hpcd, addr); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef* pdev, uint8_t ep, uint8_t* pbuf, uint32_t size) { (void)pdev; HAL_PCD_EP_Transmit(&hpcd, ep, pbuf, size); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef* pdev, uint8_t ep, uint8_t* pbuf, uint32_t size) { (void)pdev; HAL_PCD_EP_Receive(&hpcd, ep, pbuf, size); return USBD_OK; }
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef* pdev, uint8_t ep) { (void)pdev; return HAL_PCD_EP_GetRxCount(&hpcd, ep); }
