#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t read_command;
    uint8_t stop_command;
    uint32_t address;
} SdReadPlan;

typedef struct {
    bool stuff_discarded;
    bool complete;
    uint8_t response;
} SdCmd12Response;

SdReadPlan sd_make_read_plan(uint32_t lba, uint32_t count, bool high_capacity);
void sd_cmd12_response_init(SdCmd12Response* parser);
bool sd_cmd12_response_feed(SdCmd12Response* parser, uint8_t value);
