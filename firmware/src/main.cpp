#include <Arduino.h>
#include "stm32l4xx_hal.h"
#include "Config.h"
#include "UsbComposite.h"

extern "C" void SystemClock_Config(void);

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
        while (1) { }
    }

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI | RCC_OSCILLATORTYPE_HSI48;
    RCC_OscInitStruct.MSIState = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = 0;
    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_7;
    RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 20;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV4;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { while (1) { } }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) { while (1) { } }

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) { while (1) { } }
}

static uint32_t banner_until = 0;

void setup()
{
    SystemClock_Config();

    __HAL_RCC_GPIOH_CLK_ENABLE();
    GPIO_InitTypeDef boot_pin = {0};
    boot_pin.Pin = GPIO_PIN_3;
    boot_pin.Mode = GPIO_MODE_INPUT;
    boot_pin.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOH, &boot_pin);

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
    pinMode(PIN_USB_DETECT, INPUT);

    usb_composite_init();
    banner_until = millis() + 5000u;
    cdc_printf("DV alive usb_detect=%d boot=%d\n", HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9), HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3));
}

void loop()
{
    usb_composite_poll();
    if (millis() < banner_until) {
        cdc_printf("DV alive usb_detect=%d boot=%d\n", HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9), HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3));
    }
    delay(100);
}