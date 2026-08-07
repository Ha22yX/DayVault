#ifndef USBD_CONF_H
#define USBD_CONF_H

#include <stdint.h>
#include <string.h>
#include "stm32l4xx.h"

#define USBD_MAX_NUM_INTERFACES  1U
#define USBD_MAX_NUM_CONFIGURATION 1U
#define USBD_MAX_STR_DESC_SZ    64U
#define USBD_SELF_POWERED       1U
#define USBD_DEBUG_LEVEL        0U

#define USBD_CDC_INTERFACE_STR_ID 0U
#define USBD_CDC_CLASS_TEMPLATE_ID 0U

/* PCD FIFO / buffer config */
#define PCD_PMA_BUFFER_SIZE      512U

/* Memory management macros */
#define USBD_malloc         (void *)USBD_static_malloc
#define USBD_free           USBD_static_free
#define USBD_memset         memset
#define USBD_memcpy         memcpy
#define USBD_Delay          HAL_Delay

/* DEBUG macros */
#if (USBD_DEBUG_LEVEL > 0U)
#define  USBD_UsrLog(...)   do { \
                                 printf(__VA_ARGS__); \
                                 printf("\n"); \
                               } while (0)
#else
#define USBD_UsrLog(...) do {} while (0)
#endif /* (USBD_DEBUG_LEVEL > 0U) */

#if (USBD_DEBUG_LEVEL > 1U)
#define  USBD_ErrLog(...) do { \
                                 printf("ERROR: "); \
                                 printf(__VA_ARGS__); \
                                 printf("\n"); \
                               } while (0)
#else
#define USBD_ErrLog(...) do {} while (0)
#endif /* (USBD_DEBUG_LEVEL > 1U) */

#if (USBD_DEBUG_LEVEL > 2U)
#define  USBD_DbgLog(...)   do { \
                                 printf("DEBUG : "); \
                                 printf(__VA_ARGS__); \
                                 printf("\n"); \
                               } while (0)
#else
#define USBD_DbgLog(...) do {} while (0)
#endif /* (USBD_DEBUG_LEVEL > 2U) */

void *USBD_static_malloc(uint32_t size);
void USBD_static_free(void *p);

#endif
