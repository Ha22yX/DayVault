#include "SdProtocol.h"

SdReadPlan sd_make_read_plan(uint32_t lba, uint32_t count, bool high_capacity)
{
    const bool multi_block = count > 1;
    const SdReadPlan plan = {
        (uint8_t)(multi_block ? 18 : 17),
        (uint8_t)(multi_block ? 12 : 0),
        high_capacity ? lba : lba * 512u,
    };
    return plan;
}

void sd_cmd12_response_init(SdCmd12Response* parser)
{
    parser->stuff_discarded = false;
    parser->complete = false;
    parser->response = 0xFF;
}

bool sd_cmd12_response_feed(SdCmd12Response* parser, uint8_t value)
{
    if (!parser->stuff_discarded) {
        parser->stuff_discarded = true;
        return false;
    }
    if (!parser->complete && (value & 0x80u) == 0u) {
        parser->response = value;
        parser->complete = true;
    }
    return parser->complete;
}
