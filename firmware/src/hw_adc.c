#include "stm32l4xx_hal.h"
#include "hw_adc.h"
#include "dayvault_config.h"

static ADC_HandleTypeDef hadc1;
static ADC_ChannelConfTypeDef sconfig;

void hw_adc_init(void)
{
    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin = PIN_BAT_SENSE;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    HAL_ADC_Init(&hadc1);

    sconfig.Channel = ADC_CHANNEL_0;       /* PA0 */
    sconfig.Rank = ADC_REGULAR_RANK_1;
    sconfig.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
    sconfig.SingleDiff = ADC_SINGLE_ENDED;
    sconfig.OffsetNumber = ADC_OFFSET_NONE;
    sconfig.Offset = 0;
    HAL_ADC_ConfigChannel(&hadc1, &sconfig);
}

static uint16_t read_vrefint(void)
{
    ADC_ChannelConfTypeDef ch = sconfig;
    ch.Channel = ADC_CHANNEL_VREFINT;
    ch.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &ch);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    return (uint16_t)HAL_ADC_GetValue(&hadc1);
}

uint16_t hw_adc_read_battery_mv(void)
{
    uint32_t sum_raw = 0;
    uint32_t sum_vref = 0;
    uint32_t i;
    uint16_t vref_std = 0;

    /* Wait for the 500k-ohm/100nF divider to settle after cold start */
    HAL_Delay(5);

    HAL_ADC_Start(&hadc1);
    for (i = 0; i < BAT_ADC_AVG_COUNT; i++)
    {
        HAL_ADC_PollForConversion(&hadc1, 10);
        sum_raw += (uint32_t)HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
    vref_std = read_vrefint();

    /* VREFINT ~1.2 V typical. Vbat = code/4095 * Vref_actual * 2,
       Vref_actual = VREFINT_CAL(3.0V,1.2V) * 1.2 / (code_vref * 3.0/4096)... */
    if (vref_std == 0)
        return 0;
    {
        uint32_t raw = sum_raw / BAT_ADC_AVG_COUNT;
        uint32_t vref_actual_mv = (3000u * (uint32_t)3u * 4096u) / (uint32_t)vref_std;
        uint32_t vbat = (raw * vref_actual_mv * 2u) / 4096u;
        return (uint16_t)vbat;
    }
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    __HAL_RCC_ADC_CLK_ENABLE();
}
