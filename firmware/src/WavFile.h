#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t format;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits;
    uint16_t block_align;
    uint32_t byte_rate;
} WavConfig;

size_t wav_header_size(const WavConfig* cfg);
void   wav_build_header(uint8_t hdr[44], const WavConfig* cfg, uint32_t data_bytes);
void   wav_patch_sizes(uint8_t hdr[44], uint32_t data_bytes);

#ifdef __cplusplus
}
#endif
