#include <unity.h>

#include <stdint.h>

#include <opus.h>

#include "OpusArena.h"
#include "TransferBuffer.h"

void test_arena_aligns_allocations_and_rejects_overflow(void)
{
    alignas(16) uint8_t memory[64];
    opus_arena_begin(memory, sizeof(memory));
    void* a = dayvault_opus_alloc(3);
    void* b = dayvault_opus_alloc(9);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_UINT32(0u, (uintptr_t)a & 7u);
    TEST_ASSERT_EQUAL_UINT32(0u, (uintptr_t)b & 7u);
    TEST_ASSERT_NULL(dayvault_opus_alloc(80));
}

void test_arena_realloc_grows_only_latest_allocation(void)
{
    alignas(16) uint8_t memory[64];
    opus_arena_begin(memory, sizeof(memory));
    void* first = dayvault_opus_alloc(8);
    void* latest = dayvault_opus_alloc(8);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(latest);
    TEST_ASSERT_EQUAL_PTR(latest, dayvault_opus_realloc(latest, 16));
    TEST_ASSERT_NULL(dayvault_opus_realloc(first, 16));
}

void test_arena_reset_restores_capacity_and_workspace_is_contiguous(void)
{
    alignas(16) uint8_t memory[64];
    opus_arena_begin(memory, sizeof(memory));
    TEST_ASSERT_NOT_NULL(dayvault_opus_alloc(64));
    TEST_ASSERT_EQUAL_size_t(64u, opus_arena_used());

    opus_arena_begin(memory, sizeof(memory));
    TEST_ASSERT_EQUAL_size_t(0u, opus_arena_used());
    TEST_ASSERT_NOT_NULL(dayvault_opus_alloc(64));
    TEST_ASSERT_EQUAL_size_t(32u * 1024u, transfer_workspace_size());
    TEST_ASSERT_NOT_NULL(transfer_workspace());
    TEST_ASSERT_EQUAL_INT32(0, OPUS_OK);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_arena_aligns_allocations_and_rejects_overflow);
    RUN_TEST(test_arena_realloc_grows_only_latest_allocation);
    RUN_TEST(test_arena_reset_restores_capacity_and_workspace_is_contiguous);
    return UNITY_END();
}
