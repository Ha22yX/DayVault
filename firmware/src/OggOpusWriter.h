#pragma once

#include <stddef.h>
#include <stdint.h>

struct OggOpusSink {
    bool (*write)(void* context, const uint8_t* bytes, size_t length);
    void* context;
};

struct OggOpusStats {
    uint64_t page_count;
    uint64_t packet_count;
    uint64_t valid_input_samples;
    uint64_t bytes_written;
};

class OggOpusWriter {
public:
    OggOpusWriter();

    bool begin(OggOpusSink sink, uint8_t* page_buffer, size_t page_buffer_size,
               uint32_t serial, uint16_t pre_skip_48k);
    bool add_packet(const uint8_t* packet, uint16_t packet_size, uint16_t valid_input_samples);
    bool finish();
    OggOpusStats stats() const;

private:
    static const size_t kPageBufferSize = 8192u;
    static const uint8_t kMaxPacketsPerPage = 50u;
    static const uint16_t kMaxPacketSize = 160u;
    static const uint16_t kFrameSamples = 320u;
    static const size_t kHeaderSize = 27u;
    static const size_t kReservedAudioPrefix = kHeaderSize + kMaxPacketsPerPage;

    bool emit_page(uint8_t header_type, uint64_t granule_position,
                   uint8_t lace_count, size_t payload_size);
    bool flush_audio_page(bool eos);
    uint32_t crc32(const uint8_t* bytes, size_t length) const;

    OggOpusSink sink_;
    uint8_t* page_buffer_;
    uint32_t serial_;
    uint16_t pre_skip_48k_;
    uint32_t sequence_number_;
    uint64_t valid_input_samples_;
    uint8_t packet_count_;
    size_t audio_payload_size_;
    OggOpusStats stats_;
    bool started_;
    bool finished_;
    bool failed_;
    bool final_packets_started_;
};
