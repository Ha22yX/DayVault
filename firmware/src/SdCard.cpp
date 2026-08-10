#include "SdCard.h"
#include "SdProtocol.h"
#include "Config.h"
#include "stm32l4xx_hal.h"

#include <string.h>

static SPI_HandleTypeDef hspi1;
static uint64_t capacity_bytes = 0;
static bool high_capacity = false;
static uint8_t spi_tx_fill[512];
static uint8_t spi_rx_sink[512];
static sd_stats_t stats;

static uint8_t spi_txrx(uint8_t value)
{
    uint8_t received = 0xFF;
    if (HAL_SPI_TransmitReceive(&hspi1, &value, &received, 1, 100) != HAL_OK) {
        return 0x00;
    }
    return received;
}

static bool spi_read_payload(uint8_t* dst, uint16_t length)
{
    return HAL_SPI_TransmitReceive(&hspi1, spi_tx_fill, dst, length, 250) == HAL_OK;
}

static bool spi_write_payload(const uint8_t* src, uint16_t length)
{
    return HAL_SPI_TransmitReceive(&hspi1, const_cast<uint8_t*>(src),
                                   spi_rx_sink, length, 250) == HAL_OK;
}

static void cs_high(void) { HAL_GPIO_WritePin(PIN_SD_CS_PORT, PIN_SD_CS, GPIO_PIN_SET); }
static void cs_low(void)  { HAL_GPIO_WritePin(PIN_SD_CS_PORT, PIN_SD_CS, GPIO_PIN_RESET); }

static void send_command_packet(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    spi_txrx((uint8_t)(0x40 | cmd));
    spi_txrx((uint8_t)(arg >> 24));
    spi_txrx((uint8_t)(arg >> 16));
    spi_txrx((uint8_t)(arg >> 8));
    spi_txrx((uint8_t)arg);
    spi_txrx(crc);
}

static bool sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t* resp, uint32_t tries)
{
    cs_low();
    spi_txrx(0xFF);
    send_command_packet(cmd, arg, crc);
    for (uint32_t i = 0; i < tries; i++) {
        uint8_t r = spi_txrx(0xFF);
        if ((r & 0x80) == 0) {
            *resp = r;
            return true;
        }
    }
    cs_high();
    spi_txrx(0xFF);
    return false;
}

static void sd_end(void)
{
    spi_txrx(0xFF);
    cs_high();
    spi_txrx(0xFF);
}

static bool spi_set_speed(uint32_t prescaler)
{
    hspi1.Init.BaudRatePrescaler = prescaler;
    return HAL_SPI_Init(&hspi1) == HAL_OK;
}

static bool wait_data_token(uint32_t timeout_ms)
{
    const uint32_t started = HAL_GetTick();
    do {
        const uint8_t token = spi_txrx(0xFF);
        if (token == 0xFE) return true;
        if ((token & 0xF0u) == 0u) return false;
    } while ((HAL_GetTick() - started) < timeout_ms);
    return false;
}

static bool wait_ready(uint32_t timeout_ms)
{
    const uint32_t started = HAL_GetTick();
    do {
        if (spi_txrx(0xFF) == 0xFF) return true;
    } while ((HAL_GetTick() - started) < timeout_ms);
    return false;
}

bool sd_init(void)
{
    uint8_t r = 0xFF;
    bool v2_card = false;
    capacity_bytes = 0;
    high_capacity = false;
    memset(spi_tx_fill, 0xFF, sizeof(spi_tx_fill));

    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin = PIN_SD_CS;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(PIN_SD_CS_PORT, &g);
    HAL_GPIO_WritePin(PIN_SD_CS_PORT, PIN_SD_CS, GPIO_PIN_SET);

    g.Pin = PIN_SD_SCK | PIN_SD_MOSI;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &g);

    g.Pin = PIN_SD_MISO;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &g);

    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;
    if (!spi_set_speed(SPI_BAUDRATEPRESCALER_256)) return false;

    cs_high();
    for (int i = 0; i < 80; i++) spi_txrx(0xFF);

    if (!sd_cmd(0, 0, 0x95, &r, 20) || r != 1) return false;
    sd_end();
    HAL_Delay(2);

    if (sd_cmd(8, 0x1AA, 0x87, &r, 20)) {
        if (r == 1) {
            uint8_t value[4];
            for (int i = 0; i < 4; i++) value[i] = spi_txrx(0xFF);
            v2_card = value[2] == 0x01 && value[3] == 0xAA;
        }
        sd_end();
    }

    r = 0xFF;
    for (int i = 0; i < 100; i++) {
        uint8_t app_response = 0xFF;
        if (!sd_cmd(55, 0, 0x01, &app_response, 10)) return false;
        sd_end();
        if (!sd_cmd(41, v2_card ? 0x40000000u : 0, 0x01, &r, 10)) return false;
        sd_end();
        if (r == 0) break;
        HAL_Delay(1);
    }
    if (r != 0) return false;

    if (!sd_cmd(58, 0, 0x01, &r, 20) || r != 0) return false;
    {
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) ocr[i] = spi_txrx(0xFF);
        high_capacity = v2_card && ((ocr[0] & 0x40u) != 0u);
    }
    sd_end();

    /* Default-speed SD cards support up to 25 MHz; PCLK2/4 is 20 MHz. */
    if (!spi_set_speed(SPI_BAUDRATEPRESCALER_4)) return false;

    {
        uint8_t csd[16];
        if (sd_cmd(9, 0, 0x01, &r, 10) && r == 0 && wait_data_token(100)) {
            if (!spi_read_payload(csd, sizeof(csd))) {
                sd_end();
                return false;
            }
            spi_txrx(0xFF);
            spi_txrx(0xFF);
            sd_end();
            if ((csd[0] >> 6) == 1) {
                uint32_t csize = ((uint32_t)(csd[7] & 0x3F) << 16) |
                                 ((uint32_t)csd[8] << 8) | csd[9];
                capacity_bytes = ((uint64_t)(csize + 1u) * 512u) * 1024u;
            } else {
                uint32_t csize = ((uint32_t)(csd[6] & 0x03) << 10) |
                                 ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
                uint32_t mult = (uint8_t)(((csd[9] & 0x03) << 1) |
                                          (csd[10] >> 7));
                uint32_t blocks = 1u << (mult + 2u);
                capacity_bytes = ((uint64_t)(csize + 1u) * blocks) * 512u;
            }
        } else {
            sd_end();
        }
    }
    return true;
}

