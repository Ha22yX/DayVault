#ifndef DAYVAULT_HW_SPI_SD_H
#define DAYVAULT_HW_SPI_SD_H

#include <stdint.h>

int hw_sd_init(void);
int hw_sd_read_sectors(uint32_t lba, uint8_t *buf, uint32_t count, int raw);
int hw_sd_write_sectors(uint32_t lba, const uint8_t *buf, uint32_t count, int raw);
uint64_t hw_sd_capacity_bytes(void);

#endif
