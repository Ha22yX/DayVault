#include "SdCard.h"
#include "Config.h"
#include "stm32l4xx_hal.h"

static SPI_HandleTypeDef hspi1;
static uint64_t capacity_bytes = 0;

static uint8_t spi_txrx(uint8_t b)
{
    uint8_t rx = 0xFF;
    HAL_SPI_TransmitReceive(&hspi1, &b, &rx, 1, 100);
    return rx;
}

static void cs_high(void) { HAL_GPIO_WritePin(PIN_SD_CS_PORT, PIN_SD_CS, GPIO_PIN_SET); }
static void cs_low(void)  { HAL_GPIO_WritePin(PIN_SD_CS_PORT, PIN_SD_CS, GPIO_PIN_RESET); }

static bool sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t* resp, uint32_t tries)
{
    uint8_t buf[6];
    cs_low();
    spi_txrx(0xFF);
    buf[0] = (uint8_t)(0x40 | cmd);
    buf[1] = (uint8_t)(arg >> 24);
    buf[2] = (uint8_t)(arg >> 16);
    buf[3] = (uint8_t)(arg >> 8);
    buf[4] = (uint8_t)arg;
    buf[5] = crc;
    for (int i = 0; i < 6; i++) spi_txrx(buf[i]);
    for (uint32_t i = 0; i < tries; i++) {
        uint8_t r = spi_txrx(0xFF);
        if ((r & 0x80) == 0) { *resp = r; return true; }
    }
    cs_high();
    return false;
}

static void sd_end(void) { spi_txrx(0xFF); cs_high(); spi_txrx(0xFF); }

static void spi_set_speed(uint32_t prescaler)
{
    hspi1.Init.BaudRatePrescaler = prescaler;
    HAL_SPI_Init(&hspi1);
}

bool sd_init(void)
{
    uint8_t r;
    bool sdhc = false;

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
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;
    HAL_SPI_Init(&hspi1);

    spi_set_speed(SPI_BAUDRATEPRESCALER_256);   /* ~312 kHz for init handshake */

    cs_high();
    for (int i = 0; i < 80; i++) spi_txrx(0xFF);

    if (!sd_cmd(0, 0, 0x95, &r, 20)) return false;
    if (r != 1) return false;
    HAL_Delay(2);

    if (sd_cmd(8, 0x1AA, 0x87, &r, 20)) {
        uint8_t v[4];
        for (int i = 0; i < 4; i++) v[i] = spi_txrx(0xFF);
        sd_end();
        if (v[2] == 0x01 && v[3] == 0xAA) sdhc = true;
    }

    r = 0xFF;
    for (int i = 0; i < 100; i++) {
        if (!sd_cmd(55, 0, 0x01, &r, 10)) return false;
        sd_cmd(41, sdhc ? 0x40000000u : 0, 0x01, &r, 10);
        sd_end();
        if (r == 0) break;
    }
    if (r != 0) return false;

    spi_set_speed(SPI_BAUDRATEPRESCALER_8);   /* 10 MHz for data */

    {
        uint8_t csd[16];
        if (sd_cmd(9, 0, 0x01, &r, 10)) {
            for (int i = 0; i < 64; i++) { r = spi_txrx(0xFF); if (r == 0xFE) break; }
            for (int i = 0; i < 16; i++) csd[i] = spi_txrx(0xFF);
            spi_txrx(0xFF);
            spi_txrx(0xFF);
            sd_end();
            if ((csd[0] >> 6) == 1) {
                uint32_t csize = ((uint32_t)(csd[7] & 0x3F) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
                capacity_bytes = ((uint64_t)(csize + 1u) * 512u) * 1024u;
            } else {
                uint32_t csize = ((uint32_t)(csd[6] & 0x03) << 10) | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
                uint32_t mult = (uint8_t)(((csd[9] & 0x03) << 1) | (csd[10] >> 7));
                uint32_t blk = 1u << (mult + 2u);
                capacity_bytes = ((uint64_t)(csize + 1u) * blk) * 512u;
            }
        }
    }
    return true;
}

static bool sd_single(bool write, uint32_t lba, const uint8_t* src, uint8_t* dst)
{
    uint8_t r;
    if (!sd_cmd(write ? 24 : 17, lba, 0x01, &r, 20)) return false;
    if (write) {
        spi_txrx(0xFE);
        for (int j = 0; j < 512; j++) spi_txrx(src[j]);
        spi_txrx(0xFF);
        spi_txrx(0xFF);
        r = spi_txrx(0xFF);
        if ((r & 0x1F) != 0x05) return false;
        uint32_t t0 = HAL_GetTick();
        while ((HAL_GetTick() - t0) < 100) {
            r = spi_txrx(0xFF);
            if (r == 0xFF) break;
        }
        if (r != 0xFF) return false;
    } else {
        uint32_t t0 = HAL_GetTick();
        while ((HAL_GetTick() - t0) < 100) {
            r = spi_txrx(0xFF);
            if (r == 0xFE) break;
        }
        if (r != 0xFE) return false;
        for (int j = 0; j < 512; j++) dst[j] = spi_txrx(0xFF);
        spi_txrx(0xFF);
        spi_txrx(0xFF);
    }
    sd_end();
    return true;
}

bool sd_read_sectors(uint32_t lba, uint8_t* buf, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (!sd_single(false, lba + i, NULL, buf + i * 512)) return false;
    }
    return true;
}

bool sd_write_sectors(uint32_t lba, const uint8_t* buf, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (!sd_single(true, lba + i, buf + i * 512, NULL)) return false;
    }
    return true;
}

uint64_t sd_capacity_bytes(void) { return capacity_bytes; }
