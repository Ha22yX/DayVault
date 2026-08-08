#include "PdmCapture.h"
#include "Config.h"
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
static volatile uint32_t pdm_dma_underruns = 0;

static int32_t hpf_y = 0;
static int32_t hpf_x = 0;
static const int32_t hpf_a = 31405;   /* Q15 alpha for ~150 Hz HPF at 21.7 kHz */

static int32_t eq_x1 = 0, eq_x2 = 0, eq_y1 = 0, eq_y2 = 0;
static const int32_t eq_b0 = 22644, eq_b1 = -17428, eq_b2 = 4334;   /* peaking 3 kHz +10 dB Q1 Q14 @21.7k */
static const int32_t eq_a1 = -17428, eq_a2 = 10594;

static int32_t eq2_x1 = 0, eq2_x2 = 0, eq2_y1 = 0, eq2_y2 = 0;
static const int32_t eq2_b0 = 18556, eq2_b1 = -22741, eq2_b2 = 8625;  /* peaking 2 kHz +5 dB Q1 Q14 @21.7k */
static const int32_t eq2_a1 = -22741, eq2_a2 = 10799;

static int32_t sh_x1 = 0, sh_x2 = 0, sh_y1 = 0, sh_y2 = 0;
static const int32_t sh_b0 = 15999, sh_b1 = -30696, sh_b2 = 14749;  /* low shelf 250 Hz -4 dB Q14 @21.7k */
static const int32_t sh_a1 = -30657, sh_a2 = 14403;

static uint32_t agc_peak = 0;
static int32_t agc_gain = 1 << 16;                                /* Q16 unity */
static const uint32_t agc_voice_floor = 700;                      /* pre-AGC peak: speech vs noise */

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
    overruns = 0;
    samples = 0;
    pdm_dma_pos = 0;
    pdm_dma_pos2 = 0;
    pdm_dma_underruns = 0;
    hpf_y = 0;
    hpf_x = 0;
    eq_x1 = 0; eq_x2 = 0; eq_y1 = 0; eq_y2 = 0;
    eq2_x1 = 0; eq2_x2 = 0; eq2_y1 = 0; eq2_y2 = 0;
    sh_x1 = 0; sh_x2 = 0; sh_y1 = 0; sh_y2 = 0;
    agc_peak = 0;
    agc_gain = 1 << 16;

    DMA1_CSELR->CSELR &= ~(DMA_CSELR_C4S | DMA_CSELR_C5S);
    PDM_DMA_CH->CCR = 0;
    PDM_DMA_CH->CNDTR = PDM_DMA_BUF_SAMPLES;
    PDM_DMA_CH->CCR = DMA_CCR_EN_Msk | DMA_CCR_CIRC_Msk | DMA_CCR_MINC_Msk
                    | DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0 | DMA_CCR_PL_1;
    PDM_DMA2_CH->CCR = 0;
    PDM_DMA2_CH->CNDTR = PDM_DMA_BUF_SAMPLES;
    PDM_DMA2_CH->CCR = DMA_CCR_EN_Msk | DMA_CCR_CIRC_Msk | DMA_CCR_MINC_Msk
                     | DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0 | DMA_CCR_PL_1;

    hf.Instance->FLTICR = DFSDM_FLTICR_CLRROVRF | DFSDM_FLTICR_CLRJOVRF;
    hf0.Instance->FLTICR = DFSDM_FLTICR_CLRROVRF | DFSDM_FLTICR_CLRJOVRF;
    start_ret = HAL_DFSDM_FilterRegularStart(&hf);
    if (start_ret == HAL_OK) start_ret = HAL_DFSDM_FilterRegularStart(&hf0);
}

void pdm_stop(void)
{
    HAL_DFSDM_FilterRegularStop(&hf);
    HAL_DFSDM_FilterRegularStop(&hf0);
    PDM_DMA_CH->CCR &= ~DMA_CCR_EN_Msk;
    PDM_DMA2_CH->CCR &= ~DMA_CCR_EN_Msk;
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
            int32_t a = (int32_t)(int16_t)pdm_dma_buf[(pdm_dma_pos + i) & (PDM_DMA_BUF_SAMPLES - 1u)];
            int32_t x = a * (int32_t)PDM_GAIN;
            if (x > 32767) x = 32767;
            if (x < -32768) x = -32768;
            int64_t acc = (int64_t)hpf_a * ((int64_t)hpf_y + x - hpf_x);
            int32_t y = (int32_t)(acc >> 15);
            if (y > 32767) y = 32767;
            if (y < -32768) y = -32768;
            hpf_x = x;
            hpf_y = y;
            x = y;
            int64_t sh = ((int64_t)sh_b0 * x + (int64_t)sh_b1 * sh_x1 + (int64_t)sh_b2 * sh_x2
                          - (int64_t)sh_a1 * sh_y1 - (int64_t)sh_a2 * sh_y2) >> 14;
            sh_x2 = sh_x1; sh_x1 = x;
            sh_y2 = sh_y1; sh_y1 = (int32_t)sh;
            x = (int32_t)sh;
            int64_t eq2 = ((int64_t)eq2_b0 * x + (int64_t)eq2_b1 * eq2_x1 + (int64_t)eq2_b2 * eq2_x2
                           - (int64_t)eq2_a1 * eq2_y1 - (int64_t)eq2_a2 * eq2_y2) >> 14;
            eq2_x2 = eq2_x1; eq2_x1 = x;
            eq2_y2 = eq2_y1; eq2_y1 = (int32_t)eq2;
            x = (int32_t)eq2;
            int64_t eq = ((int64_t)eq_b0 * x + (int64_t)eq_b1 * eq_x1 + (int64_t)eq_b2 * eq_x2
                          - (int64_t)eq_a1 * eq_y1 - (int64_t)eq_a2 * eq_y2) >> 14;
            eq_x2 = eq_x1; eq_x1 = x;
            eq_y2 = eq_y1; eq_y1 = (int32_t)eq;
            y = (int32_t)eq;
            uint32_t pk = (y < 0) ? (uint32_t)(-y) : (uint32_t)y;
            if (pk > agc_peak) agc_peak += (pk - agc_peak) >> 2;
            else agc_peak -= agc_peak >> 12;
            int32_t target = (int32_t)((20000u << 16) / (agc_peak + 200u));
            if (agc_peak < agc_voice_floor) {
                if (target > (2 << 16)) target = (2 << 16);       /* quiet: limit gain, don't pump noise */
            } else {
                if (target > (8 << 16)) target = (8 << 16);       /* speech: make-up for distant voice */
            }
            if (target < (1 << 16)) target = (1 << 16);
            agc_gain += (target - agc_gain) >> 11;                /* slow adaptation, preserves dynamics */
            int32_t out = (int32_t)(((int64_t)y * agc_gain) >> 16);
            if (out > 32767) out = 32767;
            if (out < -32768) out = -32768;
            buf[n++] = (int16_t)out;
        }
        pdm_dma_pos = (pdm_dma_pos + take) & (PDM_DMA_BUF_SAMPLES - 1u);
        pdm_dma_pos2 = (pdm_dma_pos2 + take) & (PDM_DMA_BUF_SAMPLES - 1u);
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
