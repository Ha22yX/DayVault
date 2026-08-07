#include "stm32l4xx_hal.h"
#include "hw_dfsdm.h"
#include "dayvault_config.h"
#include <string.h>

static DFSDM_Filter_HandleTypeDef hdfsdm1;
static DFSDM_Channel_HandleTypeDef hch1;
static ringbuf_t *sink = 0;
static volatile uint32_t overrun = 0;

#ifndef STEREO_MONO_FALLBACK
static DFSDM_Filter_HandleTypeDef hdfsdm0;
static DFSDM_Channel_HandleTypeDef hch0;
static DMA_HandleTypeDef hdma_flt0;
static DMA_HandleTypeDef hdma_flt1;
static int16_t buf_ch1[PDM_HALF_SAMPLES * 2u];
static int16_t buf_ch0[PDM_HALF_SAMPLES * 2u];
static volatile uint8_t half_ready[2];
static volatile uint8_t half_idx[2];
#else
static DMA_HandleTypeDef hdma_flt1;
static int16_t buf_ch1[PDM_HALF_SAMPLES * 2u];
#endif

#define STEREO_PUSH_CHUNK 128u

static void push_stereo(const int16_t *l, const int16_t *r, uint16_t n)
{
    static int16_t inter[STEREO_PUSH_CHUNK * 2u];
    uint16_t off;
    uint16_t i;
    size_t want;
    size_t got;
    if (!sink)
        return;
    off = 0;
    while (off < n)
    {
        uint16_t c = n - off;
        if (c > STEREO_PUSH_CHUNK)
            c = STEREO_PUSH_CHUNK;
        for (i = 0; i < c; i++)
        {
            inter[i * 2u] = l[off + i];
            inter[i * 2u + 1u] = r[off + i];
        }
        want = (size_t)c * 2u * 2u;
        got = ringbuf_write(sink, (const uint8_t *)inter, want);
        if (got < want)
            overrun++;
        off += c;
    }
}

static void dfsdm_half_done(DFSDM_Filter_HandleTypeDef *hdfsdm, uint32_t half)
{
#ifndef STEREO_MONO_FALLBACK
    uint32_t f = (hdfsdm->Instance == DFSDM1_Filter1) ? 1u : 0u;
    half_idx[f] = (uint8_t)half;
    half_ready[f] = 1;
    if (half_ready[0] && half_ready[1])
    {
        const int16_t *l;
        const int16_t *r;
        half_ready[0] = 0;
        half_ready[1] = 0;
        l = buf_ch1 + (half_idx[1] ? PDM_HALF_SAMPLES : 0);
        r = buf_ch0 + (half_idx[0] ? PDM_HALF_SAMPLES : 0);
        push_stereo(l, r, PDM_HALF_SAMPLES);
    }
#else
    const int16_t *p = buf_ch1 + (half ? PDM_HALF_SAMPLES : 0);
    push_stereo(p, p, PDM_HALF_SAMPLES);
#endif
}

void HAL_DFSDM_FilterRegConvCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm)
{
    dfsdm_half_done(hdfsdm, 1u);
}

void HAL_DFSDM_FilterRegConvHalfCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm)
{
    dfsdm_half_done(hdfsdm, 0u);
}

static void dma_flt_config(DMA_HandleTypeDef *hdma, DMA_Channel_TypeDef *ch,
                           IRQn_Type irq)
{
    hdma->Instance = ch;
    hdma->Init.Request = DMA_REQUEST_0;
    hdma->Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma->Init.PeriphInc = DMA_PINC_DISABLE;
    hdma->Init.MemInc = DMA_MINC_ENABLE;
    hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma->Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma->Init.Mode = DMA_CIRCULAR;
    hdma->Init.Priority = DMA_PRIORITY_HIGH;
    __HAL_DMA_RESET_HANDLE_STATE(hdma);
    if (HAL_DMA_Init(hdma) != HAL_OK)
        return;
    HAL_NVIC_SetPriority(irq, 5, 0);
    HAL_NVIC_EnableIRQ(irq);
}

void HAL_DFSDM_FilterMspInit(DFSDM_Filter_HandleTypeDef *hdfsdm)
{
    if (hdfsdm->Instance == DFSDM1_Filter1)
    {
        __HAL_LINKDMA(hdfsdm, hdmaReg, hdma_flt1);
        dma_flt_config(&hdma_flt1, DMA1_Channel5, DMA1_Channel5_IRQn);
    }
#ifndef STEREO_MONO_FALLBACK
    else if (hdfsdm->Instance == DFSDM1_Filter0)
    {
        __HAL_LINKDMA(hdfsdm, hdmaReg, hdma_flt0);
        dma_flt_config(&hdma_flt0, DMA1_Channel4, DMA1_Channel4_IRQn);
    }
#endif
}

