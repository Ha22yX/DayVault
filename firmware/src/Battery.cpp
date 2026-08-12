#include "Battery.h"
#include "stm32l4xx_hal.h"

static ADC_HandleTypeDef hadc;
static bool adc_ready = false;

/* STM32L452 maps physical pin PA0 to ADC1_IN5. */
#define BAT_ADC_CHANNEL ADC_CHANNEL_5

static bool read_adc_channel(uint32_t channel, uint16_t* value)
{
    if (value == NULL) return false;
    ADC_ChannelConfTypeDef config = {0};
    config.Channel = channel;
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc, &config) != HAL_OK) return false;
    if (HAL_ADC_Start(&hadc) != HAL_OK) return false;
    if (HAL_ADC_PollForConversion(&hadc, 100) != HAL_OK) {
        HAL_ADC_Stop(&hadc);
        return false;
    }
    *value = (uint16_t)HAL_ADC_GetValue(&hadc);
    HAL_ADC_Stop(&hadc);
    return true;
}

/* R7/R8 present about 500 kOhm to the ADC. C23 is the local charge reservoir;
   repeated long samples settle the internal sample-and-hold capacitor. */
static bool read_adc_settled(uint32_t channel, uint16_t* value)
{
    if (value == NULL) return false;
    ADC_ChannelConfTypeDef config = {0};
    config.Channel = channel;
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc, &config) != HAL_OK) return false;

    ADC1->CFGR |= ADC_CFGR_CONT;
    if (HAL_ADC_Start(&hadc) != HAL_OK) {
        ADC1->CFGR &= ~ADC_CFGR_CONT;
        return false;
    }

    uint16_t last = 0;
    for (int i = 0; i < 32; i++) {
        if (HAL_ADC_PollForConversion(&hadc, 100) != HAL_OK) {
            HAL_ADC_Stop(&hadc);
            ADC1->CFGR &= ~ADC_CFGR_CONT;
            return false;
        }
        last = (uint16_t)HAL_ADC_GetValue(&hadc);
    }
    HAL_ADC_Stop(&hadc);
    ADC1->CFGR &= ~ADC_CFGR_CONT;
    *value = last;
    return true;
}

void bat_init(void)
{
    if (adc_ready) return;

    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

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
    if (HAL_ADC_Init(&hadc) != HAL_OK) return;
    if (HAL_ADCEx_Calibration_Start(&hadc, ADC_SINGLE_ENDED) != HAL_OK) {
        HAL_ADC_DeInit(&hadc);
        __HAL_RCC_ADC_CLK_DISABLE();
        return;
    }
    adc_ready = true;
}

void bat_suspend(void)
{
    if (!adc_ready) return;
    HAL_ADC_Stop(&hadc);
    HAL_ADC_DeInit(&hadc);
    __HAL_RCC_ADC_CLK_DISABLE();
    adc_ready = false;
}

uint16_t bat_millivolts(void)
{
    if (!adc_ready) return 0u;
    uint32_t sum_vrefint = 0;
    uint32_t sum_bat = 0;

    LL_ADC_SetCommonPathInternalCh(
        __LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_PATH_INTERNAL_VREFINT);
    HAL_Delay(1);
    for (int i = 0; i < 8; i++) {
        uint16_t sample = 0;
        if (!read_adc_channel(ADC_CHANNEL_VREFINT, &sample)) return 0u;
        sum_vrefint += sample;
    }

    LL_ADC_SetCommonPathInternalCh(
        __LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_PATH_INTERNAL_NONE);
    for (int i = 0; i < 8; i++) {
        uint16_t sample = 0;
        if (!read_adc_settled(BAT_ADC_CHANNEL, &sample)) return 0u;
        sum_bat += sample;
    }

    const uint16_t avg_bat = (uint16_t)(sum_bat / 8u);
    const uint16_t avg_vrefint = (uint16_t)(sum_vrefint / 8u);
    if (avg_vrefint == 0u) return 0u;

    const uint32_t vdda =
        (uint32_t)(*VREFINT_CAL_ADDR) * 3000UL / avg_vrefint;
    return (uint16_t)((uint32_t)avg_bat * vdda * 2u / 4095u);
}

uint8_t bat_percent(void)
{
    uint16_t mv = bat_millivolts();
    if (mv <= 3000u) return 0u;
    if (mv >= 4200u) return 100u;
    return (uint8_t)((mv - 3000u) * 100u / 1200u);
}
