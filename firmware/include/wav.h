#ifndef DAYVAULT_WAV_H
#define DAYVAULT_WAV_H

#include <stddef.h>
#include <stdint.h>

#define WAV_PCM_FORMAT 1u

typedef struct
{
    uint16_t format;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits;
    uint16_t block_align;
    uint32_t byte_rate;
} wav_config_t;

size_t wav_header_size(const wav_config_t *cfg);
void wav_build_header(uint8_t *hdr, const wav_config_t *cfg, uint32_t data_bytes);
void wav_patch_sizes(uint8_t *hdr, const wav_config_t *cfg, uint32_t data_bytes);
uint32_t wav_pcm_bytes_to_samples(uint32_t data_bytes, const wav_config_t *cfg);

#endif
