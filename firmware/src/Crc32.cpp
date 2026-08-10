#include "Crc32.h"

static const uint32_t nibble_table[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
};

uint32_t crc32_update(uint32_t previous_crc, const uint8_t* data, size_t length)
{
    uint32_t crc = previous_crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        crc = (crc >> 4) ^ nibble_table[crc & 0x0Fu];
        crc = (crc >> 4) ^ nibble_table[crc & 0x0Fu];
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t crc32_compute(const uint8_t* data, size_t length)
{
    return crc32_update(0, data, length);
}
