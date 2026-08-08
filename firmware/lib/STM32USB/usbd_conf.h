#pragma once
#include "stm32l4xx_hal.h"
#include <stdint.h>
#include <string.h>

#define DEVICE_FS                       0u

#define USBD_MAX_NUM_CONFIGURATION      1u
#define USBD_MAX_NUM_INTERFACES         2u
#define USBD_MAX_NUM_ENDPOINTS          4u
#define USBD_VID                        0x0483u
#define USBD_PID                        0x5741u
#define USBD_LANGID_STRING              0x409u
#define USBD_MANUFACTURER_STRING        "DayVault"
#define USBD_PRODUCT_STRING             "DayVault Recorder"
#define USBD_CONFIGURATION_STRING       "CDC Config"
#define USBD_INTERFACE_STRING           "CDC+MSC"

#define USBD_malloc                     usbd_static_malloc
#define USBD_free                       usbd_static_free
#define USBD_memset                     memset
#define USBD_memcpy                     memcpy

#define USBD_MAX_STR_DESC_SIZ           64u

#define USBD_SELF_POWERED               1u

void* usbd_static_malloc(uint32_t size);
void  usbd_static_free(void* p);
