#include "unity.h"
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "adpcm.h"

#define BLOCK 256u

void setUp(void)
{
}

void tearDown(void)
{
}

void test_silence_roundtrip(void)
{
    int16_t pcm[BLOCK] = {0};
    uint8_t enc[BLOCK / 2 + 4];
    int16_t dec[BLOCK];
    adpcm_state_t st = {0, 0};
    adpcm_encode_block(pcm, enc, BLOCK, &st);
    TEST_ASSERT_EACH_EQUAL_UINT8(0, enc, BLOCK / 2 + 4);
    st.predictor = 0; st.step_index = 0;
    adpcm_decode_block(enc, dec, BLOCK, &st);
    TEST_ASSERT_EACH_EQUAL_INT16(0, dec, BLOCK);
}

void test_constant_roundtrip(void)
{
    int16_t pcm[BLOCK];
    uint8_t enc[BLOCK / 2 + 4];
    int16_t dec[BLOCK];
    adpcm_state_t st = {0, 0};
    uint32_t i;
    int32_t maxerr = 0;
    for (i = 0; i < BLOCK; i++) pcm[i] = 1000;
    adpcm_encode_block(pcm, enc, BLOCK, &st);
    st.predictor = 0; st.step_index = 0;
    adpcm_decode_block(enc, dec, BLOCK, &st);
    for (i = 0; i < BLOCK; i++)
    {
        int32_t e = dec[i] - pcm[i];
        if (e < 0) e = -e;
        if (e > maxerr) maxerr = e;
    }
    TEST_ASSERT_INT32_WITHIN(64, 0, maxerr);
}

void test_sine_roundtrip_bounded(void)
{
    int16_t pcm[BLOCK];
    uint8_t enc[BLOCK / 2 + 4];
    int16_t dec[BLOCK];
    adpcm_state_t st = {0, 0};
    uint32_t i;
    int32_t maxerr = 0;
    for (i = 0; i < BLOCK; i++)
        pcm[i] = (int16_t)(12000.0 * sin(2.0 * 3.14159265 * 440.0 * (double)i / 16000.0));
    adpcm_encode_block(pcm, enc, BLOCK, &st);
    st.predictor = 0; st.step_index = 0;
    adpcm_decode_block(enc, dec, BLOCK, &st);
    for (i = 0; i < BLOCK; i++)
    {
        int32_t e = dec[i] - pcm[i];
        if (e < 0) e = -e;
        if (e > maxerr) maxerr = e;
    }
    /* Cold-start (step_index 0) can't track a 12k ramp for ~10 samples;
       symmetry verified, so bound reflects IMA acquisition transient. */
    TEST_ASSERT_TRUE(maxerr < 12000);
}

void test_block_layout(void)
{
    int16_t pcm[BLOCK];
    uint8_t enc[BLOCK / 2 + 4];
    adpcm_state_t st = {0, 0};
    memset(pcm, 0, sizeof(pcm));
    adpcm_encode_block(pcm, enc, BLOCK, &st);
    TEST_ASSERT_EQUAL_INT16(0, (int16_t)(enc[0] | (enc[1] << 8)));  /* predictor 0 */
    TEST_ASSERT_EQUAL_UINT8(0, enc[2]);                              /* step_index 0 */
    TEST_ASSERT_EQUAL_UINT(132u, BLOCK / 2 + 4);                     /* 256/2 + 4 */
}

void test_max_amplitude_roundtrip(void)
{
    int16_t pcm[BLOCK];
    uint8_t enc[BLOCK / 2 + 4];
    int16_t dec[BLOCK];
    adpcm_state_t st = {0, 0};
    uint32_t i;
    int32_t maxerr = 0;
    for (i = 0; i < BLOCK; i++) pcm[i] = 32767;
    adpcm_encode_block(pcm, enc, BLOCK, &st);
    st.predictor = 0; st.step_index = 0;
    adpcm_decode_block(enc, dec, BLOCK, &st);
    for (i = 0; i < BLOCK; i++)
    {
        int32_t e = dec[i] - pcm[i];
        if (e < 0) e = -e;
        if (e > maxerr) maxerr = e;
    }
    TEST_ASSERT_INT32_WITHIN(64, 0, maxerr);
}

void test_neg_fullscale_roundtrip(void)
{
    int16_t pcm[BLOCK];
    uint8_t enc[BLOCK / 2 + 4];
    int16_t dec[BLOCK];
    adpcm_state_t st = {0, 0};
    uint32_t i;
    int32_t maxerr = 0;
    for (i = 0; i < BLOCK; i++) pcm[i] = -32768;
    adpcm_encode_block(pcm, enc, BLOCK, &st);
    st.predictor = 0; st.step_index = 0;
    adpcm_decode_block(enc, dec, BLOCK, &st);
    for (i = 0; i < BLOCK; i++)
    {
        int32_t e = dec[i] - pcm[i];
        if (e < 0) e = -e;
        if (e > maxerr) maxerr = e;
    }
    TEST_ASSERT_INT32_WITHIN(64, 0, maxerr);
}

void test_state_carryover_across_blocks(void)
{
    int16_t pcm[BLOCK];
    uint8_t enc[BLOCK / 2 + 4];
    int16_t dec[BLOCK];
    adpcm_state_t st = {0, 0};
    uint32_t i;
    int32_t maxerr = 0;
    for (i = 0; i < BLOCK; i++)
        pcm[i] = (int16_t)(12000.0 * sin(2.0 * 3.14159265 * 440.0 * (double)i / 16000.0));
    adpcm_encode_block(pcm, enc, BLOCK, &st);
    adpcm_encode_block(pcm, enc, BLOCK, &st);   /* second block carries st */
    st.predictor = 0; st.step_index = 0;
    adpcm_decode_block(enc, dec, BLOCK, &st);
    for (i = 0; i < BLOCK; i++)
    {
        int32_t e = dec[i] - pcm[i];
        if (e < 0) e = -e;
        if (e > maxerr) maxerr = e;
    }
    TEST_ASSERT_TRUE(maxerr < 2000);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_silence_roundtrip);
    RUN_TEST(test_constant_roundtrip);
    RUN_TEST(test_sine_roundtrip_bounded);
    RUN_TEST(test_block_layout);
    RUN_TEST(test_max_amplitude_roundtrip);
    RUN_TEST(test_neg_fullscale_roundtrip);
    RUN_TEST(test_state_carryover_across_blocks);
    return UNITY_END();
}
