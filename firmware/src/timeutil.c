#include "timeutil.h"
#include <stdio.h>

int timeutil_is_leap(uint16_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint8_t timeutil_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && timeutil_is_leap(year))
        return 29;
    return days[month - 1];
}

int timeutil_is_valid(const utc_time_t *t)
{
    if (t->year < 2000 || t->year > 2099)
        return 0;
    if (t->month < 1 || t->month > 12)
        return 0;
    if (t->day < 1 || t->day > timeutil_days_in_month(t->year, t->month))
        return 0;
    if (t->hour > 23 || t->minute > 59 || t->second > 59)
        return 0;
    return 1;
}

uint32_t timeutil_to_epoch_days(const utc_time_t *t)
{
    uint16_t y = t->year;
    uint8_t m = t->month;
    if (m <= 2)
    {
        y--;
        m += 12;
    }
    return (uint32_t)y * 365u + (uint32_t)(y / 4) - (uint32_t)(y / 100)
         + (uint32_t)(y / 400) + (uint32_t)((m + 1) * 153u / 5u)
         + (uint32_t)t->day - 719591u;
}

uint64_t timeutil_to_epoch_seconds(const utc_time_t *t)
{
    uint64_t s = (uint64_t)timeutil_to_epoch_days(t) * 86400ull;
    s += (uint32_t)t->hour * 3600u;
    s += (uint32_t)t->minute * 60u;
    s += t->second;
    return s;
}

int64_t timeutil_diff_seconds(const utc_time_t *a, const utc_time_t *b)
{
    int64_t sa = (int64_t)timeutil_to_epoch_seconds(a);
    int64_t sb = (int64_t)timeutil_to_epoch_seconds(b);
    return sa - sb;
}

void timeutil_format_ts(const utc_time_t *t, char *out, size_t cap)
{
    snprintf(out, cap, "%04u%02u%02uT%02u%02u%02uZ",
             t->year, t->month, t->day, t->hour, t->minute, t->second);
}

void timeutil_format_iso(const utc_time_t *t, char *out, size_t cap)
{
    snprintf(out, cap, "%04u-%02u-%02uT%02u:%02u:%02uZ",
             t->year, t->month, t->day, t->hour, t->minute, t->second);
}

void timeutil_make_day_path(const utc_time_t *t, char *out, size_t cap)
{
    snprintf(out, cap, "DAYVAULT/%04u/%02u/%02u", t->year, t->month, t->day);
}

void timeutil_make_unsynced_path(uint32_t boot_counter, char *out, size_t cap)
{
    snprintf(out, cap, "DAYVAULT/UNSYNCED/BOOT%04lu", (unsigned long)boot_counter);
}
