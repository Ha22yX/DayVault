#include <unity.h>

#include <stdint.h>
#include <new>
#include <string.h>
#include <type_traits>

#include "AudioPipeline.h"

namespace {

static const uint32_t kSampleRate = 16000u;
static const uint32_t kInputSamples = 1003u;
static const uint32_t kFrameSamples = 320u;
static const uint32_t kMaxFrames = 4u;

struct CapturedFrames {
    int16_t samples[kMaxFrames][kFrameSamples];
    uint16_t valid_samples[kMaxFrames];
    uint32_t frame_count;
};

static int16_t g_a[kInputSamples];
static int16_t g_b[kInputSamples];
static int16_t g_transition_a[427u];
static int16_t g_transition_b[427u];
static int16_t g_reference_a[kFrameSamples];
static int16_t g_reference_b[kFrameSamples];
static int16_t g_reference_fused[kFrameSamples];
static int16_t g_reference_reduced[kFrameSamples];
static int16_t g_reference_leveled[kFrameSamples];
static AudioFusion g_reference_fusion;
static NoiseReduction g_reference_reduction;
static SpeechLeveler g_reference_leveler;

void make_input()
{
    for (uint32_t i = 0u; i < kInputSamples; ++i) {
        const int32_t phase = (int32_t)((i * 73u) % 2001u) - 1000;
        g_a[i] = (int16_t)(phase * 12);
        g_b[i] = (int16_t)(phase * 11);
    }
}

void make_speech_pulse(int16_t* samples, uint32_t count)
{
    memset(samples, 0, count * sizeof(samples[0]));
    const uint32_t pulse_samples = count < 16u ? count : 16u;
    for (uint32_t i = 0u; i < pulse_samples; ++i) {
        samples[i] = (i & 1u) == 0u ? 12000 : -12000;
    }
}

bool capture_frame(void* context, const int16_t* pcm, uint16_t valid_samples)
{
    CapturedFrames* const captured = static_cast<CapturedFrames*>(context);
    if (captured->frame_count >= kMaxFrames || pcm == 0) return false;

    memcpy(captured->samples[captured->frame_count], pcm,
           sizeof(captured->samples[captured->frame_count]));
    captured->valid_samples[captured->frame_count] = valid_samples;
    captured->frame_count++;
    return true;
}

bool rejecting_sink(void* context, const int16_t*, uint16_t)
{
    uint32_t* const calls = static_cast<uint32_t*>(context);
    (*calls)++;
    return false;
}

void run_chunked_pipeline(uint32_t chunk_size, CapturedFrames* captured,
                          AudioPipelineStats* stats)
{
    AudioPipeline pipeline;
    memset(captured, 0, sizeof(*captured));
    TEST_ASSERT_TRUE(pipeline.reset(kSampleRate, capture_frame, captured));

    for (uint32_t offset = 0u; offset < kInputSamples;) {
        uint32_t count = chunk_size;
        if (count > kInputSamples - offset) count = kInputSamples - offset;
        TEST_ASSERT_TRUE(pipeline.push(g_a + offset, g_b + offset, count));
        offset += count;
    }
    TEST_ASSERT_TRUE(pipeline.finish());
    *stats = pipeline.stats();
}

void assert_expected_frames(const CapturedFrames& captured, const AudioPipelineStats& stats)
{
    const uint16_t expected_valid[] = {320u, 320u, 320u, 43u};
    TEST_ASSERT_EQUAL_UINT32(4u, captured.frame_count);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(expected_valid, captured.valid_samples, 4u);
    TEST_ASSERT_EQUAL_UINT64(kInputSamples, stats.captured_samples);
    TEST_ASSERT_EQUAL_UINT64(kInputSamples, stats.valid_samples);
    TEST_ASSERT_EQUAL_UINT32(4u, stats.emitted_frames);
    TEST_ASSERT_EQUAL_UINT32(128u, stats.suppressed_latency_samples);
    TEST_ASSERT_FALSE(stats.failed);
    for (uint32_t i = expected_valid[3]; i < kFrameSamples; ++i) {
        TEST_ASSERT_EQUAL_INT16(0, captured.samples[3][i]);
    }
}

void append_reference(CapturedFrames* captured, uint32_t* frame_samples,
                      const int16_t* pcm, uint32_t valid_samples)
{
    for (uint32_t i = 0u; i < valid_samples; ++i) {
        captured->samples[captured->frame_count][(*frame_samples)++] = pcm[i];
        if (*frame_samples == kFrameSamples) {
            captured->valid_samples[captured->frame_count] = kFrameSamples;
            captured->frame_count++;
            *frame_samples = 0u;
        }
    }
}

void process_reference_block(AudioFusion* fusion, NoiseReduction* reduction,
                             SpeechLeveler* leveler, const int16_t* a, const int16_t* b,
                             uint32_t valid_samples, bool* first_output,
                             bool* delayed_speech_present, uint32_t* delayed_valid_samples,
                             CapturedFrames* captured, uint32_t* frame_samples)
{
    memset(g_reference_a, 0, sizeof(g_reference_a));
    memset(g_reference_b, 0, sizeof(g_reference_b));
    memcpy(g_reference_a, a, valid_samples * sizeof(g_reference_a[0]));
    memcpy(g_reference_b, b, valid_samples * sizeof(g_reference_b[0]));

    fusion->process(g_reference_a, g_reference_b, g_reference_fused, kFrameSamples);
    const bool speech_present = fusion->stats().speech_present;
    reduction->process(g_reference_fused, g_reference_reduced, kFrameSamples, speech_present);
    leveler->process(g_reference_reduced, g_reference_leveled, kFrameSamples,
                     *delayed_speech_present);
    if (!*first_output) {
        append_reference(captured, frame_samples, g_reference_leveled, *delayed_valid_samples);
    } else {
        *first_output = false;
    }
    *delayed_speech_present = speech_present;
    *delayed_valid_samples = valid_samples;
}

void make_delayed_speech_reference(CapturedFrames* captured)
{
    bool first_output = true;
    bool delayed_speech_present = false;
    uint32_t delayed_valid_samples = 0u;
    uint32_t frame_samples = 0u;

    memset(captured, 0, sizeof(*captured));
    g_reference_fusion.reset(kSampleRate);
    g_reference_reduction.reset(kSampleRate);
    g_reference_leveler.reset(kSampleRate);
    process_reference_block(&g_reference_fusion, &g_reference_reduction, &g_reference_leveler,
                            g_transition_a,
                            g_transition_b, 128u, &first_output,
                            &delayed_speech_present, &delayed_valid_samples,
                            captured, &frame_samples);
    process_reference_block(&g_reference_fusion, &g_reference_reduction, &g_reference_leveler,
                            g_transition_a + 128u,
                            g_transition_b + 128u, 128u, &first_output,
                            &delayed_speech_present, &delayed_valid_samples,
                            captured, &frame_samples);
    process_reference_block(&g_reference_fusion, &g_reference_reduction, &g_reference_leveler,
                            g_transition_a + 256u,
                            g_transition_b + 256u, 128u, &first_output,
                            &delayed_speech_present, &delayed_valid_samples,
                            captured, &frame_samples);
    process_reference_block(&g_reference_fusion, &g_reference_reduction, &g_reference_leveler,
                            g_transition_a + 384u,
                            g_transition_b + 384u, 43u, &first_output,
                            &delayed_speech_present, &delayed_valid_samples,
                            captured, &frame_samples);
    process_reference_block(&g_reference_fusion, &g_reference_reduction, &g_reference_leveler,
                            g_reference_a, g_reference_b,
                            0u, &first_output, &delayed_speech_present,
                            &delayed_valid_samples, captured, &frame_samples);

    TEST_ASSERT_EQUAL_UINT32(107u, frame_samples);
    captured->valid_samples[captured->frame_count] = (uint16_t)frame_samples;
    memset(captured->samples[captured->frame_count] + frame_samples, 0,
           (kFrameSamples - frame_samples) * sizeof(captured->samples[0][0]));
    captured->frame_count++;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_chunking_is_invariant_and_preserves_exact_valid_samples(void)
{
    static const uint32_t chunk_sizes[] = {1u, 7u, 128u, 251u};
    CapturedFrames reference = {};
    AudioPipelineStats reference_stats = {};

    make_input();
    run_chunked_pipeline(chunk_sizes[0], &reference, &reference_stats);
    assert_expected_frames(reference, reference_stats);

    for (uint32_t run = 1u; run < sizeof(chunk_sizes) / sizeof(chunk_sizes[0]); ++run) {
        CapturedFrames captured = {};
        AudioPipelineStats stats = {};
        run_chunked_pipeline(chunk_sizes[run], &captured, &stats);
        assert_expected_frames(captured, stats);
        TEST_ASSERT_EQUAL_MEMORY(&reference, &captured, sizeof(reference));
        TEST_ASSERT_EQUAL_MEMORY(&reference_stats, &stats, sizeof(stats));
    }
}

void test_empty_input_emits_no_frame(void)
{
    AudioPipeline pipeline;
    CapturedFrames captured = {};

    TEST_ASSERT_TRUE(pipeline.reset(kSampleRate, capture_frame, &captured));
    TEST_ASSERT_TRUE(pipeline.finish());

    const AudioPipelineStats stats = pipeline.stats();
    TEST_ASSERT_EQUAL_UINT32(0u, captured.frame_count);
    TEST_ASSERT_EQUAL_UINT64(0u, stats.captured_samples);
    TEST_ASSERT_EQUAL_UINT64(0u, stats.valid_samples);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.emitted_frames);
    TEST_ASSERT_FALSE(stats.failed);
}

void test_sink_failure_is_latched(void)
{
    AudioPipeline pipeline;
    int16_t samples[512] = {};
    uint32_t sink_calls = 0u;

    TEST_ASSERT_TRUE(pipeline.reset(kSampleRate, rejecting_sink, &sink_calls));
    TEST_ASSERT_FALSE(pipeline.push(samples, samples, 512u));
    TEST_ASSERT_FALSE(pipeline.push(samples, samples, 1u));
    TEST_ASSERT_FALSE(pipeline.finish());

    const AudioPipelineStats stats = pipeline.stats();
    TEST_ASSERT_TRUE(stats.failed);
    TEST_ASSERT_EQUAL_UINT32(1u, sink_calls);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.emitted_frames);
}

