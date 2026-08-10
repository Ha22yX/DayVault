#include <unity.h>

#include <stdint.h>
#include <string.h>

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

void make_input()
{
    for (uint32_t i = 0u; i < kInputSamples; ++i) {
        const int32_t phase = (int32_t)((i * 73u) % 2001u) - 1000;
        g_a[i] = (int16_t)(phase * 12);
        g_b[i] = (int16_t)(phase * 11);
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_chunking_is_invariant_and_preserves_exact_valid_samples);
    RUN_TEST(test_empty_input_emits_no_frame);
    RUN_TEST(test_sink_failure_is_latched);
    return UNITY_END();
}
