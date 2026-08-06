#ifndef DAYVAULT_SEGMGR_H
#define DAYVAULT_SEGMGR_H

#include <stdint.h>
#include "timeutil.h"

typedef struct
{
    uint32_t segment_seconds;
    uint32_t prealloc_bytes;
    uint16_t seq;
    utc_time_t opened_at;
    int open;
} segmgr_t;

void segmgr_init(segmgr_t *m, uint32_t segment_seconds, uint32_t prealloc_bytes);
int segmgr_should_rotate(const segmgr_t *m, const utc_time_t *now);
void segmgr_open(segmgr_t *m, const utc_time_t *now);
void segmgr_close(segmgr_t *m);
void segmgr_build_name(const segmgr_t *m, const utc_time_t *now, char *out, size_t cap);
uint16_t segmgr_seq(const segmgr_t *m);

#endif
