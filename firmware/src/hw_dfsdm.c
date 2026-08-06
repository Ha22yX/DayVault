#include "stm32l4xx_hal.h"
#include "hw_dfsdm.h"
#include "dayvault_config.h"
#include <string.h>

static DMA_HandleTypeDef hdma_dfsdm;
static DFSDM_Filter_HandleTypeDef hdfsdm_filter;
static DFSDM_Channel_HandleTypeDef hdfsdm_channel;
static int16_t pcm_buf[2][PDM_HALF_SAMPLES];
static volatile uint32_t current_buf = 0;
static volatile uint32_t overrun_count = 0;
static volatile uint8_t full_flag[2] = {0, 0};
static dfsdm_buffer_cb app_cb = 0;

void hw_dfsdm_set_callback(dfsdm_buffer_cb cb)
{
    app_cb = cb;
}

void hw_dfsdm_init(void)
{
    __HAL_RCC_DFSDM1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* PC2 = DFSDM1_CKOUT (AF6), PB12 = DFSDM1_DATIN1 (AF6) on L452.
       Note: brief used GPIO_AF3_DFSDM1; L4 puts DFSDM1 on AF6. */
    GPIO_InitTypeDef g = {0};
    g.Pin = PIN_PDM_CLK;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF6_DFSDM1;
    HAL_GPIO_Init(PIN_PDM_CLK_PORT, &g);

    g.Pin = PIN_PDM_DATA;
    g.Alternate = GPIO_AF6_DFSDM1;
    HAL_GPIO_Init(PIN_PDM_DATA_PORT, &g);

    /* Brief: OutputClock.Activation=ENABLE, OutputClock.Selection=DFSDM_CLOCKOUT_DIV2.
       L4 HAL: Selection is DFSDM_CHANNEL_OUTPUT_CLOCK_SYSTEM/AUDIO and the divider is
       a separate OutputClock.Divider field (2..256). Placeholder, tuned on board. */
    hdfsdm_channel.Instance = DFSDM1_Channel1;
    hdfsdm_channel.Init.OutputClock.Activation = ENABLE;
    hdfsdm_channel.Init.OutputClock.Selection = DFSDM_CHANNEL_OUTPUT_CLOCK_SYSTEM;
    hdfsdm_channel.Init.OutputClock.Divider = 2;
    /* Brief: Input.Multiplexer=DFSDM_INPUT_EXTERNAL -> DFSDM_CHANNEL_EXTERNAL_INPUTS.
       Brief: Input.Pins=DFSDM_DATA_ON_PIN1 -> DFSDM_CHANNEL_SAME_CHANNEL_PINS (DATIN1 on ch1). */
    hdfsdm_channel.Init.Input.Multiplexer = DFSDM_CHANNEL_EXTERNAL_INPUTS;
    hdfsdm_channel.Init.Input.Pins = DFSDM_CHANNEL_SAME_CHANNEL_PINS;
    hdfsdm_channel.Init.SerialInterface.Type = DFSDM_CHANNEL_SPI_RISING;
    hdfsdm_channel.Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
    /* Brief: Awd.FilterOrder=DFSDM_AWD_FILTER_DISABLED; L4 has no "disabled" order,
       AWD is only active when started via HAL_DFSDM_FilterAwdStart_IT, so this value
       is a placeholder. Oversampling is consumed unconditionally by ChannelInit. */
    hdfsdm_channel.Init.Awd.FilterOrder = DFSDM_CHANNEL_SINC1_ORDER;
    hdfsdm_channel.Init.Awd.Oversampling = 1;
    hdfsdm_channel.Init.Offset = 0;
    hdfsdm_channel.Init.RightBitShift = 0;
    HAL_DFSDM_ChannelInit(&hdfsdm_channel);

    /* Brief used flat filter Init fields (SincOrder/Oversampling/IntOversampling/
       ShortCircuitDetector/ClockDivider) and DFSDM_FILTER_SINC_ORDER_3,
       DFSDM_FILTER_OVERSAMPLING_128, DFSDM_FILTER_INTEGRATOR_1. L4 nests them under
       FilterParam and drops ShortCircuitDetector/ClockDivider (SCD is channel-level,
       CKOUT divider is the channel OutputClock.Divider above). */
    hdfsdm_filter.Instance = DFSDM1_Filter0;
    /* Brief: HAL_DFSDM_FilterDMAConfigStructInit(&hdfsdm_filter, DFSDM_DMA_DISABLE_EMPTY)
       has no L4 counterpart; DmaMode=ENABLE here is what sets FLTCR1.RDMAEN, which the
       L4 DMA start API requires. */
    hdfsdm_filter.Init.RegularParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
    hdfsdm_filter.Init.RegularParam.FastMode = DISABLE;
    hdfsdm_filter.Init.RegularParam.DmaMode = ENABLE;
    hdfsdm_filter.Init.FilterParam.SincOrder = DFSDM_FILTER_SINC3_ORDER;
    hdfsdm_filter.Init.FilterParam.Oversampling = 128;
    hdfsdm_filter.Init.FilterParam.IntOversampling = 1;
    HAL_DFSDM_FilterInit(&hdfsdm_filter);

    /* Brief relied on H7-style HAL_DFSDM_FilterDMAStart (DMA handled internally).
       The L4 HAL drives an external DMA handle linked through hdmaReg; L452 has no
       DMAMUX, so DFSDM1_FLT0 is fixed on DMA1_Channel4 with CSELR C4S=0000
       (Init.Request = DMA_REQUEST_0). */
    hdma_dfsdm.Instance = DMA1_Channel4;
    hdma_dfsdm.Init.Request = DMA_REQUEST_0;
    hdma_dfsdm.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_dfsdm.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_dfsdm.Init.MemInc = DMA_MINC_ENABLE;
    hdma_dfsdm.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_dfsdm.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_dfsdm.Init.Mode = DMA_CIRCULAR;
    hdma_dfsdm.Init.Priority = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&hdma_dfsdm);

    __HAL_LINKDMA(&hdfsdm_filter, hdmaReg, hdma_dfsdm);
    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);

    /* Brief: HAL_DFSDM_FilterConfigStructInit(&hdfsdm_filter, DFSDM_FILTER_CONTINUOUS).
       L4 equivalent: select regular channel 1 with continuous mode; required so the
       DMA start API accepts the circular transfer. */
    HAL_DFSDM_FilterConfigRegChannel(&hdfsdm_filter, DFSDM_CHANNEL_1, DFSDM_CONTINUOUS_CONV_ON);
}

