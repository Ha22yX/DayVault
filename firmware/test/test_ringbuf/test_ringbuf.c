#include "unity.h"
#include <string.h>
#include "ringbuf.h"

static uint8_t backing[16];
static ringbuf_t rb;

void setUp(void)
{
    ringbuf_init(&rb, backing, sizeof(backing));
}
void tearDown(void)
{
}

void test_empty_state(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, ringbuf_used(&rb));
    TEST_ASSERT_EQUAL_UINT(16u, ringbuf_free(&rb));
}

void test_write_read_roundtrip(void)
{
    uint8_t in[5] = {1, 2, 3, 4, 5};
    uint8_t out[5] = {0};
    TEST_ASSERT_EQUAL_UINT(5u, ringbuf_write(&rb, in, 5));
    TEST_ASSERT_EQUAL_UINT(5u, ringbuf_read(&rb, out, 5));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 5);
    TEST_ASSERT_EQUAL_UINT(0u, ringbuf_used(&rb));
}

void test_overflow_drops(void)
{
    uint8_t in[32] = {0};
    memset(in, 0xAB, sizeof(in));
    TEST_ASSERT_EQUAL_UINT(16u, ringbuf_write(&rb, in, 32));
    TEST_ASSERT_EQUAL_UINT(16u, ringbuf_used(&rb));
}

void test_wraparound(void)
{
    uint8_t in[12];
    uint8_t out[4];
    uint8_t expected[4] = {4, 5, 6, 7};
    uint8_t i;
    for (i = 0; i < 12; i++)
        in[i] = i;
    TEST_ASSERT_EQUAL_UINT(12u, ringbuf_write(&rb, in, 12));
    TEST_ASSERT_EQUAL_UINT(4u, ringbuf_read(&rb, out, 4));
    TEST_ASSERT_EQUAL_UINT(6u, ringbuf_write(&rb, in, 6));
    TEST_ASSERT_EQUAL_UINT(4u, ringbuf_read(&rb, out, 4));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 4);
}

void test_read_empty_returns_zero(void)
{
    uint8_t out[4];
    TEST_ASSERT_EQUAL_UINT(0u, ringbuf_read(&rb, out, 4));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_state);
    RUN_TEST(test_write_read_roundtrip);
    RUN_TEST(test_overflow_drops);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_read_empty_returns_zero);
    return UNITY_END();
}
