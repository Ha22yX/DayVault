#ifndef DAYVAULT_TIMEUTIL_H
#define DAYVAULT_TIMEUTIL_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint16_t year;   /* 2000-2099 */
    uint8_t month;   /* 1-12 */
    uint8_t day;     /* 1-31 */
    uint8_t hour;    /* 0-23 */
    uint8_t minute;  /* 0-59 */
    uint8_t second;  /* 0-59 */
} utc_time_t;

int timeutil_is_leap(uint16_t year);
uint8_t timeutil_days_in_month(uint16_t year, uint8_t month);
int timeutil_is_valid(const utc_time_t *t);
uint32_t timeutil_to_epoch_days(const utc_time_t *t);
uint64_t timeutil_to_epoch_seconds(const utc_time_t *t);
int64_t timeutil_diff_seconds(const utc_time_t *a, const utc_time_t *b);
void timeutil_format_ts(const utc_time_t *t, char *out, size_t cap);
void timeutil_format_iso(const utc_time_t *t, char *out, size_t cap);
void timeutil_make_day_path(const utc_time_t *t, char *out, size_t cap);
void timeutil_make_unsynced_path(uint32_t boot_counter, char *out, size_t cap);

#endif
