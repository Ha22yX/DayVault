#include "PdmCapture.h"
#include "Config.h"
#include "PdmRing.h"
#include "stm32l4xx_hal.h"
#include <string.h>
#include <math.h>

#define PDM_DMA_BUF_SAMPLES 8192u
#define PDM_DMA_BUF_FREE_MARGIN  64u                            /* never read within this many samples of write head */
#define PDM_DMA_CH      DMA1_Channel5                          /* DFSDM1_FLT1 data request on L4x2 */
#define PDM_DMA2_CH     DMA1_Channel4                          /* DFSDM1_FLT0 data request on L4x2 */

static DFSDM_Filter_HandleTypeDef hf;
static DFSDM_Channel_HandleTypeDef hc;
static DFSDM_Filter_HandleTypeDef hf0;
static DFSDM_Channel_HandleTypeDef hc0;
static RingBuf* sink = NULL;
static volatile uint32_t overruns = 0;
static volatile uint32_t samples = 0;
static volatile int start_ret = 0;
static volatile uint32_t isr_count = 0;

static int16_t pdm_dma_buf[PDM_DMA_BUF_SAMPLES] __attribute__((aligned(4)));
static int16_t pdm_dma_buf2[PDM_DMA_BUF_SAMPLES] __attribute__((aligned(4)));
static volatile uint32_t pdm_dma_pos = 0;
static volatile uint32_t pdm_dma_pos2 = 0;
static volatile PdmCaptureStats pdm_dma_stats = {0};
static volatile uint32_t pdm_dma_completed_a = 0;
static volatile uint32_t pdm_dma_completed_b = 0;

