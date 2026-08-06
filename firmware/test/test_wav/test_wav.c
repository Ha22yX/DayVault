#include "unity.h"
#include <stdint.h>
#include <string.h>
#include "wav.h"

static wav_config_t cfg_pcm;
static wav_config_t cfg_adpcm;

void setUp(void)
{
    cfg_pcm.format = WAV_PCM_FORMAT;
    cfg_pcm.sample_rate = 16000;
    cfg_pcm.channels = 1;
    cfg_pcm.bits = 16;
    cfg_pcm.block_align = 2;
    cfg_pcm.byte_rate = 32000;
    cfg_adpcm.format = WAV_IMA_ADPCM_FORMAT;
    cfg_adpcm.sample_rate = 16000;
    cfg_adpcm.channels = 1;
    cfg_adpcm.bits = 4;
    cfg_adpcm.block_align = 132;
    cfg_adpcm.byte_rate = 8250;
}

void tearDown(void)
{
}

void test_pcm_header_size(void)
{
    TEST_ASSERT_EQUAL_UINT(44u, wav_header_size(&cfg_pcm));
    TEST_ASSERT_EQUAL_UINT(48u, wav_header_size(&cfg_adpcm));
}

void test_pcm_header_golden_bytes(void)
{
    uint8_t hdr[44];
    uint8_t exp[44] = {
        'R','I','F','F',  0x24,0,0,0,
        'W','A','V','E',
        'f','m','t',' ',  0x10,0,0,0,
        0x01,0, 0x01,0, 0x80,0x3E,0,0, 0x00,0x7D,0,0, 0x02,0, 0x10,0,
        'd','a','t','a',  0,0,0,0
    };
    wav_build_header(hdr, &cfg_pcm, 0);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, hdr, 44);
}

void test_pcm_patch_sizes(void)
{
    uint8_t hdr[44];
    uint8_t exp_riff[4] = {0x0C, 0x04, 0, 0};    /* 44-8+1000 = 1036 = 0x040C */
    uint8_t exp_data[4] = {0xE8, 0x03, 0, 0};    /* 1000 */
    wav_build_header(hdr, &cfg_pcm, 0);
    wav_patch_sizes(hdr, &cfg_pcm, 1000);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp_riff, hdr + 4, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp_data, hdr + 40, 4);
}

void test_pcm_bytes_to_samples(void)
{
    TEST_ASSERT_EQUAL_UINT(500u, wav_pcm_bytes_to_samples(1000, &cfg_pcm));
}

void test_adpcm_bytes_to_samples(void)
{
    /* 132-byte block = 256 samples; 660 bytes = 5 blocks = 1280 samples */
    TEST_ASSERT_EQUAL_UINT(1280u, wav_adpcm_bytes_to_samples(660, &cfg_adpcm));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pcm_header_size);
    RUN_TEST(test_pcm_header_golden_bytes);
    RUN_TEST(test_pcm_patch_sizes);
    RUN_TEST(test_pcm_bytes_to_samples);
    RUN_TEST(test_adpcm_bytes_to_samples);
    return UNITY_END();
}
