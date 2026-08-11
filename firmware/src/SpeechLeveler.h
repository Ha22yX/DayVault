#pragma once

#include <stdint.h>

typedef struct {
    uint32_t gain_q16;
    uint32_t applied_gain_q16;
    uint32_t limiter_activations;
    uint32_t prevented_clip_events;
    bool bypass;
    bool speech_present;
} SpeechLevelerStats;

class SpeechLeveler {
public:
    static const uint32_t kQuietRiseStepQ16 = 4096u;
    static const uint32_t kLoudFallStepQ16 = 8192u;
    static const uint32_t kReleaseStepQ16 = 512u;
    static const uint32_t kSpeechHangoverMs = 200u;

    void reset(uint32_t sample_rate);
    void set_bypass(bool bypass);
    void process(const int16_t* in, int16_t* out, uint32_t count, bool speech_present);
    SpeechLevelerStats stats() const;

private:
    uint32_t gain_q16_;
    uint32_t speech_hangover_samples_;
    uint32_t speech_hangover_reload_samples_;
    bool bypass_;
    SpeechLevelerStats stats_;
};
