#include "DeviceTime.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>

#define DT_MAGIC           0xA5A5A5A5u
#define DT_RTC_ASYNCH_PREDIV  127u
#define DT_RTC_SYNCH_PREDIV   255u

static RTC_HandleTypeDef s_hrtc;

static uint32_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);            // [0, 399]
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097u + doe - 719468u;
}

static void civil_from_days(uint32_t z, int* y, unsigned* m, unsigned* d)
{
    z += 719468;
    const int era = (z >= 0 ? (int)z : (int)z - 146096) / 146097;
    const unsigned doe = z - (unsigned)era * 146097u;          // [0, 146096]
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int yy = (int)yoe + (int)era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    *d = doy - (153u * mp + 2u) / 5u + 1u;
    *m = mp + (mp < 10u ? 3u : -9u);
    *y = yy + (*m <= 2u ? 1 : 0);
}

static uint8_t day_of_week(int y, unsigned m, unsigned d)
{
    static const uint8_t t[] = { 0u, 3u, 2u, 5u, 0u, 3u, 5u, 1u, 4u, 6u, 2u, 4u };
    y -= (m < 3u);
    int w = (y + y / 4 - y / 100 + y / 400 + (int)t[m - 1u] + (int)d) % 7;  /* 0=Sun..6=Sat */
    return (w == 0) ? 7u : (uint8_t)w;                                       /* 1=Mon..7=Sun */
}

static void rtc_store(uint32_t unix)
{
    uint32_t days = unix / 86400u;
    uint32_t sod  = unix % 86400u;
    int y;
    unsigned m, d;
    civil_from_days(days, &y, &m, &d);
    if (y > 2099) y = 2099;

    RTC_TimeTypeDef t;
    t.Hours          = (uint8_t)(sod / 3600u);
    t.Minutes        = (uint8_t)((sod / 60u) % 60u);
    t.Seconds        = (uint8_t)(sod % 60u);
    t.TimeFormat     = RTC_HOURFORMAT_24;
    t.SubSeconds     = 0u;
    t.SecondFraction = 0u;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
    HAL_RTC_SetTime(&s_hrtc, &t, RTC_FORMAT_BIN);

    RTC_DateTypeDef dt;
    dt.WeekDay = day_of_week(y, m, d);
    dt.Month   = (uint8_t)m;
    dt.Date    = (uint8_t)d;
    dt.Year    = (uint8_t)(y - 2000);
    HAL_RTC_SetDate(&s_hrtc, &dt, RTC_FORMAT_BIN);
}

void dt_init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    RCC_OscInitTypeDef o = { 0 };
    o.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    o.LSEState = RCC_LSE_ON;
    if (HAL_RCC_OscConfig(&o) != HAL_OK) {
        while (1) { }
    }

    RCC_PeriphCLKInitTypeDef p = { 0 };
    p.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    p.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&p) != HAL_OK) {
        while (1) { }
    }
    __HAL_RCC_RTC_ENABLE();

    s_hrtc.Instance = RTC;
    s_hrtc.Init.HourFormat     = RTC_HOURFORMAT_24;
    s_hrtc.Init.AsynchPrediv   = DT_RTC_ASYNCH_PREDIV;
    s_hrtc.Init.SynchPrediv    = DT_RTC_SYNCH_PREDIV;
    s_hrtc.Init.OutPut         = RTC_OUTPUT_DISABLE;
    s_hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    s_hrtc.Init.OutPutType     = RTC_OUTPUT_TYPE_OPENDRAIN;
    s_hrtc.Init.OutPutRemap    = RTC_OUTPUT_REMAP_NONE;
    if (HAL_RTC_Init(&s_hrtc) != HAL_OK) {
        while (1) { }
    }

    if (RTC->BKP0R != DT_MAGIC) {
        __HAL_RTC_WRITEPROTECTION_DISABLE(&s_hrtc);
        RTC->BKP0R = DT_MAGIC;
        __HAL_RTC_WRITEPROTECTION_ENABLE(&s_hrtc);
        rtc_store(days_from_civil(2026, 1, 1) * 86400u);   /* 2026-01-01 00:00:00 UTC */
    }
}

void dt_set_unix(uint32_t unix)
{
    rtc_store(unix);
}

uint32_t dt_get_unix(void)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef dt;
    HAL_RTC_GetTime(&s_hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&s_hrtc, &dt, RTC_FORMAT_BIN);
    uint32_t days = days_from_civil(2000 + (int)dt.Year, dt.Month, dt.Date);
    return days * 86400u
         + (uint32_t)t.Hours * 3600u
         + (uint32_t)t.Minutes * 60u
         + (uint32_t)t.Seconds;
}

void dt_format(char* buf, size_t len)
{
    if (buf == NULL) return;
    if (len < 20u) {
        if (len > 0u) buf[0] = 0;
        return;
    }
    uint32_t unix = dt_get_unix();
    int y;
    unsigned m, d;
    civil_from_days(unix / 86400u, &y, &m, &d);
    uint32_t sod = unix % 86400u;
    snprintf(buf, len, "%04d-%02u-%02u %02u:%02u:%02u",
             y, m, d, sod / 3600u, (sod / 60u) % 60u, sod % 60u);
}

void dt_set_wake(uint16_t seconds)
{
    uint32_t cnt = (uint32_t)seconds > 0u ? (uint32_t)seconds - 1u : 0u;
    HAL_RTCEx_SetWakeUpTimer_IT(&s_hrtc, cnt, RTC_WAKEUPCLOCK_CK_SPRE_16BITS);
    HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);
}

void dt_wake_off(void)
{
    HAL_NVIC_DisableIRQ(RTC_WKUP_IRQn);
    HAL_RTCEx_DeactivateWakeUpTimer(&s_hrtc);
}

extern "C" void RTC_WKUP_IRQHandler(void)
{
    HAL_RTCEx_WakeUpTimerIRQHandler(&s_hrtc);
}

void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef* hrtc)
{
    (void)hrtc;   /* wake reason: periodic re-check */
}
