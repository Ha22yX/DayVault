#include "unity.h"
#include <string.h>
#include "wav.h"

static wav_config_t cfg;

void setUp(void)
{
    memset(&cfg, 0, sizeof(cfg));
    cfg.format = WAV_PCM_FORMAT;
    cfg.sample_rate = 16000;
    cfg.channels = 2;
    cfg.bits = 16;
    cfg.block_align = 4;
    cfg.byte_rate = 64000;
}
void tearDown(void)
{
}

void test_stereo_header_size_is_44(void)
{
    TEST_ASSERT_EQUAL_UINT(44u, wav_header_size(&cfg));
}

void test_stereo_header_golden_bytes(void)
{
    uint8_t hdr[44];
    uint8_t exp[44] = {
        'R','I','F','F',  0x24,0,0,0,
        'W','A','V','E',
        'f','m','t',' ',  0x10,0,0,0,
        0x01,0, 0x02,0, 0x80,0x3E,0,0, 0x00,0xFA,0,0, 0x04,0, 0x10,0,
        'd','a','t','a',  0,0,0,0
    };
    wav_build_header(hdr, &cfg, 0);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, hdr, 44);
}

void test_patch_sizes_after_write(void)
{
    uint8_t hdr[44];
    wav_build_header(hdr, &cfg, 0);
    wav_patch_sizes(hdr, &cfg, 1000u);
    /* RIFF size = 36 + data = 1036 = 0x040C, data size = 1000 = 0x03E8 */
    TEST_ASSERT_EQUAL_UINT8(0x0C, hdr[4]);
    TEST_ASSERT_EQUAL_UINT8(0x04, hdr[5]);
    TEST_ASSERT_EQUAL_UINT8(0x00, hdr[6]);
    TEST_ASSERT_EQUAL_UINT8(0x00, hdr[7]);
    TEST_ASSERT_EQUAL_UINT8(0xE8, hdr[40]);
    TEST_ASSERT_EQUAL_UINT8(0x03, hdr[41]);
    TEST_ASSERT_EQUAL_UINT8(0x00, hdr[42]);
    TEST_ASSERT_EQUAL_UINT8(0x00, hdr[43]);
}

void test_pcm_bytes_to_samples_stereo(void)
{
    /* 1000 bytes / 4 bytes-per-sample = 250 samples (stereo frame count) */
    TEST_ASSERT_EQUAL_UINT(250u, wav_pcm_bytes_to_samples(1000u, &cfg));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stereo_header_size_is_44);
    RUN_TEST(test_stereo_header_golden_bytes);
    RUN_TEST(test_patch_sizes_after_write);
    RUN_TEST(test_pcm_bytes_to_samples_stereo);
    return UNITY_END();
}
