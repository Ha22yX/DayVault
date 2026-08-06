#include "segmgr.h"
#include <stdio.h>

void segmgr_init(segmgr_t *m, uint32_t segment_seconds, uint32_t prealloc_bytes)
{
    m->segment_seconds = segment_seconds;
    m->prealloc_bytes = prealloc_bytes;
    m->seq = 0;
    m->open = 0;
}

int segmgr_should_rotate(const segmgr_t *m, const utc_time_t *now)
{
    if (!m->open)
        return 0;
    return timeutil_diff_seconds(now, &m->opened_at) >= (int64_t)m->segment_seconds;
}

void segmgr_open(segmgr_t *m, const utc_time_t *now)
{
    m->opened_at = *now;
    m->open = 1;
    m->seq = (uint16_t)((m->seq % 9999u) + 1u);
}

void segmgr_close(segmgr_t *m)
{
    m->open = 0;
}

void segmgr_build_name(const segmgr_t *m, const utc_time_t *now, char *out, size_t cap)
{
    char ts[32];
    timeutil_format_ts(now, ts, sizeof(ts));
    snprintf(out, cap, "%s_%04u.wav", ts, m->seq);
}

uint16_t segmgr_seq(const segmgr_t *m)
{
    return m->seq;
}
