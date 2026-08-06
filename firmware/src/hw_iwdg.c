#include "stm32l4xx_hal.h"
#include "hw_iwdg.h"

static IWDG_HandleTypeDef hiwdg;

void hw_iwdg_init(void)
{
    __HAL_RCC_LSI_ENABLE();
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) == RESET) {}
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_128;  /* LSI ~32k /128 = 250 Hz */
    hiwdg.Init.Reload = 2500;                   /* 2500/250 = 10 s window */
    hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
    HAL_IWDG_Init(&hiwdg);
}

void hw_iwdg_feed(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}
