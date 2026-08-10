#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <opus.h>

#include "DayVaultOpusEncoder.h"
#include "OpusArena.h"
#include "OpusFinalization.h"

static_assert(kOpusSampleRate == 16000, "Opus input rate must be 16 kHz");
static_assert(kOpusFrameSamples == 320, "Opus frames must be 20 ms");
static_assert(kOpusBitrate == 24000, "Opus target bitrate must be 24 kbit/s");
static_assert(kOpusMaxPacketBytes == 160, "Opus packet storage must remain bounded");

namespace {

alignas(16) uint8_t g_workspace[32u * 1024u];
alignas(16) uint8_t g_reference_workspace[32u * 1024u];

void fill_voice_like_pcm(int16_t* pcm, uint32_t frame)
{
    for (size_t i = 0; i < 320u; ++i) {
        const uint32_t phase = (frame * 37u + (uint32_t)i * 53u) % 2001u;
        pcm[i] = (int16_t)((int32_t)phase - 1000);
    }
}

}  // namespace

void test_encoder_emits_bounded_standard_opus_packets(void)
{
    opus_arena_begin(g_reference_workspace, sizeof(g_reference_workspace));
    int reference_error = OPUS_OK;
    OpusEncoder* reference = opus_encoder_create(
        16000, 1, OPUS_APPLICATION_RESTRICTED_SILK, &reference_error);
    TEST_ASSERT_EQUAL_INT(OPUS_OK, reference_error);
    TEST_ASSERT_NOT_NULL(reference);
    int lookahead = 0;
    TEST_ASSERT_EQUAL_INT(OPUS_OK, opus_encoder_ctl(reference, OPUS_GET_LOOKAHEAD(&lookahead)));
    TEST_ASSERT_GREATER_THAN(0, lookahead);
    opus_encoder_destroy(reference);

    DayVaultOpusEncoder encoder;
    TEST_ASSERT_TRUE(encoder.begin(g_workspace, sizeof(g_workspace)));

    int16_t pcm[320] = {0};
    uint8_t packet[160] = {0};
    const int length = encoder.encode(pcm, packet);

    TEST_ASSERT_GREATER_THAN(0, length);
    TEST_ASSERT_LESS_OR_EQUAL(160, length);
    TEST_ASSERT_EQUAL_INT(1, opus_packet_get_nb_frames(packet, length));
    TEST_ASSERT_EQUAL_INT(320, opus_packet_get_nb_samples(packet, length, 16000));
    TEST_ASSERT_EQUAL_INT(OPUS_BANDWIDTH_WIDEBAND, opus_packet_get_bandwidth(packet));
    TEST_ASSERT_GREATER_THAN(0u, encoder.workspace_used());
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(g_workspace), encoder.workspace_used());
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(lookahead * 3), encoder.pre_skip_48k());

    const DayVaultOpusStats stats = encoder.stats();
    TEST_ASSERT_EQUAL_UINT64(1u, stats.frame_count);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)length, stats.encoded_bytes);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.error_count);
}

void test_encoder_encodes_100_frames_without_arena_growth(void)
{
    DayVaultOpusEncoder encoder;
    TEST_ASSERT_TRUE(encoder.begin(g_workspace, sizeof(g_workspace)));
    const size_t initial_used = encoder.workspace_used();
    TEST_ASSERT_EQUAL_size_t(initial_used, opus_arena_used());

    int16_t pcm[320];
    uint8_t packet[160];
    uint64_t encoded_bytes = 0u;
    for (uint32_t frame = 0; frame < 100u; ++frame) {
        fill_voice_like_pcm(pcm, frame);
        const int length = encoder.encode(pcm, packet);
        TEST_ASSERT_GREATER_THAN(0, length);
        TEST_ASSERT_LESS_OR_EQUAL(160, length);
        TEST_ASSERT_EQUAL_INT(320, opus_packet_get_nb_samples(packet, length, 16000));
        TEST_ASSERT_EQUAL_size_t(initial_used, opus_arena_used());
        TEST_ASSERT_EQUAL_size_t(initial_used, encoder.workspace_used());
        encoded_bytes += (uint32_t)length;
    }

    const DayVaultOpusStats stats = encoder.stats();
    TEST_ASSERT_EQUAL_UINT64(100u, stats.frame_count);
    TEST_ASSERT_EQUAL_UINT64(encoded_bytes, stats.encoded_bytes);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.error_count);
}

