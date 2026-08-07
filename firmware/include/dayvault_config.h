#ifndef DAYVAULT_CONFIG_H
#define DAYVAULT_CONFIG_H

#include "stm32l4xx_hal.h"

#define PIN_USB_DETECT    GPIO_PIN_9
#define PIN_USB_DM        GPIO_PIN_11
#define PIN_USB_DP        GPIO_PIN_12
#define PIN_LED           GPIO_PIN_8

#define USB_CDC_RX_LINE_MAX  64u

#endif
