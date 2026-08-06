#ifndef DAYVAULT_EVENTLOG_H
#define DAYVAULT_EVENTLOG_H

#include <stddef.h>
#include "timeutil.h"

void eventlog_format(const utc_time_t *t, const char *event, const char *detail, char *out, size_t cap);

#endif
