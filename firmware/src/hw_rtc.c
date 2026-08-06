#include "stm32l4xx_hal.h"
#include "hw_rtc.h"
#include "dayvault_config.h"

static RTC_HandleTypeDef hrtc;

struct __RTC_HandleTypeDef *hw_rtc_handle(void)
{
    return &hrtc;
}

static uint8_t to_bcd(uint8_t v) { return (uint8_t)((v / 10) << 4) | (v % 10); }
static uint8_t from_bcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }

static void rtc_msp_init(void)
{
    __HAL_RCC_RTC_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

int hw_rtc_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_PeriphCLKInitTypeDef periph = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN1);
    HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN2);
    HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN3);
    HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN4);
    HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN5);
    /* combined mask sets reserved bit 31 of PWR_SCR; clear flags separately */
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);

    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState = RCC_LSE_ON;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        return 0;

    periph.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    periph.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK)
        return 0;

    hrtc.Instance = RTC;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 127;
    hrtc.Init.SynchPrediv = 255;
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    if (HAL_RTC_Init(&hrtc) != HAL_OK)
        return 0;
    HAL_RTCEx_BKUPWrite(&hrtc, BKP_IDX_MAGIC, RTC_VALID_MAGIC);
    return 1;
}

int hw_rtc_set_time(const utc_time_t *t)
{
    RTC_DateTypeDef d;
    RTC_TimeTypeDef ti;
    d.Year = to_bcd((uint8_t)(t->year % 100));
    d.Month = to_bcd(t->month);
    d.Date = to_bcd(t->day);
    d.WeekDay = 1;
    ti.Hours = to_bcd(t->hour);
    ti.Minutes = to_bcd(t->minute);
    ti.Seconds = to_bcd(t->second);
    ti.TimeFormat = RTC_HOURFORMAT12_AM;
    ti.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    ti.StoreOperation = RTC_STOREOPERATION_RESET;
    HAL_RTC_SetTime(&hrtc, &ti, RTC_FORMAT_BCD);
    if (HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BCD) != HAL_OK)
        return 0;
    HAL_RTCEx_BKUPWrite(&hrtc, BKP_IDX_MAGIC, RTC_VALID_MAGIC);
    return 1;
}

int hw_rtc_get_time(utc_time_t *t)
{
    RTC_DateTypeDef d;
    RTC_TimeTypeDef ti;
    HAL_RTC_GetTime(&hrtc, &ti, RTC_FORMAT_BCD);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BCD);
    t->year = 2000u + from_bcd(d.Year);
    t->month = from_bcd(d.Month);
    t->day = from_bcd(d.Date);
    t->hour = from_bcd(ti.Hours);
    t->minute = from_bcd(ti.Minutes);
    t->second = from_bcd(ti.Seconds);
    return 1;
}

int hw_rtc_is_time_valid(void)
{
    return HAL_RTCEx_BKUPRead(&hrtc, BKP_IDX_MAGIC) == RTC_VALID_MAGIC;
}

void hw_rtc_mark_time_valid(void)
{
    HAL_RTCEx_BKUPWrite(&hrtc, BKP_IDX_MAGIC, RTC_VALID_MAGIC);
}

uint32_t hw_rtc_boot_counter(void)
{
    return HAL_RTCEx_BKUPRead(&hrtc, BKP_IDX_BOOT);
}

void hw_rtc_bump_boot_counter(void)
{
    uint32_t n = hw_rtc_boot_counter() + 1u;
    HAL_RTCEx_BKUPWrite(&hrtc, BKP_IDX_BOOT, n);
}

void HAL_RTC_MspInit(RTC_HandleTypeDef *hrtc_instance)
{
    rtc_msp_init();
}
