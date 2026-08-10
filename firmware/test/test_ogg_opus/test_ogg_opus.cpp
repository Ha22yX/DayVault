#include <unity.h>

#include <stdint.h>
#include <string.h>

#include "OggOpusWriter.h"

namespace {

struct CapturedOgg {
    uint8_t bytes[32768];
    size_t length;
};

struct OggPage {
    const uint8_t* bytes;
    size_t length;
};

uint32_t read_le32(const uint8_t* bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

uint64_t read_le64(const uint8_t* bytes)
{
    uint64_t value = 0u;
    for (uint8_t i = 0u; i < 8u; ++i) value |= (uint64_t)bytes[i] << (i * 8u);
    return value;
}

uint32_t ogg_crc_update(uint32_t crc, uint8_t byte)
{
    crc ^= (uint32_t)byte << 24;
    for (uint8_t bit = 0u; bit < 8u; ++bit) {
        crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
    }
    return crc;
}

bool all_page_crc_values_are_valid(const uint8_t* bytes, size_t length)
{
    size_t offset = 0u;
    while (offset < length) {
        if (length - offset < 27u || memcmp(bytes + offset, "OggS", 4u) != 0) return false;
        const size_t segments = bytes[offset + 26u];
        if (length - offset < 27u + segments) return false;
        size_t page_length = 27u + segments;
        for (size_t i = 0u; i < segments; ++i) page_length += bytes[offset + 27u + i];
        if (page_length > length - offset) return false;

        uint32_t crc = 0u;
        for (size_t i = 0u; i < page_length; ++i) {
            const uint8_t byte = (i >= 22u && i < 26u) ? 0u : bytes[offset + i];
            crc = ogg_crc_update(crc, byte);
        }
        if (crc != read_le32(bytes + offset + 22u)) return false;
        offset += page_length;
    }
    return offset == length;
}

size_t collect_pages(const CapturedOgg& captured, OggPage* pages, size_t max_pages)
{
    size_t offset = 0u;
    size_t count = 0u;
    while (offset < captured.length && count < max_pages) {
        const size_t segments = captured.bytes[offset + 26u];
        size_t page_length = 27u + segments;
        for (size_t i = 0u; i < segments; ++i) page_length += captured.bytes[offset + 27u + i];
        pages[count].bytes = captured.bytes + offset;
        pages[count].length = page_length;
        ++count;
        offset += page_length;
    }
    return count;
}

bool capture_write(void* context, const uint8_t* bytes, size_t length)
{
    CapturedOgg* captured = static_cast<CapturedOgg*>(context);
    if (length > sizeof(captured->bytes) - captured->length) return false;
    memcpy(captured->bytes + captured->length, bytes, length);
    captured->length += length;
    return true;
}

OggOpusSink capture_sink(CapturedOgg* captured)
{
    OggOpusSink sink = {capture_write, captured};
    return sink;
}

void begin_writer(OggOpusWriter* writer, CapturedOgg* captured, uint8_t* page_buffer,
                  uint32_t serial = 0x12345678u, uint16_t pre_skip = 312u)
{
    memset(captured, 0, sizeof(*captured));
    TEST_ASSERT_TRUE(writer->begin(capture_sink(captured), page_buffer, 8192u, serial, pre_skip));
}

}  // namespace

void test_begin_emits_golden_opus_head_page_with_valid_crc(void)
{
    static const uint8_t expected[] = {
        'O', 'g', 'g', 'S', 0x00, 0x02,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x78, 0x56, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00,
        0x76, 0x13, 0x83, 0x69, 0x01, 0x13,
        'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 0x01, 0x01,
        0x38, 0x01, 0x80, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    uint8_t page_buffer[8192];
    CapturedOgg captured;
    OggOpusWriter writer;

    begin_writer(&writer, &captured, page_buffer);

    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), captured.length - 44u);
    TEST_ASSERT_EQUAL_MEMORY(expected, captured.bytes, sizeof(expected));
    TEST_ASSERT_EQUAL_MEMORY("OggS", captured.bytes, 4u);
    TEST_ASSERT_EQUAL_UINT8(0x02u, captured.bytes[5]);
    TEST_ASSERT_EQUAL_MEMORY("OpusHead", captured.bytes + 28u, 8u);
    TEST_ASSERT_EQUAL_UINT8(1u, captured.bytes[37u]);
    TEST_ASSERT_EQUAL_UINT32(16000u, read_le32(captured.bytes + 40u));
    TEST_ASSERT_TRUE(all_page_crc_values_are_valid(captured.bytes, captured.length));
}