void test_encoder_reset_repeats_the_first_silence_packet(void)
{
    DayVaultOpusEncoder encoder;
    int16_t pcm[320] = {0};
    uint8_t first_packet[160] = {0};
    uint8_t reset_packet[160] = {0};

    TEST_ASSERT_TRUE(encoder.begin(g_workspace, sizeof(g_workspace)));
    const size_t first_used = encoder.workspace_used();
    const uint16_t first_pre_skip = encoder.pre_skip_48k();
    const int first_length = encoder.encode(pcm, first_packet);
    TEST_ASSERT_GREATER_THAN(0, first_length);
    encoder.end();

    TEST_ASSERT_TRUE(encoder.begin(g_workspace, sizeof(g_workspace)));
    const int reset_length = encoder.encode(pcm, reset_packet);
    TEST_ASSERT_EQUAL_INT(first_length, reset_length);
    TEST_ASSERT_EQUAL_MEMORY(first_packet, reset_packet, (size_t)first_length);
    TEST_ASSERT_EQUAL_size_t(first_used, encoder.workspace_used());
    TEST_ASSERT_EQUAL_UINT16(first_pre_skip, encoder.pre_skip_48k());

    const DayVaultOpusStats stats = encoder.stats();
    TEST_ASSERT_EQUAL_UINT64(1u, stats.frame_count);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)reset_length, stats.encoded_bytes);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.error_count);
}

void test_encoder_rejects_bad_parameters_and_counts_errors(void)
{
    DayVaultOpusEncoder encoder;
    int16_t pcm[320] = {0};
    uint8_t packet[160] = {0};

    TEST_ASSERT_EQUAL_INT(OPUS_INVALID_STATE, encoder.encode(pcm, packet));
    TEST_ASSERT_EQUAL_UINT32(1u, encoder.stats().error_count);

    TEST_ASSERT_FALSE(encoder.begin(nullptr, sizeof(g_workspace)));
    TEST_ASSERT_EQUAL_UINT32(1u, encoder.stats().error_count);
    TEST_ASSERT_FALSE(encoder.begin(g_workspace, 0u));
    TEST_ASSERT_EQUAL_UINT32(1u, encoder.stats().error_count);

    TEST_ASSERT_TRUE(encoder.begin(g_workspace, sizeof(g_workspace)));
    TEST_ASSERT_EQUAL_INT(OPUS_BAD_ARG, encoder.encode(nullptr, packet));
    TEST_ASSERT_EQUAL_INT(OPUS_BAD_ARG, encoder.encode(pcm, nullptr));
    TEST_ASSERT_EQUAL_UINT32(2u, encoder.stats().error_count);

    const int length = encoder.encode(pcm, packet);
    TEST_ASSERT_GREATER_THAN(0, length);
    encoder.end();
    TEST_ASSERT_EQUAL_INT(OPUS_INVALID_STATE, encoder.encode(pcm, packet));

    const DayVaultOpusStats stats = encoder.stats();
    TEST_ASSERT_EQUAL_UINT64(1u, stats.frame_count);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)length, stats.encoded_bytes);
    TEST_ASSERT_EQUAL_UINT32(3u, stats.error_count);
}

