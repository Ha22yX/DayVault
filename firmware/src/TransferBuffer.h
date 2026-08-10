#pragma once

#include <stddef.h>
#include <stdint.h>

enum {
    TRANSFER_BUFFER_COUNT = 2,
    TRANSFER_BUFFER_BYTES = 16 * 1024,
};

uint8_t* transfer_buffer(size_t index);
size_t transfer_buffer_size(void);
uint8_t* transfer_workspace(void);
size_t transfer_workspace_size(void);
