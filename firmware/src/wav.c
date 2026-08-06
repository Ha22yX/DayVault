#include "wav.h"
#include <string.h>

static void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

size_t wav_header_size(const wav_config_t *cfg)
{
    return cfg->format == WAV_IMA_ADPCM_FORMAT ? 48u : 44u;
}

void wav_build_header(uint8_t *hdr, const wav_config_t *cfg, uint32_t data_bytes)
{
    size_t hsz = wav_header_size(cfg);
    memset(hdr, 0, hsz);
    hdr[0] = 'R'; hdr[1] = 'I'; hdr[2] = 'F'; hdr[3] = 'F';
    put_le32(hdr + 4, (uint32_t)(hsz - 8u) + data_bytes);
    hdr[8] = 'W'; hdr[9] = 'A'; hdr[10] = 'V'; hdr[11] = 'E';
    hdr[12] = 'f'; hdr[13] = 'm'; hdr[14] = 't'; hdr[15] = ' ';
    if (cfg->format == WAV_IMA_ADPCM_FORMAT)
        put_le32(hdr + 16, 18u);   /* fmt chunk size incl. cbSize */
    else
        put_le32(hdr + 16, 16u);
    put_le16(hdr + 20, cfg->format);
    put_le16(hdr + 22, cfg->channels);
    put_le32(hdr + 24, cfg->sample_rate);
    put_le32(hdr + 28, cfg->byte_rate);
    put_le16(hdr + 32, cfg->block_align);
    put_le16(hdr + 34, cfg->bits);
    if (cfg->format == WAV_IMA_ADPCM_FORMAT)
    {
        put_le16(hdr + 36, 2u);                        /* cbSize */
        put_le16(hdr + 38, WAV_ADPCM_SAMPLES_PER_BLOCK);
        hdr[40] = 'd'; hdr[41] = 'a'; hdr[42] = 't'; hdr[43] = 'a';
        put_le32(hdr + 44, data_bytes);
    }
    else
    {
        hdr[36] = 'd'; hdr[37] = 'a'; hdr[38] = 't'; hdr[39] = 'a';
        put_le32(hdr + 40, data_bytes);
    }
}

void wav_patch_sizes(uint8_t *hdr, const wav_config_t *cfg, uint32_t data_bytes)
{
    size_t hsz = wav_header_size(cfg);
    put_le32(hdr + 4, (uint32_t)(hsz - 8u) + data_bytes);
    if (cfg->format == WAV_IMA_ADPCM_FORMAT)
        put_le32(hdr + 44, data_bytes);
    else
        put_le32(hdr + 40, data_bytes);
}

uint32_t wav_pcm_bytes_to_samples(uint32_t data_bytes, const wav_config_t *cfg)
{
    return data_bytes / cfg->block_align;
}

uint32_t wav_adpcm_bytes_to_samples(uint32_t data_bytes, const wav_config_t *cfg)
{
    uint32_t blocks = data_bytes / cfg->block_align;
    return blocks * WAV_ADPCM_SAMPLES_PER_BLOCK;
}
