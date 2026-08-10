#pragma once

#include <stdint.h>

typedef struct {
    uint32_t common_consumed;
    uint32_t available;
    bool overrun;
    bool has_common_data;
} PdmRingRecovery;

static inline uint32_t pdm_ring_produced_from_tc(uint32_t transfer_completes,
                                                  uint32_t buffer_samples,
                                                  uint32_t ndtr) {
    const uint32_t offset = ndtr ? (buffer_samples - ndtr) : buffer_samples;
    return transfer_completes * buffer_samples + offset;
}

static inline uint32_t pdm_ring_index(uint32_t absolute_sample,
                                      uint32_t buffer_samples) {
    return absolute_sample % buffer_samples;
}

static inline bool pdm_ring_has_lapped(uint32_t produced, uint32_t consumed,
                                       uint32_t buffer_samples) {
    return produced - consumed > buffer_samples;
}

static inline uint32_t pdm_ring_pair_available(uint32_t produced_a,
                                                uint32_t produced_b,
                                                uint32_t common_consumed,
                                                uint32_t free_margin) {
    const uint32_t slowest_produced = produced_a < produced_b ? produced_a : produced_b;
    if (slowest_produced <= common_consumed + free_margin) return 0u;
    return slowest_produced - common_consumed - free_margin;
}

static inline PdmRingRecovery pdm_ring_recover_pair(uint32_t produced_a,
                                                     uint32_t produced_b,
                                                     uint32_t common_consumed,
                                                     uint32_t buffer_samples,
                                                     uint32_t free_margin) {
    PdmRingRecovery recovery;
    recovery.overrun = pdm_ring_has_lapped(produced_a, common_consumed,
                                           buffer_samples) ||
                       pdm_ring_has_lapped(produced_b, common_consumed,
                                           buffer_samples);
    recovery.common_consumed = common_consumed;
    recovery.has_common_data = true;

    if (recovery.overrun) {
        const uint32_t fastest_produced = produced_a > produced_b ? produced_a : produced_b;
        const uint32_t slowest_produced = produced_a < produced_b ? produced_a : produced_b;
        const uint32_t capacity = buffer_samples - free_margin;
        const uint32_t oldest_safe = fastest_produced - capacity;
        const uint32_t newest_safe = slowest_produced > free_margin ?
                                     slowest_produced - free_margin : 0u;

        if (oldest_safe > newest_safe) {
            recovery.common_consumed = common_consumed > newest_safe ?
                                        common_consumed : newest_safe;
            recovery.available = 0u;
            recovery.has_common_data = false;
            return recovery;
        }
        recovery.common_consumed = oldest_safe;
    }

    recovery.available = pdm_ring_pair_available(
        produced_a, produced_b, recovery.common_consumed, free_margin);
    return recovery;
}

static inline uint32_t pdm_compat_read_chunk(uint32_t remaining) {
    return remaining > 128u ? 128u : remaining;
}
