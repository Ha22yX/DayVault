#ifndef DAYVAULT_HW_RTC_H
#define DAYVAULT_HW_RTC_H

#include "timeutil.h"
#include <stdint.h>

int hw_rtc_init(void);
int hw_rtc_set_time(const utc_time_t *t);
int hw_rtc_get_time(utc_time_t *t);
int hw_rtc_is_time_valid(void);
void hw_rtc_mark_time_valid(void);
uint32_t hw_rtc_boot_counter(void);
void hw_rtc_bump_boot_counter(void);

#endif
