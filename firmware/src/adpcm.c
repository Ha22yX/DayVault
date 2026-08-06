#include "adpcm.h"

static const int16_t step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const int8_t index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8
};

static int16_t clamp_predictor(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static int clamp_index(int v)
{
    if (v < 0) return 0;
    if (v > 88) return 88;
    return v;
}

static uint8_t encode_sample(int16_t sample, int16_t *predictor, int *step_index)
{
    int32_t step = step_table[*step_index];
    int32_t diff = (int32_t)sample - *predictor;
    int32_t temp;
    int32_t diffq;
    uint8_t code;
    if (diff < 0) { code = 8; diff = -diff; }
    else           { code = 0; }

    temp = step;
    if (diff >= temp) { diff -= temp; code |= 4; }
    temp = step >> 1;
    if (diff >= temp) { diff -= temp; code |= 2; }
    temp = step >> 2;
    if (diff >= temp) { diff -= temp; code |= 1; }

    diffq = step >> 3;
    if (code & 4) diffq += step;
    if (code & 2) diffq += step >> 1;
    if (code & 1) diffq += step >> 2;
    *predictor = clamp_predictor((int32_t)*predictor + ((code & 8) ? -diffq : diffq));
    *step_index = clamp_index(*step_index + index_table[code]);
    return code;
}

static int16_t decode_sample(uint8_t code, int16_t *predictor, int *step_index)
{
    int32_t step = step_table[*step_index];
    int32_t diffq = step >> 3;
    if (code & 4) diffq += step;
    if (code & 2) diffq += step >> 1;
    if (code & 1) diffq += step >> 2;
    *predictor = clamp_predictor((int32_t)*predictor + ((code & 8) ? -diffq : diffq));
    *step_index = clamp_index(*step_index + index_table[code]);
    return *predictor;
}

void adpcm_encode_block(const int16_t *pcm, uint8_t *out, uint16_t samples, adpcm_state_t *st)
{
    uint16_t i;
    int16_t predictor = pcm[0];
    int step_index = st->step_index;
    out[0] = (uint8_t)(predictor & 0xFF);
    out[1] = (uint8_t)((uint16_t)predictor >> 8);
    out[2] = (uint8_t)step_index;
    out[3] = 0;
    for (i = 0; i < samples; i += 2)
    {
        uint8_t lo = encode_sample(pcm[i], &predictor, &step_index);
        uint8_t hi = encode_sample(pcm[i + 1], &predictor, &step_index);
        out[4 + i / 2] = (uint8_t)((hi << 4) | (lo & 0x0F));
    }
    st->predictor = predictor;
    st->step_index = (int8_t)step_index;
}

void adpcm_decode_block(const uint8_t *in, int16_t *pcm, uint16_t samples, adpcm_state_t *st)
{
    uint16_t i;
    int16_t predictor = (int16_t)(in[0] | (in[1] << 8));
    int step_index = in[2];
    for (i = 0; i < samples; i += 2)
    {
        uint8_t byte = in[4 + i / 2];
        pcm[i] = decode_sample((uint8_t)(byte & 0x0F), &predictor, &step_index);
        pcm[i + 1] = decode_sample((uint8_t)(byte >> 4), &predictor, &step_index);
    }
    st->predictor = predictor;
    st->step_index = (int8_t)step_index;
}
