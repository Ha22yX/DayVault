#pragma once

#include <stdint.h>

#define PDM_DFSDM_SOURCE_HZ 80000000u
#define PDM_PRODUCTION_CKOUT_DIVIDER 50u
#define PDM_PRODUCTION_OSR 100u
#define PDM_CLOCK_HZ(source_hz, divider) \
    ((divider) ? ((source_hz) / (divider)) : 0u)
#define PDM_PCM_RATE_HZ(source_hz, divider, filter_osr) \
    ((filter_osr) ? ((PDM_CLOCK_HZ((source_hz), (divider)) + (filter_osr) / 2u) / (filter_osr)) : 0u)

static inline uint32_t pdm_clock_hz(uint32_t source_hz, uint32_t divider) {
    return PDM_CLOCK_HZ(source_hz, divider);
}

static inline uint32_t pdm_pcm_rate_hz(uint32_t source_hz, uint32_t divider,
                                       uint32_t filter_osr) {
    return PDM_PCM_RATE_HZ(source_hz, divider, filter_osr);
}

static inline bool pdm_clock_is_sph0655_normal(uint32_t source_hz,
                                                uint32_t divider) {
    const uint32_t clock = pdm_clock_hz(source_hz, divider);
    return clock >= 1100000u && clock <= 4800000u;
}