void test_speech_metadata_tracks_delayed_nr_audio_through_partial_flush(void)
{
    AudioPipeline pipeline;
    CapturedFrames captured = {};
    CapturedFrames expected = {};

    memset(g_transition_a, 0, sizeof(g_transition_a));
    memset(g_transition_b, 0, sizeof(g_transition_b));
    make_speech_pulse(g_transition_a + 128u, 128u);
    make_speech_pulse(g_transition_b + 128u, 128u);
    make_speech_pulse(g_transition_a + 384u, 43u);
    make_speech_pulse(g_transition_b + 384u, 43u);
    make_delayed_speech_reference(&expected);

    TEST_ASSERT_TRUE(pipeline.reset(kSampleRate, capture_frame, &captured));
    for (uint32_t offset = 0u; offset < 427u;) {
        uint32_t count = 19u;
        if (count > 427u - offset) count = 427u - offset;
        TEST_ASSERT_TRUE(pipeline.push(g_transition_a + offset, g_transition_b + offset,
                                       count));
        offset += count;
    }
    TEST_ASSERT_TRUE(pipeline.finish());

    const uint16_t expected_valid[] = {320u, 107u};
    TEST_ASSERT_EQUAL_UINT32(2u, captured.frame_count);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(expected_valid, captured.valid_samples, 2u);
    TEST_ASSERT_EQUAL_MEMORY(&expected, &captured, sizeof(expected));
}

