#ifndef DAYVAULT_ADPCM_H
#define DAYVAULT_ADPCM_H

#include <stdint.h>

typedef struct
{
    int16_t predictor;
    int8_t step_index;
} adpcm_state_t;

void adpcm_encode_block(const int16_t *pcm, uint8_t *out, uint16_t samples, adpcm_state_t *st);
void adpcm_decode_block(const uint8_t *in, int16_t *pcm, uint16_t samples, adpcm_state_t *st);

#endif
