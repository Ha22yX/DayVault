#ifndef DAYVAULT_HW_DFSDM_H
#define DAYVAULT_HW_DFSDM_H

#include <stdint.h>

typedef void (*dfsdm_buffer_cb)(const int16_t *samples, uint16_t count);

void hw_dfsdm_init(void);
void hw_dfsdm_start(void);
void hw_dfsdm_stop(void);
uint32_t hw_dfsdm_overruns(void);
void hw_dfsdm_set_callback(dfsdm_buffer_cb cb);

#endif