static uint32_t pdm_irq_lock(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void pdm_irq_unlock(uint32_t primask)
{
    if ((primask & 1u) == 0u) __enable_irq();
}

static bool pdm_dma_account_pending_tc(volatile uint32_t* completed,
                                       uint32_t tc_flag, uint32_t clear_flag)
{
    if ((DMA1->ISR & tc_flag) == 0u) return false;
    DMA1->IFCR = clear_flag;
    (*completed)++;
    return true;
}

static uint32_t pdm_dma_snapshot_produced(volatile DMA_Channel_TypeDef* dma,
                                          volatile uint32_t* completed,
                                          uint32_t tc_flag, uint32_t clear_flag)
{
    pdm_dma_account_pending_tc(completed, tc_flag, clear_flag);
    uint32_t ndtr = dma->CNDTR;
    if (pdm_dma_account_pending_tc(completed, tc_flag, clear_flag)) {
        ndtr = dma->CNDTR;
    }
    return pdm_ring_produced_from_tc(*completed, PDM_DMA_BUF_SAMPLES, ndtr);
}

extern volatile uint32_t g_dbg_step;
void pdm_dbg_step(uint32_t v) { g_dbg_step = v; }

void pdm_raw_diag(int32_t* rms, int32_t* zcr, int32_t* peak, int32_t* n)
{
    uint32_t base = pdm_dma_pos;
    int64_t ss = 0;
    int32_t mx = 0, prev = 0, zc = 0;
    uint32_t cnt = 0;
    for (uint32_t i = 0; i < 4096u; i++) {
        int32_t v = (int32_t)(int16_t)pdm_dma_buf[(base + i) & (PDM_DMA_BUF_SAMPLES - 1u)];
        ss += (int64_t)v * v;
        if (i > 0 && ((v >= 0 && prev < 0) || (v < 0 && prev >= 0))) zc++;
        prev = v;
        int32_t a = (v < 0) ? -v : v;
        if (a > mx) mx = a;
        cnt++;
    }
    *rms = (int32_t)sqrt((double)ss / cnt);
    *zcr = (int32_t)((int64_t)zc * 1000 / cnt);   /* zero crossings per 1000 samples ~ Hz-ish */
    *peak = mx;
    *n = (int32_t)cnt;
}

void pdm_dual_diag(int32_t* u1rms, int32_t* u2rms, int32_t* corr, int32_t* n)
{
    uint32_t base = pdm_dma_pos;
    int64_t s1 = 0, s2 = 0, s12 = 0;
    uint32_t cnt = 0;
    for (uint32_t i = 0; i < 4096u; i++) {
        int32_t a = ((int32_t)(int16_t)pdm_dma_buf[(base + i) & (PDM_DMA_BUF_SAMPLES - 1u)]);
        int32_t b = ((int32_t)(int16_t)pdm_dma_buf2[(base + i) & (PDM_DMA_BUF_SAMPLES - 1u)]);
        s1 += (int64_t)a * a;
        s2 += (int64_t)b * b;
        s12 += (int64_t)a * b;
        cnt++;
    }
    *u1rms = (int32_t)sqrt((double)s1 / cnt);
    *u2rms = (int32_t)sqrt((double)s2 / cnt);
    double denom = sqrt((double)s1) * sqrt((double)s2) + 1.0;
    *corr = (int32_t)(1000.0 * (double)s12 / denom);
    *n = (int32_t)cnt;
}

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

    hc0.Instance = DFSDM1_Channel0;
    hc0.Init.OutputClock.Activation = DISABLE;
    hc0.Init.OutputClock.Selection = DFSDM_CHANNEL_OUTPUT_CLOCK_SYSTEM;
    hc0.Init.OutputClock.Divider = PDM_CKOUT_DIVIDER;
    hc0.Init.Input.Multiplexer = DFSDM_CHANNEL_EXTERNAL_INPUTS;
    hc0.Init.Input.DataPacking = DFSDM_CHANNEL_STANDARD_MODE;
    hc0.Init.Input.Pins = DFSDM_CHANNEL_FOLLOWING_CHANNEL_PINS;
    hc0.Init.SerialInterface.Type = DFSDM_CHANNEL_SPI_FALLING;
    hc0.Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
    hc0.Init.Awd.FilterOrder = DFSDM_CHANNEL_FASTSINC_ORDER;
    hc0.Init.Awd.Oversampling = 1;
    hc0.Init.Offset = 0;
    hc0.Init.RightBitShift = 0;
    HAL_DFSDM_ChannelInit(&hc0);

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

    hf0.Instance = DFSDM1_Filter0;
    hf0.Init.RegularParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
    hf0.Init.RegularParam.FastMode = DISABLE;
    hf0.Init.RegularParam.DmaMode = ENABLE;
    hf0.Init.InjectedParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
    hf0.Init.InjectedParam.ScanMode = DISABLE;
    hf0.Init.InjectedParam.DmaMode = DISABLE;
    hf0.Init.InjectedParam.ExtTrigger = DFSDM_FILTER_EXT_TRIG_TIM1_TRGO;
    hf0.Init.InjectedParam.ExtTriggerEdge = DFSDM_FILTER_EXT_TRIG_BOTH_EDGES;
    hf0.Init.FilterParam.SincOrder = DFSDM_FILTER_SINC3_ORDER;
    hf0.Init.FilterParam.Oversampling = PDM_OSR;
    hf0.Init.FilterParam.IntOversampling = 1;
    HAL_DFSDM_FilterInit(&hf0);
    HAL_DFSDM_FilterConfigRegChannel(&hf0, DFSDM_CHANNEL_0, DFSDM_CONTINUOUS_CONV_ON);

    pdm_dbg_step(11);
    DMA1_CSELR->CSELR &= ~(DMA_CSELR_C4S | DMA_CSELR_C5S);
    pdm_dbg_step(12);
    PDM_DMA_CH->CPAR = (uint32_t)&DFSDM1_Filter1->FLTRDATAR + 2u;
    PDM_DMA2_CH->CPAR = (uint32_t)&DFSDM1_Filter0->FLTRDATAR + 2u;
    pdm_dbg_step(13);
    PDM_DMA_CH->CMAR = (uint32_t)pdm_dma_buf;
    PDM_DMA2_CH->CMAR = (uint32_t)pdm_dma_buf2;
    pdm_dbg_step(14);
}

