#pragma once

#include <stdint.h>

struct OpusFinalizationPlan {
    uint16_t zeros_already_encoded;
    uint16_t remaining_lookahead_samples;
    uint16_t additional_zero_frames;
};

OpusFinalizationPlan opus_finalization_plan(uint16_t lookahead_samples,
                                            bool has_encoded_frame,
                                            uint16_t last_valid_samples);
