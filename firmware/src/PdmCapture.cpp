#include "PdmCapture.h"
#include "Config.h"
#include "stm32l4xx_hal.h"
#include <string.h>

#define PDM_DMA_BUF_SAMPLES 16384u
#define PDM_DMA_BUF_FREE_MARGIN  64u                            /* never read within this many samples of write head */
#define PDM_DMA_CH      DMA1_Channel5                          /* DFSDM1_FLT1 data request on L4x2 */

static DFSDM_Filter_HandleTypeDef hf;
static DFSDM_Channel_HandleTypeDef hc;
static RingBuf* sink = NULL;
static volatile uint32_t overruns = 0;
static volatile uint32_t samples = 0;
static volatile int start_ret = 0;
static volatile uint32_t isr_count = 0;

static int16_t pdm_dma_buf[PDM_DMA_BUF_SAMPLES] __attribute__((aligned(4)));
static volatile uint32_t pdm_dma_pos = 0;
static volatile uint32_t pdm_dma_underruns = 0;

extern volatile uint32_t g_dbg_step;
void pdm_dbg_step(uint32_t v) { g_dbg_step = v; }

void pdm_init(RingBuf* s)
{
    sink = s;
    GPIO_InitTypeDef g = {0};

    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_DFSDM1;
    pclk.Dfsdm1ClockSelection = RCC_DFSDM1CLKSOURCE_SYSCLK;
    HAL_RCCEx_PeriphCLKConfig(&pclk);

    __HAL_RCC_DFSDM1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    g.Pin = PIN_PDM_CLK;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF6_DFSDM1;
    HAL_GPIO_Init(PIN_PDM_CLK_PORT, &g);

    g.Pin = PIN_PDM_DATA;
    HAL_GPIO_Init(PIN_PDM_DATA_PORT, &g);

    hc.Instance = DFSDM1_Channel1;
    hc.Init.OutputClock.Activation = ENABLE;
    hc.Init.OutputClock.Selection = DFSDM_CHANNEL_OUTPUT_CLOCK_SYSTEM;
    hc.Init.OutputClock.Divider = PDM_CKOUT_DIVIDER;
    hc.Init.Input.Multiplexer = DFSDM_CHANNEL_EXTERNAL_INPUTS;
    hc.Init.Input.DataPacking = DFSDM_CHANNEL_STANDARD_MODE;
    hc.Init.Input.Pins = DFSDM_CHANNEL_SAME_CHANNEL_PINS;
    hc.Init.SerialInterface.Type = DFSDM_CHANNEL_SPI_RISING;
    hc.Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
    hc.Init.Awd.FilterOrder = DFSDM_CHANNEL_FASTSINC_ORDER;
    hc.Init.Awd.Oversampling = 1;
    hc.Init.Offset = 0;
    hc.Init.RightBitShift = 0;
    HAL_DFSDM_ChannelInit(&hc);

    hf.Instance = DFSDM1_Filter1;
    hf.Init.RegularParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
    hf.Init.RegularParam.FastMode = DISABLE;
    hf.Init.RegularParam.DmaMode = ENABLE;
    hf.Init.InjectedParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
    hf.Init.InjectedParam.ScanMode = DISABLE;
    hf.Init.InjectedParam.DmaMode = DISABLE;
    hf.Init.InjectedParam.ExtTrigger = DFSDM_FILTER_EXT_TRIG_TIM1_TRGO;
    hf.Init.InjectedParam.ExtTriggerEdge = DFSDM_FILTER_EXT_TRIG_BOTH_EDGES;
    hf.Init.FilterParam.SincOrder = DFSDM_FILTER_SINC3_ORDER;
    hf.Init.FilterParam.Oversampling = PDM_OSR;
    hf.Init.FilterParam.IntOversampling = 1;
    HAL_DFSDM_FilterInit(&hf);
    HAL_DFSDM_FilterConfigRegChannel(&hf, DFSDM_CHANNEL_1, DFSDM_CONTINUOUS_CONV_ON);

    pdm_dbg_step(11);
    DMA1_CSELR->CSELR &= ~DMA_CSELR_C5S;
    pdm_dbg_step(12);
    PDM_DMA_CH->CPAR = (uint32_t)&DFSDM1_Filter1->FLTRDATAR + 2u;
    pdm_dbg_step(13);
    PDM_DMA_CH->CMAR = (uint32_t)pdm_dma_buf;
    pdm_dbg_step(14);
}

