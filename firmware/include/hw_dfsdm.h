#ifndef DAYVAULT_HW_DFSDM_H
#define DAYVAULT_HW_DFSDM_H

#include "ringbuf.h"

void hw_dfsdm_init(void);
void hw_dfsdm_start(void);
void hw_dfsdm_stop(void);
uint32_t hw_dfsdm_overruns(void);
void hw_dfsdm_set_sink(ringbuf_t *rb);

#endif
