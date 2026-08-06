#ifndef __STM32L4xx_HAL_CONF_H
#define __STM32L4xx_HAL_CONF_H

#ifdef __cplusplus
 extern "C" {
#endif

/* ########################## Module Selection ############################## */
#define HAL_ADC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DFSDM_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED
#define HAL_PCD_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_RTC_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* ########################## Register Callbacks selection ############################## */
#define USE_HAL_ADC_REGISTER_CALLBACKS     0U
#define USE_HAL_DFSDM_REGISTER_CALLBACKS   0U
#define USE_HAL_DMA_REGISTER_CALLBACKS     0U
#define USE_HAL_PCD_REGISTER_CALLBACKS     0U
#define USE_HAL_RTC_REGISTER_CALLBACKS     0U
#define USE_HAL_SPI_REGISTER_CALLBACKS     0U
#define USE_HAL_UART_REGISTER_CALLBACKS    0U

/* ########################## Oscillator Values adaptation ####################*/
#if !defined  (HSE_VALUE)
  #define HSE_VALUE    8000000U
#endif
#if !defined  (HSI_VALUE)
  #define HSI_VALUE    16000000U
#endif
#if !defined  (LSE_VALUE)
  #define LSE_VALUE    32768U
#endif
#if !defined  (LSI_VALUE)
  #define LSI_VALUE    32000U
#endif
#if !defined  (EXTERNAL_CLOCK_VALUE)
  #define EXTERNAL_CLOCK_VALUE    12288000U
#endif

/* ########################### System Configuration ######################### */
#define  VDD_VALUE                    3300U
#define  TICK_INT_PRIORITY            0U
#define  USE_RTOS                     0U
#define  PREFETCH_ENABLE              1U
#define  INSTRUCTION_CACHE_ENABLE     1U
#define  DATA_CACHE_ENABLE            1U

/* ########################## Assert Selection ############################## */
#define  USE_FULL_ASSERT              0U

/* Includes ------------------------------------------------------------------*/
#if defined (HAL_ADC_MODULE_ENABLED)
  #include "stm32l4xx_hal_adc.h"
#endif
#if defined (HAL_CORTEX_MODULE_ENABLED)
  #include "stm32l4xx_hal_cortex.h"
#endif
#if defined (HAL_DFSDM_MODULE_ENABLED)
  #include "stm32l4xx_hal_dfsdm.h"
#endif
#if defined (HAL_DMA_MODULE_ENABLED)
  #include "stm32l4xx_hal_dma.h"
#endif
#if defined (HAL_EXTI_MODULE_ENABLED)
  #include "stm32l4xx_hal_exti.h"
#endif
#if defined (HAL_FLASH_MODULE_ENABLED)
  #include "stm32l4xx_hal_flash.h"
#endif
#if defined (HAL_GPIO_MODULE_ENABLED)
  #include "stm32l4xx_hal_gpio.h"
#endif
#if defined (HAL_IWDG_MODULE_ENABLED)
  #include "stm32l4xx_hal_iwdg.h"
#endif
#if defined (HAL_PCD_MODULE_ENABLED)
  #include "stm32l4xx_hal_pcd.h"
#endif
#if defined (HAL_PWR_MODULE_ENABLED)
  #include "stm32l4xx_hal_pwr.h"
#endif
#if defined (HAL_RCC_MODULE_ENABLED)
  #include "stm32l4xx_hal_rcc.h"
#endif
#if defined (HAL_RTC_MODULE_ENABLED)
  #include "stm32l4xx_hal_rtc.h"
#endif
#if defined (HAL_SPI_MODULE_ENABLED)
  #include "stm32l4xx_hal_spi.h"
#endif
#if defined (HAL_UART_MODULE_ENABLED)
  #include "stm32l4xx_hal_uart.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* __STM32L4xx_HAL_CONF_H */