void pdm_start(void)
{
    overruns = 0;
    samples = 0;
    pdm_dma_pos = 0;
    pdm_dma_underruns = 0;

    DMA1_CSELR->CSELR &= ~DMA_CSELR_C5S;
    PDM_DMA_CH->CCR = 0;
    PDM_DMA_CH->CNDTR = PDM_DMA_BUF_SAMPLES;
    PDM_DMA_CH->CCR = DMA_CCR_EN_Msk | DMA_CCR_CIRC_Msk | DMA_CCR_MINC_Msk
                    | DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0 | DMA_CCR_PL_1;

    hf.Instance->FLTICR = DFSDM_FLTICR_CLRROVRF | DFSDM_FLTICR_CLRJOVRF;
    start_ret = HAL_DFSDM_FilterRegularStart(&hf);
}

void pdm_stop(void)
{
    HAL_DFSDM_FilterRegularStop(&hf);
    PDM_DMA_CH->CCR &= ~DMA_CCR_EN_Msk;
}

int pdm_dma_read(int16_t* buf, int max)
{
    int n = 0;
    while (n < max) {
        uint32_t ndtr = PDM_DMA_CH->CNDTR;
        uint32_t written = (PDM_DMA_BUF_SAMPLES - ndtr) & (PDM_DMA_BUF_SAMPLES - 1u);
        uint32_t avail = (written + PDM_DMA_BUF_SAMPLES - pdm_dma_pos) & (PDM_DMA_BUF_SAMPLES - 1u);
        if (avail > PDM_DMA_BUF_SAMPLES - PDM_DMA_BUF_FREE_MARGIN) {
            avail = PDM_DMA_BUF_SAMPLES - PDM_DMA_BUF_FREE_MARGIN;
        }
        if (avail == 0) break;
        uint32_t take = avail;
        if (take > (uint32_t)(max - n)) take = (uint32_t)(max - n);
        uint32_t first = PDM_DMA_BUF_SAMPLES - pdm_dma_pos;
        if (take > first) take = first;
        for (uint32_t i = 0; i < take; i++) {
            int32_t v = ((int32_t)(int16_t)pdm_dma_buf[(pdm_dma_pos + i) & (PDM_DMA_BUF_SAMPLES - 1u)]) * (int32_t)PDM_GAIN;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            buf[n++] = (int16_t)v;
        }
        pdm_dma_pos = (pdm_dma_pos + take) & (PDM_DMA_BUF_SAMPLES - 1u);
    }
    return n;
}

int pdm_try_read_sample(int16_t* out)
{
    if ((DFSDM1_Filter1->FLTISR & DFSDM_FLTISR_REOCF) != 0u) {
        uint32_t ch = 0;
        int32_t v = HAL_DFSDM_FilterGetRegularValue(&hf, &ch);
        int32_t s = v * (int32_t)PDM_GAIN;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        *out = (int16_t)s;
        samples++;
        return 1;
    }
    return 0;
}

int pdm_itst_start(void)
{
    isr_count = 0;
    HAL_NVIC_SetPriority(DFSDM1_FLT1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DFSDM1_FLT1_IRQn);
    hf.Instance->FLTCR2 |= DFSDM_FLTCR2_REOCIE;
    return 0;
}

int pdm_isr_count_now(void) { return (int)isr_count; }

void DFSDM1_FLT1_IRQHandler(void)
{
    isr_count++;
}

uint32_t pdm_overruns(void) { return overruns; }
uint32_t pdm_sample_count(void) { return samples; }
int pdm_start_result(void) { return start_ret; }
uint32_t pdm_isr_count(void) { return 0; }
