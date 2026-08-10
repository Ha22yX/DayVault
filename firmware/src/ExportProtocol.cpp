#include "ExportProtocol.h"

#include <stdio.h>
#include <string.h>

static bool filename_char_allowed(char value)
{
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') ||
           value == '-' || value == '_' || value == '.';
}

bool export_parse_get2(const char* line, ExportRequest* request)
{
    if (line == nullptr || request == nullptr || strncmp(line, "GET2 ", 5) != 0) {
        return false;
    }

    const char* cursor = line + 5;
    if (*cursor < '0' || *cursor > '9') return false;
    uint32_t offset = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        const uint32_t digit = (uint32_t)(*cursor - '0');
        if (offset > (0xFFFFFFFFu - digit) / 10u) return false;
        offset = offset * 10u + digit;
        ++cursor;
    }
    if (*cursor != ' ') return false;
    while (*cursor == ' ') ++cursor;
    if (*cursor == '\0' || *cursor == '.' || strstr(cursor, "..") != nullptr) {
        return false;
    }

    size_t length = 0;
    while (cursor[length] != '\0') {
        if (!filename_char_allowed(cursor[length])) return false;
        ++length;
        if (length >= EXPORT_FILENAME_BYTES) return false;
    }

    request->offset = offset;
    memcpy(request->filename, cursor, length + 1);
    return true;
}

size_t export_format_completion(char* output, size_t capacity,
                                const char* layer, uint32_t sent,
                                uint32_t elapsed_ms, uint32_t crc32)
{
    if (output == nullptr || capacity == 0u || layer == nullptr) return 0u;
    if (elapsed_ms == 0u) elapsed_ms = 1u;
    const uint32_t kib_per_second = (uint32_t)(
        ((uint64_t)sent * 1000u) / ((uint64_t)elapsed_ms * 1024u));
    const int length = snprintf(
        output, capacity,
        "GET2END sent=%lu crc32=%08lX\r\n"
        "BENCH %s bytes=%lu ms=%lu kib_s=%lu crc32=%08lX\r\n",
        (unsigned long)sent, (unsigned long)crc32, layer,
        (unsigned long)sent, (unsigned long)elapsed_ms,
        (unsigned long)kib_per_second, (unsigned long)crc32);
    if (length < 0 || (size_t)length >= capacity) {
        output[0] = '\0';
        return 0u;
    }
    return (size_t)length;
}
