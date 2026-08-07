#ifndef USBD_CONF_H
#define USBD_CONF_H

#include "stm32l4xx_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USBD_MAX_NUM_INTERFACES       3u
#define USBD_MAX_NUM_CONFIGURATION    1u
#define USBD_MAX_STR_DESC_SIZ         64u
#define USBD_SELF_POWERED             1u
#define USBD_DEBUG_LEVEL              0u

/* Composite activation (from usbd_composite_builder.h); guarded so the
   platformio.ini -DUSBD_CMPSIT_ACTIVATE_* flags do not cause redefinition */
#ifndef USBD_CMPSIT_ACTIVATE_CDC
#define USBD_CMPSIT_ACTIVATE_CDC 1U
#endif
#ifndef USBD_CMPSIT_ACTIVATE_MSC
#define USBD_CMPSIT_ACTIVATE_MSC 1U
#endif
#define USBD_CMPST_MAX_CONFDESC_SZ 300U

/* MSC class config: SCSI data buffer (must match SD sector size 512) */
#define MSC_MEDIA_PACKET 512U

/* PCD low-level config */
#define PCD_SOFTEND_MODE              0u
#define USB_EXT_LD_ENABLE             0u
#define USB_EXT_LD_INVERT             0u

#ifndef USE_USBD_COMPOSITE
#define USE_USBD_COMPOSITE            1u
#endif

/* Low level core index used by USBD_Init */
#define DEVICE_FS 0U

/* Memory / delay abstractions used by the USB Device Library */
#define USBD_malloc         (void *)USBD_static_malloc
#define USBD_free           USBD_static_free
#define USBD_memset         memset
#define USBD_memcpy         memcpy
#define USBD_Delay          HAL_Delay

void *USBD_static_malloc(uint32_t size);
void USBD_static_free(void *p);

#endif
