#include <Arduino.h>

#define PIN_LED_PA8     8
#define PIN_USB_DETECT  9
#define PIN_BOOT_PH3    51

extern "C" void SystemClock_Config(void);

uint32_t last_blink = 0;
bool led_on = false;
uint32_t report_counter = 0;

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
        Error_Handler();

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
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
        Error_Handler();

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
        Error_Handler();
}

void setup() {
  pinMode(PIN_LED_PA8, OUTPUT);
  digitalWrite(PIN_LED_PA8, LOW);
  pinMode(PIN_USB_DETECT, INPUT);
  pinMode(PIN_BOOT_PH3, INPUT_PULLDOWN);

  Serial.begin(115200);
}

void loop() {
  uint32_t now = millis();

  if (now - last_blink >= 500) {
    last_blink = now;
    led_on = !led_on;
    digitalWrite(PIN_LED_PA8, led_on ? HIGH : LOW);
  }

  if (Serial && (now - report_counter >= 1000)) {
    report_counter = now;
    Serial.print("DayVault Arduino alive, uptime=");
    Serial.print(now / 1000);
    Serial.print("s, USB_detect=");
    Serial.print(digitalRead(PIN_USB_DETECT));
    Serial.print(", BOOT=");
    Serial.print(digitalRead(PIN_BOOT_PH3));
    Serial.println();
  }
}