static bool read_block_body(uint8_t* dst)
{
    if (!wait_data_token(100)) return false;
    if (!spi_read_payload(dst, 512)) return false;
    spi_txrx(0xFF);
    spi_txrx(0xFF);
    return true;
}

static bool stop_multi_read(void)
{
    SdCmd12Response parser;
    sd_cmd12_response_init(&parser);
    send_command_packet(12, 0, 0x01);
    for (uint32_t i = 0; i < 64 && !parser.complete; ++i) {
        sd_cmd12_response_feed(&parser, spi_txrx(0xFF));
    }
    const bool response_ok = parser.complete && parser.response == 0;
    const bool ready = response_ok && wait_ready(250);
    sd_end();
    return ready;
}

static bool sd_single(bool write, uint32_t lba, const uint8_t* src, uint8_t* dst)
{
    IWDG->KR = 0xAAAA;
    uint8_t r = 0xFF;
    const SdReadPlan plan = sd_make_read_plan(lba, 1, high_capacity);
    const uint8_t command = write ? 24 : plan.read_command;
    if (!sd_cmd(command, plan.address, 0x01, &r, 20) || r != 0) {
        sd_end();
        return false;
    }
    if (write) {
        spi_txrx(0xFE);
        if (!spi_write_payload(src, 512)) {
            sd_end();
            return false;
        }
        spi_txrx(0xFF);
        spi_txrx(0xFF);
        r = spi_txrx(0xFF);
        if ((r & 0x1F) != 0x05 || !wait_ready(250)) {
            sd_end();
            return false;
        }
    } else {
        stats.single_read_commands++;
        if (!read_block_body(dst)) {
            stats.read_errors++;
            sd_end();
            return false;
        }
        stats.sectors_read++;
    }
    sd_end();
    return true;
}

static bool sd_multi_read(uint32_t lba, uint8_t* buf, uint32_t count)
{
    const SdReadPlan plan = sd_make_read_plan(lba, count, high_capacity);
    uint8_t r = 0xFF;
    if (!sd_cmd(plan.read_command, plan.address, 0x01, &r, 20) || r != 0) {
        sd_end();
        return false;
    }

    stats.multi_read_commands++;
    bool read_ok = true;
    for (uint32_t i = 0; i < count; ++i) {
        IWDG->KR = 0xAAAA;
        if (!read_block_body(buf + i * 512u)) {
            stats.read_errors++;
            read_ok = false;
            break;
        }
        stats.sectors_read++;
    }
    const bool stop_ok = stop_multi_read();
    return read_ok && stop_ok;
}

bool sd_read_sectors(uint32_t lba, uint8_t* buf, uint32_t count)
{
    if (count == 0) return true;
    if (count == 1) return sd_single(false, lba, nullptr, buf);
    if (sd_multi_read(lba, buf, count)) return true;

    stats.multi_read_fallbacks++;
    if (!sd_init()) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!sd_single(false, lba + i, nullptr, buf + i * 512u)) return false;
    }
    return true;
}

bool sd_write_sectors(uint32_t lba, const uint8_t* buf, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (!sd_single(true, lba + i, buf + i * 512u, nullptr)) return false;
    }
    return true;
}

uint64_t sd_capacity_bytes(void) { return capacity_bytes; }

void sd_reset_stats(void)
{
    memset(&stats, 0, sizeof(stats));
}

void sd_get_stats(sd_stats_t* out)
{
    if (out != nullptr) *out = stats;
}
