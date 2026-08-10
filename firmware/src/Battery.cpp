#include "Battery.h"
#include "stm32l4xx_hal.h"

static ADC_HandleTypeDef hadc;

#define BAT_GAIN_Q 395u   /* calibrated: 10 s avg 1038 mV -> true 4.1 V (x3.95) */

static uint16_t read_adc_channel(uint32_t channel)
{
    ADC_ChannelConfTypeDef s = {0};
    s.Channel = channel;
    s.Rank = ADC_REGULAR_RANK_1;
    s.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc, &s);
    HAL_ADC_Start(&hadc);
    HAL_ADC_PollForConversion(&hadc, 100);
    uint16_t v = (uint16_t)HAL_ADC_GetValue(&hadc);
    HAL_ADC_Stop(&hadc);
    return v;
}

/* The 1M/1M divider is a ~500 k source with little local capacitance at the ADC,
   so a single sample reads low. Continuous back-to-back conversions accumulate
   charge on the sampling capacitor toward the true node voltage; the last value
   is the most settled. */
static uint16_t read_adc_settled(uint32_t channel)
{
    ADC_ChannelConfTypeDef s = {0};
    s.Channel = channel;
    s.Rank = ADC_REGULAR_RANK_1;
    s.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc, &s);
    ADC1->CFGR |= ADC_CFGR_CONT;                 /* continuous conversions */
    HAL_ADC_Start(&hadc);
    uint16_t last = 0;
    for (int i = 0; i < 32; i++) {
        HAL_ADC_PollForConversion(&hadc, 100);
        last = (uint16_t)HAL_ADC_GetValue(&hadc);
    }
    HAL_ADC_Stop(&hadc);
    ADC1->CFGR &= ~ADC_CFGR_CONT;
    return last;
}

void bat_init(void)
{
    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_0;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    hadc.Instance = ADC1;
    hadc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc.Init.Resolution = ADC_RESOLUTION_12B;
    hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc.Init.LowPowerAutoWait = DISABLE;
    hadc.Init.ContinuousConvMode = DISABLE;
    hadc.Init.NbrOfConversion = 1;
    hadc.Init.DiscontinuousConvMode = DISABLE;
    hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc.Init.DMAContinuousRequests = DISABLE;
    hadc.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc.Init.OversamplingMode = DISABLE;
    HAL_ADC_Init(&hadc);

    HAL_ADCEx_Calibration_Start(&hadc, ADC_SINGLE_ENDED);
}

uint16_t bat_millivolts(void)
{
    uint32_t sum_vrefint = 0, sum_bat = 0;

    /* VREFINT: enable the internal path only for the reference read, then disable
       it so "channel 0" (PA0) samples the external divider. On this HAL VREFINT
       shares SQR channel 0 with PA0; leaving the internal path enabled hijacks
       every channel-0 read to VREFINT (~1.2 V). */
    LL_ADC_SetCommonPathInternalCh(__LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_PATH_INTERNAL_VREFINT);
    for (int i = 0; i < 8; i++) {
        sum_vrefint += (uint32_t)read_adc_channel(ADC_CHANNEL_VREFINT);
    }
    LL_ADC_SetCommonPathInternalCh(__LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_PATH_INTERNAL_NONE);
    for (int i = 0; i < 8; i++) {
        sum_bat += (uint32_t)read_adc_settled(ADC_CHANNEL_0);
    }

    uint16_t avg_bat = (uint16_t)(sum_bat / 8u);
    uint16_t avg_vrefint = (uint16_t)(sum_vrefint / 8u);
    if (avg_vrefint == 0u) return 0u;
    uint32_t vdda = (uint32_t)(*VREFINT_CAL_ADDR) * 3000UL / avg_vrefint;
    uint32_t mv = (uint32_t)avg_bat * vdda / 2048u;      /* battery-equivalent (12-bit, ÷2 divider) */
    return (uint16_t)(mv * BAT_GAIN_Q / 100u);            /* proportional correction to true battery */
}

uint8_t bat_percent(void)
{
    uint16_t mv = bat_millivolts();
    if (mv <= 3000) return 0;
    if (mv >= 4200) return 100;
    return (uint8_t)((mv - 3000u) * 100u / 1200u);
}
