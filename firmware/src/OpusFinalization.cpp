#include "OpusFinalization.h"

#include "DayVaultOpusEncoder.h"

OpusFinalizationPlan opus_finalization_plan(uint16_t lookahead_samples,
                                            bool has_encoded_frame,
                                            uint16_t last_valid_samples)
{
    OpusFinalizationPlan plan = {};
    if (!has_encoded_frame) return plan;

    if (last_valid_samples > kOpusFrameSamples) {
        last_valid_samples = kOpusFrameSamples;
    }
    if (last_valid_samples < kOpusFrameSamples) {
        plan.zeros_already_encoded =
            (uint16_t)(kOpusFrameSamples - last_valid_samples);
    }
    if (plan.zeros_already_encoded < lookahead_samples) {
        plan.remaining_lookahead_samples =
            (uint16_t)(lookahead_samples - plan.zeros_already_encoded);
        plan.additional_zero_frames = (uint16_t)(
            (plan.remaining_lookahead_samples + kOpusFrameSamples - 1u) /
            kOpusFrameSamples);
    }
    return plan;
}
