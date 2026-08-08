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

#ifdef __cplusplus
}
#endif
