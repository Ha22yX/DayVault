#include "unity.h"
#include <stdint.h>
#include <string.h>
#include "ringbuf.h"

static uint8_t backing[16];
static ringbuf_t rb;

void setUp(void) { ringbuf_init(&rb, backing, sizeof(backing)); }
void tearDown(void) {}

void test_empty_used_is_zero(void)
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

void test_wraparound(void)
{
    uint8_t in[12] = {0};
    uint8_t out[3] = {0};
    uint8_t expected[3] = {3, 4, 5};
    for (uint8_t i = 0; i < 12; i++) in[i] = i;
    TEST_ASSERT_EQUAL_UINT(12u, ringbuf_write(&rb, in, 12));
    TEST_ASSERT_EQUAL_UINT(3u, ringbuf_read(&rb, out, 3));   /* consume 0..2 */
    TEST_ASSERT_EQUAL_UINT(7u, ringbuf_write(&rb, in, 7));   /* wraps tail */
    TEST_ASSERT_EQUAL_UINT(3u, ringbuf_read(&rb, out, 3));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 3);
}

void test_overflow_drops(void)
{
    uint8_t in[32] = {0};
    memset(in, 0xAB, sizeof(in));
    TEST_ASSERT_EQUAL_UINT(16u, ringbuf_write(&rb, in, 32));
    TEST_ASSERT_EQUAL_UINT(16u, ringbuf_used(&rb));
}

void test_read_empty_returns_zero(void)
{
    uint8_t out[4];
    TEST_ASSERT_EQUAL_UINT(0u, ringbuf_read(&rb, out, 4));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_used_is_zero);
    RUN_TEST(test_write_read_roundtrip);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_overflow_drops);
    RUN_TEST(test_read_empty_returns_zero);
    return UNITY_END();
}
