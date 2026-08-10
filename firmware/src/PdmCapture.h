#pragma once
#include "RingBuf.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t produced_a;
    uint32_t produced_b;
    uint32_t consumed_a;
    uint32_t consumed_b;
    uint32_t overruns_a;
    uint32_t overruns_b;
    uint32_t paired_overruns;
    uint32_t output_count;
} PdmCaptureStats;

void     pdm_init(RingBuf* sink);
void     pdm_start(void);
void     pdm_stop(void);
int      pdm_try_read_sample(int16_t* out);
uint32_t pdm_overruns(void);
uint32_t pdm_sample_count(void);
int      pdm_start_result(void);
int      pdm_itst_start(void);
int      pdm_isr_count_now(void);
uint32_t pdm_clock_output_hz(void);
uint32_t pdm_output_rate_hz(void);
int      pdm_dma_read_dual(int16_t* channel_a, int16_t* channel_b, int max_samples);
int      pdm_dma_read(int16_t* buf, int max);
PdmCaptureStats pdm_capture_stats(void);
void     pdm_dbg_step(uint32_t v);
void     pdm_dual_diag(int32_t* u1rms, int32_t* u2rms, int32_t* corr, int32_t* n);
void     pdm_raw_diag(int32_t* rms, int32_t* zcr, int32_t* peak, int32_t* n);
int      pdm_isr_count_now(void);

#ifdef __cplusplus
}
#endif
