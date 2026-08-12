#pragma once
#include <stdint.h>
void bat_init(void);
void bat_suspend(void);
uint16_t bat_millivolts(void);   /* e.g. 3650 = 3.65 V at the battery terminal */
uint8_t  bat_percent(void);      /* 0..100, linear 3.0 V..4.2 V */
