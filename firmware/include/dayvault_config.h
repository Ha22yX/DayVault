#ifndef DAYVAULT_CONFIG_H
#define DAYVAULT_CONFIG_H

#include "stm32l4xx_hal.h"

#define PIN_USB_DETECT    GPIO_PIN_9
#define PIN_USB_DM        GPIO_PIN_11
#define PIN_USB_DP        GPIO_PIN_12
#define PIN_LED           GPIO_PIN_8
#define PIN_SD_CS        GPIO_PIN_4
#define PIN_SD_CS_PORT   GPIOA
#define PIN_SD_SCK       GPIO_PIN_5
#define PIN_SD_MISO      GPIO_PIN_6
#define PIN_SD_MOSI      GPIO_PIN_7

#define USB_CDC_RX_LINE_MAX  64u

#define PIN_PDM_CLK          GPIO_PIN_2
#define PIN_PDM_CLK_PORT     GPIOC
#define PIN_PDM_DATA         GPIO_PIN_12
#define PIN_PDM_DATA_PORT    GPIOB
#define PDM_CKOUT_HZ         2048000u
#define PDM_CKOUT_DIVIDER    39u
#define PDM_OSR              128u
#define PDM_HALF_SAMPLES     1024u
#define PDM_RING_BYTES       (PDM_HALF_SAMPLES * 2u * 2u * 8u)

#endif
