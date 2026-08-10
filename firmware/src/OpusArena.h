#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void opus_arena_begin(void* memory, size_t size);
size_t opus_arena_used(void);
void* dayvault_opus_alloc(size_t size);
void* dayvault_opus_realloc(void* ptr, size_t size);
void dayvault_opus_free(void* ptr);

#ifdef __cplusplus
}
#endif
