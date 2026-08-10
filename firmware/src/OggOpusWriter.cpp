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
    0x16, 0x00, 0x00, 0x00,
    'D', 'a', 'y', 'V', 'a', 'u', 'l', 't', ' ', 'l', 'i', 'b', 'o', 'p', 'u', 's',
    ' ', '1', '.', '6', '.', '1',
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

OggOpusWriter::OggOpusWriter()
    : sink_(),
      page_buffer_(nullptr),
      serial_(0u),
      pre_skip_48k_(0u),
      sequence_number_(0u),
      valid_input_samples_(0u),
      packet_count_(0u),
      audio_payload_size_(0u),
      stats_(),
      started_(false),
      finished_(false),
      failed_(true),
      final_packets_started_(false)
{
}

bool OggOpusWriter::begin(OggOpusSink sink, uint8_t* page_buffer, size_t page_buffer_size,
                          uint32_t serial, uint16_t pre_skip_48k)
{
    sink_ = {};
    page_buffer_ = nullptr;
    serial_ = 0u;
    pre_skip_48k_ = 0u;
    sequence_number_ = 0u;
    valid_input_samples_ = 0u;
    packet_count_ = 0u;
    audio_payload_size_ = 0u;
    stats_ = {};
    started_ = false;
    finished_ = false;
    failed_ = true;
    final_packets_started_ = false;
    if (sink.write == nullptr || page_buffer == nullptr || page_buffer_size < kPageBufferSize) return false;

    sink_ = sink;
    page_buffer_ = page_buffer;
    serial_ = serial;
    pre_skip_48k_ = pre_skip_48k;
    sequence_number_ = 0u;
    started_ = true;
    failed_ = false;

    uint8_t opus_head[sizeof(kOpusHead)];
    memcpy(opus_head, kOpusHead, sizeof(opus_head));
    opus_head[10] = (uint8_t)pre_skip_48k_;
    opus_head[11] = (uint8_t)(pre_skip_48k_ >> 8);
    page_buffer_[kHeaderSize] = sizeof(kOpusHead);
    memcpy(page_buffer_ + kHeaderSize + 1u, opus_head, sizeof(opus_head));
    if (!emit_page(0x02u, 0u, 1u, sizeof(opus_head))) return false;

    page_buffer_[kHeaderSize] = sizeof(kOpusTags);
    memcpy(page_buffer_ + kHeaderSize + 1u, kOpusTags, sizeof(kOpusTags));
    if (!emit_page(0x00u, 0u, 1u, sizeof(kOpusTags))) return false;
    return true;
}

bool OggOpusWriter::add_packet(const uint8_t* packet, uint16_t packet_size, uint16_t valid_input_samples)
{
    if (!started_ || finished_ || failed_ || packet_size == 0u || packet_size > kMaxPacketSize ||
        packet == nullptr) return false;
    const bool final_packet = valid_input_samples < kFrameSamples;
    if (final_packets_started_ && !final_packet) return false;
    if (final_packet && packet_count_ == kMaxPacketsPerPage - 1u &&
        !flush_audio_page(false)) return false;
    if (packet_count_ == kMaxPacketsPerPage) return false;

    page_buffer_[kHeaderSize + packet_count_] = (uint8_t)packet_size;
    memcpy(page_buffer_ + kReservedAudioPrefix + audio_payload_size_, packet, packet_size);
    audio_payload_size_ += packet_size;
    ++packet_count_;
    valid_input_samples_ += valid_input_samples;
    ++stats_.packet_count;
    stats_.valid_input_samples = valid_input_samples_;
    if (final_packet) final_packets_started_ = true;
    if (!final_packet && packet_count_ == kMaxPacketsPerPage &&
        !flush_audio_page(false)) return false;
    return true;
}

bool OggOpusWriter::finish()
{
    if (!started_ || finished_ || failed_) return false;
    const bool wrote = packet_count_ == 0u
        ? emit_page(0x04u, (uint64_t)pre_skip_48k_ + valid_input_samples_ * 3u, 0u, 0u)
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
                              uint8_t lace_count, size_t payload_size)
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

    write_le32(page_buffer_ + 22u, 0u);
    write_le32(page_buffer_ + 22u, crc32(page_buffer_, page_size));
    if (!sink_.write(sink_.context, page_buffer_, page_size)) {
        failed_ = true;
        return false;
    }
    ++sequence_number_;
    ++stats_.page_count;
    stats_.bytes_written += page_size;
    return true;
}

bool OggOpusWriter::flush_audio_page(bool eos)
{
    const uint64_t granule_position = (uint64_t)pre_skip_48k_ + (uint64_t)valid_input_samples_ * 3u;
    memmove(page_buffer_ + kHeaderSize + packet_count_, page_buffer_ + kReservedAudioPrefix,
            audio_payload_size_);
    if (!emit_page(eos ? 0x04u : 0x00u, granule_position, packet_count_, audio_payload_size_)) {
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