#ifndef STEREO_MONO_FALLBACK
void DMA1_Channel4_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_flt0);
}
#endif

void DMA1_Channel5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_flt1);
}

static void channel_common(DFSDM_Channel_HandleTypeDef *ch, uint32_t type)
{
    ch->Init.OutputClock.Activation = DISABLE;
    ch->Init.Input.Multiplexer = DFSDM_CHANNEL_EXTERNAL_INPUTS;
    ch->Init.Input.DataPacking = DFSDM_CHANNEL_STANDARD_MODE;
    ch->Init.Input.Pins = DFSDM_CHANNEL_SAME_CHANNEL_PINS;
    ch->Init.SerialInterface.Type = type;
    ch->Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
    ch->Init.Awd.FilterOrder = DFSDM_CHANNEL_FASTSINC_ORDER;
    ch->Init.Awd.Oversampling = 1;
    ch->Init.Offset = 0;
    ch->Init.RightBitShift = 0;
}

static void filter_common(DFSDM_Filter_HandleTypeDef *f)
{
    f->Init.RegularParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
    f->Init.RegularParam.FastMode = ENABLE;
    f->Init.RegularParam.DmaMode = ENABLE;
    f->Init.InjectedParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
    f->Init.InjectedParam.ScanMode = DISABLE;
    f->Init.InjectedParam.DmaMode = DISABLE;
    f->Init.InjectedParam.ExtTrigger = DFSDM_FILTER_EXT_TRIG_TIM1_TRGO;
    f->Init.InjectedParam.ExtTriggerEdge = DFSDM_FILTER_EXT_TRIG_BOTH_EDGES;
    f->Init.FilterParam.SincOrder = DFSDM_FILTER_SINC3_ORDER;
    f->Init.FilterParam.Oversampling = PDM_OSR;
    f->Init.FilterParam.IntOversampling = 1;
}

void hw_dfsdm_init(void)
{
    GPIO_InitTypeDef g = {0};

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

    hch1.Instance = DFSDM1_Channel1;
    hch1.Init.OutputClock.Activation = ENABLE;
    hch1.Init.OutputClock.Selection = DFSDM_CHANNEL_OUTPUT_CLOCK_SYSTEM;
    hch1.Init.OutputClock.Divider = PDM_CKOUT_DIVIDER;
    channel_common(&hch1, DFSDM_CHANNEL_SPI_RISING);
    HAL_DFSDM_ChannelInit(&hch1);

#ifndef STEREO_MONO_FALLBACK
    hch0.Instance = DFSDM1_Channel0;
    hch0.Init.OutputClock.Activation = DISABLE;
    channel_common(&hch0, DFSDM_CHANNEL_SPI_FALLING);
    hch0.Init.Input.Pins = DFSDM_CHANNEL_FOLLOWING_CHANNEL_PINS;
    HAL_DFSDM_ChannelInit(&hch0);
#endif

    hdfsdm1.Instance = DFSDM1_Filter1;
    filter_common(&hdfsdm1);
    HAL_DFSDM_FilterInit(&hdfsdm1);
    HAL_DFSDM_FilterConfigRegChannel(&hdfsdm1, DFSDM_CHANNEL_1, DFSDM_CONTINUOUS_CONV_ON);

#ifndef STEREO_MONO_FALLBACK
    hdfsdm0.Instance = DFSDM1_Filter0;
    filter_common(&hdfsdm0);
    HAL_DFSDM_FilterInit(&hdfsdm0);
    HAL_DFSDM_FilterConfigRegChannel(&hdfsdm0, DFSDM_CHANNEL_0, DFSDM_CONTINUOUS_CONV_ON);
#endif
}

void hw_dfsdm_start(void)
{
    memset(buf_ch1, 0, sizeof(buf_ch1));
#ifndef STEREO_MONO_FALLBACK
    memset(buf_ch0, 0, sizeof(buf_ch0));
    half_ready[0] = 0;
    half_ready[1] = 0;
    if (HAL_DFSDM_FilterRegularMsbStart_DMA(&hdfsdm0, buf_ch0, PDM_HALF_SAMPLES * 2u) != HAL_OK)
        overrun++;
#endif
    if (HAL_DFSDM_FilterRegularMsbStart_DMA(&hdfsdm1, buf_ch1, PDM_HALF_SAMPLES * 2u) != HAL_OK)
        overrun++;
}

void hw_dfsdm_stop(void)
{
#ifndef STEREO_MONO_FALLBACK
    HAL_DFSDM_FilterRegularStop_DMA(&hdfsdm0);
#endif
    HAL_DFSDM_FilterRegularStop_DMA(&hdfsdm1);
}

uint32_t hw_dfsdm_overruns(void)
{
    return overrun;
}

void hw_dfsdm_set_sink(ringbuf_t *rb)
{
    sink = rb;
}
