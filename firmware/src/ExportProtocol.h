#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { EXPORT_FILENAME_BYTES = 64 };

typedef struct {
    uint32_t offset;
    char filename[EXPORT_FILENAME_BYTES];
} ExportRequest;

bool export_parse_get2(const char* line, ExportRequest* request);
size_t export_format_completion(char* output, size_t capacity,
                                const char* layer, uint32_t sent,
                                uint32_t elapsed_ms, uint32_t crc32);
