#pragma once
#include "usbd_def.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool usb_composite_init(void);
void usb_composite_deinit(void);
void usb_composite_poll(void);
void cdc_printf(const char* fmt, ...);
extern USBD_HandleTypeDef hUsbDevice;

#ifdef __cplusplus
}
#endif
