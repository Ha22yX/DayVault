#include "TransferBuffer.h"

static uint8_t g_transfer_buffers[TRANSFER_BUFFER_COUNT][TRANSFER_BUFFER_BYTES]
    __attribute__((section(".ram2"), aligned(32)));

static_assert(sizeof(g_transfer_buffers) == 32u * 1024u,
              "transfer buffers must exactly fill SRAM2");

uint8_t* transfer_buffer(size_t index)
{
    if (index >= TRANSFER_BUFFER_COUNT) return nullptr;
    return g_transfer_buffers[index];
}

size_t transfer_buffer_size(void)
{
    return TRANSFER_BUFFER_BYTES;
}
