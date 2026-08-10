#include "OggOpusWriter.h"

#include <string.h>

namespace {

const uint8_t kOpusHead[] = {
    'O', 'p', 'u', 's', 'H', 'e', 'a', 'd',
    0x01, 0x01, 0x00, 0x00, 0x80, 0x3E, 0x00, 0x00,
    0x00, 0x00, 0x00
};

const uint8_t kOpusTags[] = {
    'O', 'p', 'u', 's', 'T', 'a', 'g', 's',
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

void write_le32(uint8_t* bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void write_le64(uint8_t* bytes, uint64_t value)
{
    for (uint8_t i = 0u; i < 8u; ++i) bytes[i] = (uint8_t)(value >> (i * 8u));
}

}  // namespace

bool OggOpusWriter::begin(OggOpusSink sink, uint8_t* page_buffer, size_t page_buffer_size,
                          uint32_t serial, uint16_t pre_skip_48k)
{
    if (sink.write == nullptr || page_buffer == nullptr || page_buffer_size < kPageBufferSize) return false;

    sink_ = sink;
    page_buffer_ = page_buffer;
    serial_ = serial;
    pre_skip_48k_ = pre_skip_48k;
    sequence_number_ = 0u;
    valid_input_samples_ = 0u;
    packet_count_ = 0u;
    audio_payload_size_ = 0u;
    stats_ = {};
    started_ = true;
    finished_ = false;
    failed_ = false;

    uint8_t head[kHeaderSize + 1u];
    head[0] = sizeof(kOpusHead);
    uint8_t opus_head[sizeof(kOpusHead)];
    memcpy(opus_head, kOpusHead, sizeof(opus_head));
    opus_head[10] = (uint8_t)pre_skip_48k_;
    opus_head[11] = (uint8_t)(pre_skip_48k_ >> 8);
    if (!emit_page(0x02u, 0u, head, 1u, opus_head, sizeof(opus_head))) return false;

    uint8_t tags[kHeaderSize + 1u];
    tags[0] = sizeof(kOpusTags);
    if (!emit_page(0x00u, 0u, tags, 1u, kOpusTags, sizeof(kOpusTags))) return false;
    return true;
}

bool OggOpusWriter::add_packet(const uint8_t* packet, uint16_t packet_size, uint16_t valid_input_samples)
{
    if (!started_ || finished_ || failed_ || packet_size > kMaxPacketSize ||
        (packet == nullptr && packet_size != 0u)) return false;
    if (packet_count_ == kMaxPacketsPerPage && !flush_audio_page(false)) return false;

    page_buffer_[kHeaderSize + packet_count_] = (uint8_t)packet_size;
    if (packet_size != 0u) {
        memcpy(page_buffer_ + kReservedAudioPrefix + audio_payload_size_, packet, packet_size);
    }
    audio_payload_size_ += packet_size;
    ++packet_count_;
    valid_input_samples_ += valid_input_samples;
    ++stats_.packet_count;
    stats_.valid_input_samples = valid_input_samples_;
    return true;
}

bool OggOpusWriter::finish()
{
    if (!started_ || finished_ || failed_) return false;
    const bool wrote = packet_count_ == 0u
        ? emit_page(0x04u, pre_skip_48k_, nullptr, 0u, nullptr, 0u)
        : flush_audio_page(true);
    if (!wrote) return false;
    finished_ = true;
    return true;
}

OggOpusStats OggOpusWriter::stats() const
{
    return stats_;
}

bool OggOpusWriter::emit_page(uint8_t header_type, uint64_t granule_position,
                              const uint8_t* laces, uint8_t lace_count,
                              const uint8_t* payload, size_t payload_size)
{
    const size_t page_size = kHeaderSize + lace_count + payload_size;
    if (page_size > kPageBufferSize) {
        failed_ = true;
        return false;
    }

    memset(page_buffer_, 0, kHeaderSize);
    memcpy(page_buffer_, "OggS", 4u);
    page_buffer_[4] = 0u;
    page_buffer_[5] = header_type;
    write_le64(page_buffer_ + 6u, granule_position);
    write_le32(page_buffer_ + 14u, serial_);
    write_le32(page_buffer_ + 18u, sequence_number_);
    page_buffer_[26] = lace_count;
    if (lace_count != 0u) memcpy(page_buffer_ + kHeaderSize, laces, lace_count);
    if (payload_size != 0u) memcpy(page_buffer_ + kHeaderSize + lace_count, payload, payload_size);

    write_le32(page_buffer_ + 22u, 0u);
    write_le32(page_buffer_ + 22u, crc32(page_buffer_, page_size));
    if (!sink_.write(sink_.context, page_buffer_, page_size)) {
        failed_ = true;
        return false;
    }
    ++sequence_number_;
    ++stats_.page_count;
    stats_.bytes_written += (uint32_t)page_size;
    return true;
}

bool OggOpusWriter::flush_audio_page(bool eos)
{
    const uint64_t granule_position = (uint64_t)pre_skip_48k_ + (uint64_t)valid_input_samples_ * 3u;
    memmove(page_buffer_ + kHeaderSize + packet_count_, page_buffer_ + kReservedAudioPrefix,
            audio_payload_size_);
    if (!emit_page(eos ? 0x04u : 0x00u, granule_position, page_buffer_ + kHeaderSize,
                   packet_count_, page_buffer_ + kHeaderSize + packet_count_, audio_payload_size_)) {
        return false;
    }
    packet_count_ = 0u;
    audio_payload_size_ = 0u;
    return true;
}

uint32_t OggOpusWriter::crc32(const uint8_t* bytes, size_t length) const
{
    uint32_t crc = 0u;
    for (size_t i = 0u; i < length; ++i) {
        crc ^= (uint32_t)bytes[i] << 24;
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
        }
    }
    return crc;
}
