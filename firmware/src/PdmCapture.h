#pragma once
#include "RingBuf.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void     pdm_init(RingBuf* sink);
void     pdm_start(void);
void     pdm_stop(void);
int      pdm_try_read_sample(int16_t* out);
uint32_t pdm_overruns(void);
uint32_t pdm_sample_count(void);
int      pdm_start_result(void);
int      pdm_itst_start(void);
int      pdm_isr_count_now(void);
int      pdm_dma_read(int16_t* buf, int max);
void     pdm_dbg_step(uint32_t v);
int      pdm_isr_count_now(void);

#ifdef __cplusplus
}
#endif
