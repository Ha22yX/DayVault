#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool     sd_init(void);
bool     sd_read_sectors(uint32_t lba, uint8_t* buf, uint32_t count);
bool     sd_write_sectors(uint32_t lba, const uint8_t* buf, uint32_t count);
uint64_t sd_capacity_bytes(void);

typedef struct {
    uint32_t single_read_commands;
    uint32_t multi_read_commands;
    uint32_t multi_read_fallbacks;
    uint32_t sectors_read;
    uint32_t read_errors;
} sd_stats_t;

void sd_reset_stats(void);
void sd_get_stats(sd_stats_t* out);

#ifdef __cplusplus
}
#endif
