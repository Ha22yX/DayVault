#ifndef DAYVAULT_HW_ADC_H
#define DAYVAULT_HW_ADC_H

#include <stdint.h>

void hw_adc_init(void);
uint16_t hw_adc_read_battery_mv(void);

#endif
