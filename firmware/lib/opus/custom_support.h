#ifndef DAYVAULT_OPUS_CUSTOM_SUPPORT_H
#define DAYVAULT_OPUS_CUSTOM_SUPPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* dayvault_opus_alloc(size_t size);
void* dayvault_opus_realloc(void* ptr, size_t size);
void dayvault_opus_free(void* ptr);

#ifdef __cplusplus
}
#endif

#define OVERRIDE_OPUS_ALLOC
#define opus_alloc(size) dayvault_opus_alloc(size)
#define OVERRIDE_OPUS_REALLOC
#define opus_realloc(ptr, size) dayvault_opus_realloc(ptr, size)
#define OVERRIDE_OPUS_FREE
#define opus_free(ptr) dayvault_opus_free(ptr)

#endif
