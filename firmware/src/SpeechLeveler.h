#pragma once

#include <stdint.h>

typedef struct {
    uint32_t gain_q16;
    uint32_t limiter_activations;
    uint32_t prevented_clip_events;
    bool bypass;
    bool speech_present;
} SpeechLevelerStats;

class SpeechLeveler {
public:
    static const uint32_t kQuietRiseStepQ16 = 1024u;
    static const uint32_t kLoudFallStepQ16 = 8192u;
    static const uint32_t kReleaseStepQ16 = 512u;

    void reset(uint32_t sample_rate);
    void set_bypass(bool bypass);
    void process(const int16_t* in, int16_t* out, uint32_t count, bool speech_present);
    SpeechLevelerStats stats() const;

private:
    uint32_t gain_q16_;
    bool bypass_;
    SpeechLevelerStats stats_;
};
