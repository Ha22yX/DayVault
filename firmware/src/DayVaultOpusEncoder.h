#pragma once

#include <stddef.h>
#include <stdint.h>

const int kOpusSampleRate = 16000;
const int kOpusFrameSamples = 320;
const int kOpusBitrate = 24000;
const int kOpusMaxPacketBytes = 160;

struct DayVaultOpusStats {
    uint64_t frame_count;
    uint64_t encoded_bytes;
    uint32_t error_count;
};

struct OpusEncoder;

class DayVaultOpusEncoder {
public:
    DayVaultOpusEncoder();
    ~DayVaultOpusEncoder();

    bool begin(void* workspace, size_t bytes);
    int encode(const int16_t pcm[kOpusFrameSamples],
               uint8_t packet[kOpusMaxPacketBytes]);
    uint16_t pre_skip_48k() const;
    size_t workspace_used() const;
    DayVaultOpusStats stats() const;
    void end();

private:
    DayVaultOpusEncoder(const DayVaultOpusEncoder&) = delete;
    DayVaultOpusEncoder& operator=(const DayVaultOpusEncoder&) = delete;

    bool fail_begin();

    OpusEncoder* encoder_;
    uint16_t pre_skip_48k_;
    size_t workspace_used_;
    DayVaultOpusStats stats_;
};
