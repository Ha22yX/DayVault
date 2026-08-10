#include "RecordingName.h"

#include <stdio.h>

bool recording_format_timestamp_path(char* output, size_t length,
                                     const char* timestamp_stem,
                                     uint16_t collision,
                                     bool include_duration,
                                     uint32_t duration_seconds,
                                     const char* extension)
{
    if (output == nullptr || length == 0u || timestamp_stem == nullptr ||
        extension == nullptr) {
        return false;
    }

    int written = 0;
    if (!include_duration) {
        written = collision == 0u
            ? snprintf(output, length, "0:/REC-%s.%s", timestamp_stem, extension)
            : snprintf(output, length, "0:/REC-%s_%u.%s", timestamp_stem,
                       (unsigned)collision, extension);
    } else {
        const uint32_t hours = duration_seconds / 3600u;
        const uint32_t minutes = (duration_seconds / 60u) % 60u;
        const uint32_t seconds = duration_seconds % 60u;
        if (hours > 0u) {
            written = collision == 0u
                ? snprintf(output, length, "0:/REC-%s_%luh%02lum%02lus.%s",
                           timestamp_stem, (unsigned long)hours,
                           (unsigned long)minutes, (unsigned long)seconds, extension)
                : snprintf(output, length, "0:/REC-%s_%u_%luh%02lum%02lus.%s",
                           timestamp_stem, (unsigned)collision, (unsigned long)hours,
                           (unsigned long)minutes, (unsigned long)seconds, extension);
        } else {
            written = collision == 0u
                ? snprintf(output, length, "0:/REC-%s_%lum%02lus.%s", timestamp_stem,
                           (unsigned long)minutes, (unsigned long)seconds, extension)
                : snprintf(output, length, "0:/REC-%s_%u_%lum%02lus.%s",
                           timestamp_stem, (unsigned)collision, (unsigned long)minutes,
                           (unsigned long)seconds, extension);
        }
    }

    return written >= 0 && (size_t)written < length;
}