void test_encoder_rejects_small_workspace_without_heap_fallback(void)
{
    alignas(16) uint8_t small_workspace[1024];
    memset(small_workspace, 0xA5, sizeof(small_workspace));

    DayVaultOpusEncoder encoder;
    TEST_ASSERT_FALSE(encoder.begin(small_workspace, sizeof(small_workspace)));
    TEST_ASSERT_EQUAL_size_t(0u, encoder.workspace_used());
    TEST_ASSERT_EQUAL_UINT32(1u, encoder.stats().error_count);

    int16_t pcm[320] = {0};
    uint8_t packet[160] = {0};
    TEST_ASSERT_EQUAL_INT(OPUS_INVALID_STATE, encoder.encode(pcm, packet));
    TEST_ASSERT_EQUAL_UINT32(2u, encoder.stats().error_count);
}

void test_encoder_leaves_non_overlapping_sram2_page_region(void)
{
    static const size_t kOggPageBytes = 8192u;
    static const size_t kEncoderBytes = sizeof(g_workspace) - kOggPageBytes;
    DayVaultOpusEncoder encoder;

    TEST_ASSERT_TRUE(encoder.begin(g_workspace, kEncoderBytes));
    TEST_ASSERT_LESS_OR_EQUAL(kEncoderBytes, encoder.workspace_used());
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(g_workspace),
                              encoder.workspace_used() + kOggPageBytes);
}

void test_encoder_exposes_native_16k_lookahead(void)
{
    DayVaultOpusEncoder encoder;

    TEST_ASSERT_TRUE(encoder.begin(g_workspace, sizeof(g_workspace)));
    TEST_ASSERT_GREATER_THAN(0u, encoder.lookahead_samples_16k());
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(encoder.lookahead_samples_16k() * 3u),
                             encoder.pre_skip_48k());
}

void test_finalization_plan_counts_partial_frame_zeros_before_lookahead(void)
{
    const OpusFinalizationPlan needs_one = opus_finalization_plan(104u, true, 300u);
    TEST_ASSERT_EQUAL_UINT16(20u, needs_one.zeros_already_encoded);
    TEST_ASSERT_EQUAL_UINT16(84u, needs_one.remaining_lookahead_samples);
    TEST_ASSERT_EQUAL_UINT16(1u, needs_one.additional_zero_frames);

    const OpusFinalizationPlan already_covered = opus_finalization_plan(104u, true, 200u);
    TEST_ASSERT_EQUAL_UINT16(120u, already_covered.zeros_already_encoded);
    TEST_ASSERT_EQUAL_UINT16(0u, already_covered.remaining_lookahead_samples);
    TEST_ASSERT_EQUAL_UINT16(0u, already_covered.additional_zero_frames);
}

void test_finalization_plan_drains_boundary_frame_but_not_empty_input(void)
{
    const OpusFinalizationPlan boundary = opus_finalization_plan(104u, true, 320u);
    TEST_ASSERT_EQUAL_UINT16(0u, boundary.zeros_already_encoded);
    TEST_ASSERT_EQUAL_UINT16(104u, boundary.remaining_lookahead_samples);
    TEST_ASSERT_EQUAL_UINT16(1u, boundary.additional_zero_frames);

    const OpusFinalizationPlan empty = opus_finalization_plan(104u, false, 0u);
    TEST_ASSERT_EQUAL_UINT16(0u, empty.additional_zero_frames);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_encoder_emits_bounded_standard_opus_packets);
    RUN_TEST(test_encoder_encodes_100_frames_without_arena_growth);
    RUN_TEST(test_encoder_reset_repeats_the_first_silence_packet);
    RUN_TEST(test_encoder_rejects_bad_parameters_and_counts_errors);
    RUN_TEST(test_encoder_rejects_small_workspace_without_heap_fallback);
    RUN_TEST(test_encoder_leaves_non_overlapping_sram2_page_region);
    RUN_TEST(test_encoder_exposes_native_16k_lookahead);
    RUN_TEST(test_finalization_plan_counts_partial_frame_zeros_before_lookahead);
    RUN_TEST(test_finalization_plan_drains_boundary_frame_but_not_empty_input);
    return UNITY_END();
}
