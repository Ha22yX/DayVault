#ifndef DAYVAULT_HW_PWR_H
#define DAYVAULT_HW_PWR_H

#include <stdint.h>

void hw_pwr_set_wake_period(uint32_t seconds);
void hw_pwr_enter_standby(void);

#endif
