#include <Arduino.h>
#include "stm32l4xx_hal.h"
#include "Config.h"
#include "SdCard.h"

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

#define PIN_USB_DETECT 9    /* PA9 */
#define PIN_BOOT0       51  /* PH3 */

#define SYSTEM_MEMORY_BASE 0x1FFF0000u

static void dfu_enter(void)
{
    HAL_DeInit();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    __disable_irq();

    uint32_t msp = *(volatile uint32_t *)SYSTEM_MEMORY_BASE;
    if ((msp & 0xFFF00000u) != 0x20000000u) {
        NVIC_SystemReset();   /* invalid bootloader stack: reset instead */
        return;
    }
    __set_MSP(msp);
    ((void (*)(void)) * (volatile uint32_t *)(SYSTEM_MEMORY_BASE + 4u))();
}

void setup()
{
    SystemClock_Config();
    pinMode(PIN_USB_DETECT, INPUT);
    pinMode(PIN_BOOT0, INPUT_PULLDOWN);

    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && (millis() - t) < 3000) { }

    Serial.println("DV step2 ready");
    Serial.print("usb_detect="); Serial.print(digitalRead(PIN_USB_DETECT));
    Serial.print(" boot="); Serial.println(digitalRead(PIN_BOOT0));
}

void loop()
{
    static uint32_t last_tick = 0;

    if (Serial.available()) {
        static char line[64];
        static size_t n = 0;
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                if (n > 0) {
                    line[n] = 0;
                    if (strncmp(line, "DFU", 3) == 0) {
                        Serial.println("entering DFU...");
                        Serial.flush();
                        dfu_enter();
                    } else if (strncmp(line, "INFO", 4) == 0) {
                        Serial.print("INFO usb_detect="); Serial.print(digitalRead(PIN_USB_DETECT));
                        Serial.print(" boot="); Serial.print(digitalRead(PIN_BOOT0));
                        Serial.print(" up="); Serial.print(millis());
                        Serial.print(" sd=");
                        if (sd_capacity_bytes() > 0) { Serial.print(sd_capacity_bytes()); Serial.print("B"); }
                        else { Serial.print("none"); }
                        Serial.println();
                    } else if (strncmp(line, "SD", 2) == 0) {
                        bool ok = sd_init();
                        Serial.print("SD init="); Serial.print(ok ? "OK" : "FAIL");
                        if (ok) { Serial.print(" cap="); Serial.print(sd_capacity_bytes()); Serial.println("B"); }
                        else { Serial.println(); }
                    } else {
                        Serial.print("? "); Serial.println(line);
                    }
                    n = 0;
                }
            } else if (n < sizeof(line) - 1) {
                line[n++] = c;
            }
        }
    }

    uint32_t now = millis();
    if (now - last_tick >= 1000) {
        last_tick = now;
        if (Serial) {
            Serial.print("tick usb_detect="); Serial.print(digitalRead(PIN_USB_DETECT));
            Serial.print(" boot="); Serial.println(digitalRead(PIN_BOOT0));
        }
    }
}
