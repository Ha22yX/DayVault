#include "OpusArena.h"

#include <stdint.h>

namespace {

const size_t kAlignment = 8u;
uint8_t* g_arena = nullptr;
size_t g_capacity = 0u;
size_t g_used = 0u;
uint8_t* g_last_allocation = nullptr;
size_t g_last_size = 0u;

bool round_up(size_t size, size_t* rounded)
{
    if (size > SIZE_MAX - (kAlignment - 1u)) return false;
    *rounded = (size + (kAlignment - 1u)) & ~(kAlignment - 1u);
    return true;
}

}  // namespace

extern "C" void opus_arena_begin(void* memory, size_t size)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(memory);
    const uintptr_t aligned_address = (address + (kAlignment - 1u)) & ~(uintptr_t)(kAlignment - 1u);
    const size_t padding = (size_t)(aligned_address - address);

    g_arena = size >= padding ? reinterpret_cast<uint8_t*>(aligned_address) : nullptr;
    g_capacity = size >= padding ? size - padding : 0u;
    g_used = 0u;
    g_last_allocation = nullptr;
    g_last_size = 0u;
}

extern "C" size_t opus_arena_used(void)
{
    return g_used;
}

extern "C" void* dayvault_opus_alloc(size_t size)
{
    size_t rounded = 0u;
    if (g_arena == nullptr || size == 0u || !round_up(size, &rounded)) return nullptr;
    if (rounded > g_capacity - g_used) return nullptr;

    void* allocation = g_arena + g_used;
    g_used += rounded;
    g_last_allocation = static_cast<uint8_t*>(allocation);
    g_last_size = rounded;
    return allocation;
}

extern "C" void* dayvault_opus_realloc(void* ptr, size_t size)
{
    size_t rounded = 0u;
    if (ptr == nullptr || ptr != g_last_allocation || !round_up(size, &rounded)) return nullptr;
    if (rounded <= g_last_size || rounded - g_last_size > g_capacity - g_used) return nullptr;

    g_used += rounded - g_last_size;
    g_last_size = rounded;
    return ptr;
}

extern "C" void dayvault_opus_free(void* ptr)
{
    (void)ptr;
}