void pdm_start(void)
{
    const uint32_t primask = pdm_irq_lock();
    HAL_NVIC_DisableIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Channel5_IRQn);

    overruns = 0;
    samples = 0;
    pdm_dma_pos = 0;
    pdm_dma_pos2 = 0;
    memset((void*)&pdm_dma_stats, 0, sizeof(pdm_dma_stats));
    pdm_dma_completed_a = 0;
    pdm_dma_completed_b = 0;

    DMA1_CSELR->CSELR &= ~(DMA_CSELR_C4S | DMA_CSELR_C5S);
    PDM_DMA_CH->CCR = 0;
    PDM_DMA_CH->CNDTR = PDM_DMA_BUF_SAMPLES;
    DMA1->IFCR = DMA_IFCR_CTCIF4 | DMA_IFCR_CTCIF5;
    PDM_DMA_CH->CCR = DMA_CCR_EN_Msk | DMA_CCR_CIRC_Msk | DMA_CCR_MINC_Msk | DMA_CCR_TCIE
                    | DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0 | DMA_CCR_PL_1;
    PDM_DMA2_CH->CCR = 0;
    PDM_DMA2_CH->CNDTR = PDM_DMA_BUF_SAMPLES;
    PDM_DMA2_CH->CCR = DMA_CCR_EN_Msk | DMA_CCR_CIRC_Msk | DMA_CCR_MINC_Msk | DMA_CCR_TCIE
                     | DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0 | DMA_CCR_PL_1;

    HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 5, 0);
    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
    pdm_irq_unlock(primask);

    hf.Instance->FLTICR = DFSDM_FLTICR_CLRROVRF | DFSDM_FLTICR_CLRJOVRF;
    hf0.Instance->FLTICR = DFSDM_FLTICR_CLRROVRF | DFSDM_FLTICR_CLRJOVRF;
    start_ret = HAL_DFSDM_FilterRegularStart(&hf);
    if (start_ret == HAL_OK) start_ret = HAL_DFSDM_FilterRegularStart(&hf0);
}

void pdm_stop(void)
{
    HAL_DFSDM_FilterRegularStop(&hf);
    HAL_DFSDM_FilterRegularStop(&hf0);
    const uint32_t primask = pdm_irq_lock();
    PDM_DMA_CH->CCR &= ~DMA_CCR_EN_Msk;
    PDM_DMA2_CH->CCR &= ~DMA_CCR_EN_Msk;
    HAL_NVIC_DisableIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Channel5_IRQn);
    DMA1->IFCR = DMA_IFCR_CTCIF4 | DMA_IFCR_CTCIF5;
    pdm_irq_unlock(primask);
}

static int16_t pdm_scale_raw(int16_t sample)
{
    const int32_t scaled = (int32_t)sample * (int32_t)PDM_GAIN;
    if (scaled > 32767) return 32767;
    if (scaled < -32768) return -32768;
    return (int16_t)scaled;
}