void test_begin_emits_opus_tags_page(void)
{
    uint8_t page_buffer[8192];
    CapturedOgg captured;
    OggOpusWriter writer;
    OggPage pages[4];

    begin_writer(&writer, &captured, page_buffer);

    TEST_ASSERT_EQUAL_UINT32(2u, collect_pages(captured, pages, 4u));
    TEST_ASSERT_EQUAL_UINT8(0x00u, pages[1].bytes[5u]);
    TEST_ASSERT_EQUAL_UINT32(1u, read_le32(pages[1].bytes + 18u));
    TEST_ASSERT_EQUAL_UINT8(1u, pages[1].bytes[26u]);
    TEST_ASSERT_EQUAL_UINT8(16u, pages[1].bytes[27u]);
    TEST_ASSERT_EQUAL_MEMORY("OpusTags", pages[1].bytes + 28u, 8u);
    TEST_ASSERT_EQUAL_UINT32(0u, read_le32(pages[1].bytes + 36u));
    TEST_ASSERT_EQUAL_UINT32(0u, read_le32(pages[1].bytes + 40u));
}

void test_audio_page_uses_packet_lacing_and_eos_granule(void)
{
    const uint8_t packet_a[] = {0xF8, 0x01, 0x02};
    const uint8_t packet_b[] = {0xF8, 0x03, 0x04, 0x05, 0x06};
    uint8_t page_buffer[8192];
    CapturedOgg captured;
    OggOpusWriter writer;
    OggPage pages[4];

    begin_writer(&writer, &captured, page_buffer, 0xA1B2C3D4u, 120u);
    TEST_ASSERT_TRUE(writer.add_packet(packet_a, sizeof(packet_a), 160u));
    TEST_ASSERT_TRUE(writer.add_packet(packet_b, sizeof(packet_b), 80u));
    TEST_ASSERT_TRUE(writer.finish());

    TEST_ASSERT_EQUAL_UINT32(3u, collect_pages(captured, pages, 4u));
    TEST_ASSERT_EQUAL_UINT8(0x04u, pages[2].bytes[5u]);
    TEST_ASSERT_EQUAL_UINT32(2u, read_le32(pages[2].bytes + 18u));
    TEST_ASSERT_EQUAL_UINT64(840u, read_le64(pages[2].bytes + 6u));
    TEST_ASSERT_EQUAL_UINT8(2u, pages[2].bytes[26u]);
    TEST_ASSERT_EQUAL_UINT8(sizeof(packet_a), pages[2].bytes[27u]);
    TEST_ASSERT_EQUAL_UINT8(sizeof(packet_b), pages[2].bytes[28u]);
    TEST_ASSERT_EQUAL_MEMORY(packet_a, pages[2].bytes + 29u, sizeof(packet_a));
    TEST_ASSERT_EQUAL_MEMORY(packet_b, pages[2].bytes + 29u + sizeof(packet_a), sizeof(packet_b));
    TEST_ASSERT_TRUE(all_page_crc_values_are_valid(captured.bytes, captured.length));
}

void test_fiftieth_packet_stays_pending_until_next_packet_or_finish(void)
{
    uint8_t packet[160] = {};
    packet[0] = 0xF8;
    uint8_t page_buffer[8192];
    CapturedOgg captured;
    OggOpusWriter writer;
    OggPage pages[5];

    begin_writer(&writer, &captured, page_buffer);
    for (uint8_t i = 0u; i < 50u; ++i) TEST_ASSERT_TRUE(writer.add_packet(packet, sizeof(packet), 160u));
    TEST_ASSERT_EQUAL_UINT32(2u, collect_pages(captured, pages, 5u));

    TEST_ASSERT_TRUE(writer.add_packet(packet, sizeof(packet), 160u));
    TEST_ASSERT_EQUAL_UINT32(3u, collect_pages(captured, pages, 5u));
    TEST_ASSERT_EQUAL_UINT8(0x00u, pages[2].bytes[5u]);
    TEST_ASSERT_EQUAL_UINT32(8077u, pages[2].length);
    TEST_ASSERT_EQUAL_UINT8(50u, pages[2].bytes[26u]);
    for (uint8_t i = 0u; i < 50u; ++i) TEST_ASSERT_EQUAL_UINT8(160u, pages[2].bytes[27u + i]);

    TEST_ASSERT_TRUE(writer.finish());
    TEST_ASSERT_EQUAL_UINT32(4u, collect_pages(captured, pages, 5u));
    TEST_ASSERT_EQUAL_UINT8(0x04u, pages[3].bytes[5u]);
    TEST_ASSERT_EQUAL_UINT64(24792u, read_le64(pages[3].bytes + 6u));
}

void test_rejects_packet_larger_than_fixed_160_byte_limit(void)
{
    uint8_t packet[161] = {};
    uint8_t page_buffer[8192];
    CapturedOgg captured;
    OggOpusWriter writer;

    begin_writer(&writer, &captured, page_buffer);

    TEST_ASSERT_FALSE(writer.add_packet(packet, sizeof(packet), 160u));
    TEST_ASSERT_TRUE(writer.finish());
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_begin_emits_golden_opus_head_page_with_valid_crc);
    RUN_TEST(test_begin_emits_opus_tags_page);
    RUN_TEST(test_audio_page_uses_packet_lacing_and_eos_granule);
    RUN_TEST(test_fiftieth_packet_stays_pending_until_next_packet_or_finish);
    RUN_TEST(test_rejects_packet_larger_than_fixed_160_byte_limit);
    return UNITY_END();
}