void test_calls_before_reset_are_rejected_with_zero_stats(void)
{
    alignas(AudioPipeline) uint8_t storage[sizeof(AudioPipeline)];
    memset(storage, 0xA5, sizeof(storage));
    AudioPipeline* const pipeline = new (storage) AudioPipeline;

    TEST_ASSERT_FALSE(pipeline->push(0, 0, 0u));
    TEST_ASSERT_FALSE(pipeline->finish());
    const AudioPipelineStats stats = pipeline->stats();
    TEST_ASSERT_EQUAL_UINT64(0u, stats.captured_samples);
    TEST_ASSERT_EQUAL_UINT64(0u, stats.valid_samples);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.emitted_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.dsp_blocks);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.suppressed_latency_samples);
    TEST_ASSERT_FALSE(stats.failed);
    pipeline->~AudioPipeline();
}

void test_only_one_pipeline_can_be_active(void)
{
    AudioPipeline first;
    AudioPipeline second;
    CapturedFrames captured = {};

    TEST_ASSERT_TRUE(first.reset(kSampleRate, capture_frame, &captured));
    TEST_ASSERT_FALSE(second.reset(kSampleRate, capture_frame, &captured));
    TEST_ASSERT_TRUE(second.stats().failed);
    TEST_ASSERT_TRUE(first.finish());
    TEST_ASSERT_TRUE(second.reset(kSampleRate, capture_frame, &captured));
    TEST_ASSERT_TRUE(second.finish());
}

