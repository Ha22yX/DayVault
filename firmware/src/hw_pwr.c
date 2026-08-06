#include "stm32l4xx_hal.h"
#include "hw_pwr.h"
#include "hw_rtc.h"
#include "dayvault_config.h"

void hw_pwr_set_wake_period(uint32_t seconds)
{
    hw_rtc_set_wakeup_seconds((uint16_t)seconds);
    HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);
}

void hw_pwr_enter_standby(void)
{
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
    HAL_PWR_EnableBkUpAccess();
    HAL_PWR_EnterSTANDBYMode();
}
