#include "DayVaultOpusEncoder.h"

#include <limits.h>

#include <opus.h>

#include "OpusArena.h"

namespace {

const size_t kMaximumWorkspaceBytes = 32u * 1024u;

}  // namespace

DayVaultOpusEncoder::DayVaultOpusEncoder()
    : encoder_(nullptr),
      lookahead_samples_16k_(0u),
      pre_skip_48k_(0u),
      workspace_used_(0u),
      stats_{}
{
}

DayVaultOpusEncoder::~DayVaultOpusEncoder()
{
    end();
}

bool DayVaultOpusEncoder::begin(void* workspace, size_t bytes)
{
    end();
    lookahead_samples_16k_ = 0u;
    pre_skip_48k_ = 0u;
    workspace_used_ = 0u;
    stats_ = DayVaultOpusStats{};

    if (workspace == nullptr || bytes == 0u) {
        ++stats_.error_count;
        return false;
    }

    opus_arena_begin(workspace, bytes);
    int error = OPUS_OK;
    encoder_ = opus_encoder_create(kOpusSampleRate, 1,
                                   OPUS_APPLICATION_RESTRICTED_SILK, &error);
    workspace_used_ = opus_arena_used();
    if (encoder_ == nullptr || error != OPUS_OK) return fail_begin();
    if (workspace_used_ > bytes || workspace_used_ > kMaximumWorkspaceBytes) {
        return fail_begin();
    }

    if (opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(kOpusBitrate)) != OPUS_OK) {
        return fail_begin();
    }
    if (opus_encoder_ctl(encoder_, OPUS_SET_VBR(1)) != OPUS_OK) {
        return fail_begin();
    }
    if (opus_encoder_ctl(encoder_, OPUS_SET_VBR_CONSTRAINT(1)) != OPUS_OK) {
        return fail_begin();
    }
    if (opus_encoder_ctl(encoder_, OPUS_SET_COMPLEXITY(kOpusComplexity)) != OPUS_OK) {
        return fail_begin();
    }
    if (opus_encoder_ctl(encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE)) != OPUS_OK) {
        return fail_begin();
    }
    if (opus_encoder_ctl(encoder_, OPUS_SET_DTX(0)) != OPUS_OK) {
        return fail_begin();
    }
    if (opus_encoder_ctl(encoder_, OPUS_SET_INBAND_FEC(0)) != OPUS_OK) {
        return fail_begin();
    }
    if (opus_encoder_ctl(encoder_, OPUS_SET_PACKET_LOSS_PERC(0)) != OPUS_OK) {
        return fail_begin();
    }
    if (opus_encoder_ctl(encoder_, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND)) != OPUS_OK) {
        return fail_begin();
    }

    opus_int32 lookahead = 0;
    if (opus_encoder_ctl(encoder_, OPUS_GET_LOOKAHEAD(&lookahead)) != OPUS_OK) {
        return fail_begin();
    }
    if (lookahead <= 0 || lookahead > UINT16_MAX / 3) return fail_begin();

    lookahead_samples_16k_ = (uint16_t)lookahead;
    pre_skip_48k_ = (uint16_t)(lookahead_samples_16k_ * 3u);
    workspace_used_ = opus_arena_used();
    if (workspace_used_ > bytes || workspace_used_ > kMaximumWorkspaceBytes) {
        return fail_begin();
    }
    return true;
}

int DayVaultOpusEncoder::encode(const int16_t pcm[kOpusFrameSamples],
                                uint8_t packet[kOpusMaxPacketBytes])
{
    if (encoder_ == nullptr) {
        ++stats_.error_count;
        return OPUS_INVALID_STATE;
    }
    if (pcm == nullptr || packet == nullptr) {
        ++stats_.error_count;
        return OPUS_BAD_ARG;
    }

    const int length = opus_encode(encoder_, pcm, kOpusFrameSamples,
                                   packet, kOpusMaxPacketBytes);
    if (length <= 0 || length > kOpusMaxPacketBytes ||
        opus_arena_used() != workspace_used_) {
        ++stats_.error_count;
        return length < 0 ? length : OPUS_INTERNAL_ERROR;
    }

    ++stats_.frame_count;
    stats_.encoded_bytes += (uint32_t)length;
    return length;
}

uint16_t DayVaultOpusEncoder::lookahead_samples_16k() const
{
    return lookahead_samples_16k_;
}

uint16_t DayVaultOpusEncoder::pre_skip_48k() const
{
    return pre_skip_48k_;
}

size_t DayVaultOpusEncoder::workspace_used() const
{
    return workspace_used_;
}

DayVaultOpusStats DayVaultOpusEncoder::stats() const
{
    return stats_;
}

void DayVaultOpusEncoder::end()
{
    if (encoder_ != nullptr) opus_encoder_destroy(encoder_);
    encoder_ = nullptr;
}

bool DayVaultOpusEncoder::fail_begin()
{
    workspace_used_ = opus_arena_used();
    if (encoder_ != nullptr) opus_encoder_destroy(encoder_);
    encoder_ = nullptr;
    lookahead_samples_16k_ = 0u;
    pre_skip_48k_ = 0u;
    ++stats_.error_count;
    return false;
}
