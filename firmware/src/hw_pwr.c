#include "stm32l4xx_hal.h"
#include "hw_pwr.h"
#include "hw_rtc.h"
#include "dayvault_config.h"

void hw_pwr_set_wake_period(uint32_t seconds)
{
    RTC_HandleTypeDef *hrtc = hw_rtc_handle();
    HAL_RTCEx_SetWakeUpTimer_IT(hrtc, seconds, RTC_WAKEUPCLOCK_CK_SPRE_16BITS);
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
