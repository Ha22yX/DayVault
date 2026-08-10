#pragma once

#include <stddef.h>
#include <stdint.h>

bool recording_format_timestamp_path(char* output, size_t length,
                                     const char* timestamp_stem,
                                     uint16_t collision,
                                     bool include_duration,
                                     uint32_t duration_seconds,
                                     const char* extension);
