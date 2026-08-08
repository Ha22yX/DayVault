#include "WavFile.h"
#include <string.h>

static void put_le16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

size_t wav_header_size(const WavConfig* cfg) { (void)cfg; return 44u; }

void wav_build_header(uint8_t hdr[44], const WavConfig* cfg, uint32_t data_bytes)
{
    memset(hdr, 0, 44);
    hdr[0] = 'R'; hdr[1] = 'I'; hdr[2] = 'F'; hdr[3] = 'F';
    put_le32(hdr + 4, 36u + data_bytes);
    hdr[8] = 'W'; hdr[9] = 'A'; hdr[10] = 'V'; hdr[11] = 'E';
    hdr[12] = 'f'; hdr[13] = 'm'; hdr[14] = 't'; hdr[15] = ' ';
    put_le32(hdr + 16, 16u);
    put_le16(hdr + 20, cfg->format);
    put_le16(hdr + 22, cfg->channels);
    put_le32(hdr + 24, cfg->sample_rate);
    put_le32(hdr + 28, cfg->byte_rate);
    put_le16(hdr + 32, cfg->block_align);
    put_le16(hdr + 34, cfg->bits);
    hdr[36] = 'd'; hdr[37] = 'a'; hdr[38] = 't'; hdr[39] = 'a';
    put_le32(hdr + 40, data_bytes);
}

void wav_patch_sizes(uint8_t hdr[44], uint32_t data_bytes)
{
    put_le32(hdr + 4, 36u + data_bytes);
    put_le32(hdr + 40, data_bytes);
}
