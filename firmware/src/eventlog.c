#include "eventlog.h"
#include <stdio.h>

void eventlog_format(const utc_time_t *t, const char *event, const char *detail, char *out, size_t cap)
{
    char iso[32];
    timeutil_format_iso(t, iso, sizeof(iso));
    /* NULL detail tolerated as omitted (M7 may pass no detail string) */
    if (detail != NULL && detail[0] != 0)
        snprintf(out, cap, "%s,%s,%s", iso, event, detail);
    else
        snprintf(out, cap, "%s,%s", iso, event);
}
