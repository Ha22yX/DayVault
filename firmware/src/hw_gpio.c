#include "stm32l4xx_hal.h"
#include "hw_gpio.h"
#include "dayvault_config.h"

static volatile uint8_t usb_edge = 0;

void hw_gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};

    /* SD_CS high immediately (drive deselected during boot) */
    g.Pin = PIN_SD_CS;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_GPIO_WritePin(GPIOA, PIN_SD_CS, GPIO_PIN_SET);

    /* USB_DETECT (PA9) EXTI both edges */
    g.Pin = PIN_USB_DETECT;
    g.Mode = GPIO_MODE_IT_RISING_FALLING;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

    /* LED */
    g.Pin = PIN_LED;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);
}

uint8_t hw_gpio_usb_detect(void)
{
    return HAL_GPIO_ReadPin(GPIOA, PIN_USB_DETECT) == GPIO_PIN_SET;
}

int hw_gpio_usb_event_pending(void)
{
    return usb_edge != 0;
}

void hw_gpio_clear_usb_event(void)
{
    usb_edge = 0;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == PIN_USB_DETECT)
        usb_edge = 1;
}

void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(PIN_USB_DETECT);
}
