#pragma once

#include <stdint.h>

static inline uint32_t pdm_clock_hz(uint32_t source_hz, uint32_t divider) {
    return divider ? (source_hz / divider) : 0u;
}

static inline uint32_t pdm_pcm_rate_hz(uint32_t source_hz, uint32_t divider,
                                       uint32_t filter_osr) {
    const uint32_t clock = pdm_clock_hz(source_hz, divider);
    return filter_osr ? ((clock + filter_osr / 2u) / filter_osr) : 0u;
}

static inline bool pdm_clock_is_sph0655_normal(uint32_t source_hz,
                                                uint32_t divider) {
    const uint32_t clock = pdm_clock_hz(source_hz, divider);
    return clock >= 1100000u && clock <= 4800000u;
}