void test_bad_push_failure_releases_storage_for_another_pipeline(void)
{
    AudioPipeline failed;
    AudioPipeline replacement;
    CapturedFrames captured = {};
    int16_t sample = 0;

    TEST_ASSERT_TRUE(failed.reset(kSampleRate, capture_frame, &captured));
    TEST_ASSERT_FALSE(failed.push(0, &sample, 1u));
    TEST_ASSERT_TRUE(failed.stats().failed);
    TEST_ASSERT_FALSE(failed.push(&sample, &sample, 1u));
    TEST_ASSERT_TRUE(replacement.reset(kSampleRate, capture_frame, &captured));
    TEST_ASSERT_TRUE(replacement.finish());
}

void test_sink_failure_releases_storage_for_another_pipeline(void)
{
    AudioPipeline failed;
    AudioPipeline replacement;
    CapturedFrames captured = {};
    int16_t samples[512] = {};
    uint32_t sink_calls = 0u;

    TEST_ASSERT_TRUE(failed.reset(kSampleRate, rejecting_sink, &sink_calls));
    TEST_ASSERT_FALSE(failed.push(samples, samples, 512u));
    TEST_ASSERT_TRUE(failed.stats().failed);
    TEST_ASSERT_FALSE(failed.finish());
    TEST_ASSERT_TRUE(replacement.reset(kSampleRate, capture_frame, &captured));
    TEST_ASSERT_TRUE(replacement.finish());
}

void test_finish_failure_releases_storage_for_another_pipeline(void)
{
    AudioPipeline failed;
    AudioPipeline replacement;
    CapturedFrames captured = {};
    int16_t samples[129] = {};
    uint32_t sink_calls = 0u;

    TEST_ASSERT_TRUE(failed.reset(kSampleRate, rejecting_sink, &sink_calls));
    TEST_ASSERT_TRUE(failed.push(samples, samples, 129u));
    TEST_ASSERT_FALSE(failed.finish());
    TEST_ASSERT_TRUE(failed.stats().failed);
    TEST_ASSERT_EQUAL_UINT32(1u, sink_calls);
    TEST_ASSERT_FALSE(failed.finish());
    TEST_ASSERT_TRUE(replacement.reset(kSampleRate, capture_frame, &captured));
    TEST_ASSERT_TRUE(replacement.finish());
}

void test_pipeline_cannot_be_copied_or_moved(void)
{
    TEST_ASSERT_FALSE((std::is_copy_constructible<AudioPipeline>::value));
    TEST_ASSERT_FALSE((std::is_copy_assignable<AudioPipeline>::value));
    TEST_ASSERT_FALSE((std::is_move_constructible<AudioPipeline>::value));
    TEST_ASSERT_FALSE((std::is_move_assignable<AudioPipeline>::value));
}

void test_stats_expose_latest_dsp_diagnostics(void)
{
    AudioPipeline pipeline;
    CapturedFrames captured = {};
    int16_t samples[256] = {};

    TEST_ASSERT_TRUE(pipeline.reset(kSampleRate, capture_frame, &captured));
    TEST_ASSERT_TRUE(pipeline.push(samples, samples, 256u));

    const AudioPipelineStats stats = pipeline.stats();
    TEST_ASSERT_EQUAL_INT32(32768, stats.fusion.weight_a_q15 + stats.fusion.weight_b_q15);
    TEST_ASSERT_EQUAL_UINT32(128u, stats.noise_reduction.latency_samples);
    TEST_ASSERT_GREATER_THAN(0u, stats.leveler.gain_q16);
    TEST_ASSERT_TRUE(pipeline.finish());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_chunking_is_invariant_and_preserves_exact_valid_samples);
    RUN_TEST(test_empty_input_emits_no_frame);
    RUN_TEST(test_sink_failure_is_latched);
    RUN_TEST(test_speech_metadata_tracks_delayed_nr_audio_through_partial_flush);
    RUN_TEST(test_calls_before_reset_are_rejected_with_zero_stats);
    RUN_TEST(test_only_one_pipeline_can_be_active);
    RUN_TEST(test_bad_push_failure_releases_storage_for_another_pipeline);
    RUN_TEST(test_sink_failure_releases_storage_for_another_pipeline);
    RUN_TEST(test_finish_failure_releases_storage_for_another_pipeline);
    RUN_TEST(test_pipeline_cannot_be_copied_or_moved);
    RUN_TEST(test_stats_expose_latest_dsp_diagnostics);
    return UNITY_END();
}