int pdm_dma_read_dual(int16_t* channel_a, int16_t* channel_b, int max_samples)
{
    if (channel_a == NULL || channel_b == NULL || max_samples <= 0) return 0;

    const uint32_t primask = pdm_irq_lock();
    pdm_dma_stats.produced_a = pdm_dma_snapshot_produced(
        PDM_DMA_CH, &pdm_dma_completed_a, DMA_ISR_TCIF5, DMA_IFCR_CTCIF5);
    pdm_dma_stats.produced_b = pdm_dma_snapshot_produced(
        PDM_DMA2_CH, &pdm_dma_completed_b, DMA_ISR_TCIF4, DMA_IFCR_CTCIF4);
    pdm_irq_unlock(primask);

    const uint32_t previous_common = pdm_dma_stats.consumed_a;
    PdmRingRecovery recovery = pdm_ring_recover_pair(
        pdm_dma_stats.produced_a, pdm_dma_stats.produced_b, previous_common,
        PDM_DMA_BUF_SAMPLES, PDM_DMA_BUF_FREE_MARGIN);

    if (recovery.overrun) {
        if (pdm_ring_has_lapped(pdm_dma_stats.produced_a, previous_common,
                                PDM_DMA_BUF_SAMPLES)) pdm_dma_stats.overruns_a++;
        if (pdm_ring_has_lapped(pdm_dma_stats.produced_b, previous_common,
                                PDM_DMA_BUF_SAMPLES)) pdm_dma_stats.overruns_b++;
        pdm_dma_stats.paired_overruns++;
        overruns++;
    }

    pdm_dma_stats.consumed_a = recovery.common_consumed;
    pdm_dma_stats.consumed_b = recovery.common_consumed;
    pdm_dma_pos = pdm_ring_index(recovery.common_consumed, PDM_DMA_BUF_SAMPLES);
    pdm_dma_pos2 = pdm_dma_pos;

    uint32_t take = recovery.available;
    if (take > (uint32_t)max_samples) take = (uint32_t)max_samples;
    for (uint32_t i = 0; i < take; i++) {
        channel_a[i] = pdm_scale_raw(pdm_dma_buf[(pdm_dma_pos + i) &
                                                  (PDM_DMA_BUF_SAMPLES - 1u)]);
        channel_b[i] = pdm_scale_raw(pdm_dma_buf2[(pdm_dma_pos2 + i) &
                                                   (PDM_DMA_BUF_SAMPLES - 1u)]);
    }

    pdm_dma_stats.consumed_a += take;
    pdm_dma_stats.consumed_b += take;
    pdm_dma_pos = pdm_ring_index(pdm_dma_stats.consumed_a, PDM_DMA_BUF_SAMPLES);
    pdm_dma_pos2 = pdm_dma_pos;
    pdm_dma_stats.output_count += take;
    return (int)take;
}

int pdm_dma_read(int16_t* mono, int max_samples)
{
    static int16_t discard_b[128];
    int total = 0;
    while (total < max_samples) {
        int take = (int)pdm_compat_read_chunk((uint32_t)(max_samples - total));
        int n = pdm_dma_read_dual(mono + total, discard_b, take);
        if (n <= 0) break;
        total += n;
    }
    return total;
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

void DMA1_Channel5_IRQHandler(void)
{
    pdm_dma_account_pending_tc(&pdm_dma_completed_a, DMA_ISR_TCIF5, DMA_IFCR_CTCIF5);
}

void DMA1_Channel4_IRQHandler(void)
{
    pdm_dma_account_pending_tc(&pdm_dma_completed_b, DMA_ISR_TCIF4, DMA_IFCR_CTCIF4);
}

uint32_t pdm_overruns(void) { return overruns; }
PdmCaptureStats pdm_capture_stats(void)
{
    const uint32_t primask = pdm_irq_lock();
    pdm_dma_stats.produced_a = pdm_dma_snapshot_produced(
        PDM_DMA_CH, &pdm_dma_completed_a, DMA_ISR_TCIF5, DMA_IFCR_CTCIF5);
    pdm_dma_stats.produced_b = pdm_dma_snapshot_produced(
        PDM_DMA2_CH, &pdm_dma_completed_b, DMA_ISR_TCIF4, DMA_IFCR_CTCIF4);
    PdmCaptureStats stats;
    stats.produced_a = pdm_dma_stats.produced_a;
    stats.produced_b = pdm_dma_stats.produced_b;
    stats.consumed_a = pdm_dma_stats.consumed_a;
    stats.consumed_b = pdm_dma_stats.consumed_b;
    stats.overruns_a = pdm_dma_stats.overruns_a;
    stats.overruns_b = pdm_dma_stats.overruns_b;
    stats.paired_overruns = pdm_dma_stats.paired_overruns;
    stats.output_count = pdm_dma_stats.output_count;
    pdm_irq_unlock(primask);
    return stats;
}
uint32_t pdm_sample_count(void) { return samples; }
int pdm_start_result(void) { return start_ret; }
uint32_t pdm_isr_count(void) { return 0; }