void hw_dfsdm_start(void)
{
    current_buf = 0;
    overrun_count = 0;
    memset((void *)full_flag, 0, sizeof(full_flag));
    memset(pcm_buf, 0, sizeof(pcm_buf));
    /* Brief: HAL_DFSDM_FilterDMAStart(&hdfsdm_filter, pcm_buf[0], PDM_HALF_SAMPLES).
       L4 has no FilterDMAStart; HAL_DFSDM_FilterRegularMsbStart_DMA is the int16_t
       variant. One circular transfer over BOTH halves: the DMA HalfCplt/Cplt events
       deliver pcm_buf[0]/pcm_buf[1] ping-pong to the callbacks below. */
    HAL_DFSDM_FilterRegularMsbStart_DMA(&hdfsdm_filter, pcm_buf[0], 2u * PDM_HALF_SAMPLES);
}

void hw_dfsdm_stop(void)
{
    HAL_DFSDM_FilterRegularStop_DMA(&hdfsdm_filter);
}

uint32_t hw_dfsdm_overruns(void)
{
    return overrun_count;
}

/* Brief: HAL_DFSDM_FilterCpltCallback/HAL_DFSDM_FilterHalfCpltCallback. L4 names are
   HAL_DFSDM_FilterRegConvCpltCallback/HAL_DFSDM_FilterRegConvHalfCpltCallback; they are
   the weak DMA-regular half/full hooks invoked via DFSDM_DMARegularHalfConvCplt/Cplt. */
void HAL_DFSDM_FilterRegConvHalfCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm)
{
    (void)hdfsdm;
    if (app_cb)
        app_cb(pcm_buf[current_buf], PDM_HALF_SAMPLES);
    full_flag[current_buf] = 1;
    current_buf = (current_buf + 1) % 2;
}

void HAL_DFSDM_FilterRegConvCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm)
{
    (void)hdfsdm;
    if (app_cb)
        app_cb(pcm_buf[current_buf], PDM_HALF_SAMPLES);
    full_flag[current_buf] = 1;
    current_buf = (current_buf + 1) % 2;
}

void DMA1_Channel4_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_dfsdm);
}
