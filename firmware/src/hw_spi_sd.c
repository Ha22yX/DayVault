#include "stm32l4xx_hal.h"
#include "hw_spi_sd.h"
#include "dayvault_config.h"

static SPI_HandleTypeDef hspi1;
static uint64_t capacity_bytes = 0;
static uint32_t lba_offset = 0;   /* MBR partition LBA offset */

static uint8_t spi_txrx(uint8_t b)
{
    uint8_t rx = 0xFF;
    HAL_SPI_TransmitReceive(&hspi1, &b, &rx, 1, 10);
    return rx;
}

static void cs_high(void) { HAL_GPIO_WritePin(PIN_SD_CS_PORT, PIN_SD_CS, GPIO_PIN_SET); }
static void cs_low(void)  { HAL_GPIO_WritePin(PIN_SD_CS_PORT, PIN_SD_CS, GPIO_PIN_RESET); }

static void sd_spi_fast(void)
{
    hspi1.Instance->CR1 &= ~SPI_CR1_BR;   /* /2 = 24 MHz */
}

static int sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *resp, uint32_t tries)
{
    uint8_t buf[6];
    uint32_t i;
    cs_low();
    spi_txrx(0xFF);   /* dummy, wait for ready */
    buf[0] = (uint8_t)(0x40 | cmd);
    buf[1] = (uint8_t)(arg >> 24);
    buf[2] = (uint8_t)(arg >> 16);
    buf[3] = (uint8_t)(arg >> 8);
    buf[4] = (uint8_t)arg;
    buf[5] = crc;
    for (i = 0; i < 6; i++)
        spi_txrx(buf[i]);
    for (i = 0; i < tries; i++)
    {
        uint8_t r = spi_txrx(0xFF);
        if ((r & 0x80) == 0)
        {
            *resp = r;
            return 1;
        }
    }
    cs_high();
    return 0;
}

static void sd_end(void)
{
    spi_txrx(0xFF);
    cs_high();
    spi_txrx(0xFF);
}

int hw_sd_init(void)
{
    uint8_t r;
    uint32_t i;
    int sdhc = 0;

    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
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

    /* 74+ clocks with CS high before init */
    cs_high();
    for (i = 0; i < 80; i++)
        spi_txrx(0xFF);

    if (!sd_cmd(0, 0, 0x95, &r, 20))     /* CMD0 GO_IDLE */
        return 0;
    if (r != 1)
        return 0;

    /* CMD8 SEND_IF_COND */
    {
        uint8_t resp[5];
        int ok = sd_cmd(8, 0x1AA, 0x87, &r, 20);
        if (ok)
        {
            resp[0] = r;
            for (i = 0; i < 4; i++)
                resp[1 + i] = spi_txrx(0xFF);
            sd_end();
            if (resp[3] == 0x1 && resp[4] == 0xAA)
                sdhc = 1;   /* supports CMD58 */
        }
    }

    /* ACMD41 (SD_SEND_OP_COND) */
    for (i = 0; i < 100; i++)
    {
        sd_cmd(55, 0, 0x01, &r, 10);
        if (r != 1) { sd_end(); return 0; }
        sd_cmd(41, sdhc ? 0x40000000 : 0, 0x01, &r, 10);
        sd_end();
        if (r == 0)
            break;
    }
    if (r != 0)
        return 0;

    /* CMD58 read OCR to determine card size class */
    if (sdhc)
    {
        uint8_t ocr[4];
        sd_cmd(58, 0, 0x01, &r, 10);
        for (i = 0; i < 4; i++)
            ocr[i] = spi_txrx(0xFF);
        sd_end();
        capacity_bytes = (ocr[0] & 0x3F) ? 0 : 1;   /* placeholder; real size from CSD below */
    }

    /* CMD9 read CSD for capacity */
    {
        uint8_t csd[16];
        if (sd_cmd(9, 0, 0x01, &r, 10))
        {
            for (i = 0; i < 16; i++)
                csd[i] = spi_txrx(0xFF);
            spi_txrx(0xFF);
            sd_end();
            /* CSD v1/v2 capacity decode */
            if (csd[0] >> 6 == 1)   /* CSD v2.0 */
            {
                uint32_t csize = ((uint32_t)(csd[7] & 0x3F) << 16) |
                                 ((uint32_t)csd[8] << 8) | csd[9];
                capacity_bytes = ((uint64_t)(csize + 1u) * 512u) * 1024u;
            }
            else
            {
                uint32_t csize = ((uint32_t)(csd[6] & 0x03) << 10) |
                                 ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
                uint32_t mult = (uint8_t)(((csd[9] & 0x03) << 1) | (csd[10] >> 7));
                uint32_t blk = 1u << (mult + 2u);
                capacity_bytes = ((uint64_t)(csize + 1u) * blk) * 512u;
            }
        }
    }

    sd_spi_fast();

    /* Parse MBR: first partition at LBA offset */
    {
        uint8_t mbr[512];
        hw_sd_read_sectors(0, mbr, 1);
        if (mbr[510] == 0x55 && mbr[511] == 0xAA)
        {
            uint8_t ptype = mbr[446 + 4];
            if (ptype == 0x0B || ptype == 0x0C || ptype == 0x0E || ptype == 0x00)
            {
                lba_offset = ((uint32_t)mbr[446 + 8]) |
                             ((uint32_t)mbr[446 + 9] << 8) |
                             ((uint32_t)mbr[446 + 10] << 16) |
                             ((uint32_t)mbr[446 + 11] << 24);
            }
        }
    }
    return 1;
}

int hw_sd_read_sectors(uint32_t lba, uint8_t *buf, uint32_t count)
{
    uint8_t r;
    uint32_t i;
    uint32_t addr = lba + lba_offset;
    if (!sd_cmd(17, addr, 0x01, &r, 20))     /* CMD17 READ_SINGLE_BLOCK */
        return 0;
    for (i = 0; i < count; i++)
    {
        uint32_t j;
        if (addr != (lba + lba_offset))
        {
            if (!sd_cmd(17, addr, 0x01, &r, 20))
                return 0;
        }
        /* wait for start token 0xFE */
        for (j = 0; j < 64; j++)
        {
            r = spi_txrx(0xFF);
            if (r == 0xFE)
                break;
        }
        if (r != 0xFE)
            return 0;
        for (j = 0; j < 512; j++)
            buf[i * 512 + j] = spi_txrx(0xFF);
        spi_txrx(0xFF);   /* CRC */
        spi_txrx(0xFF);
        addr++;
        if (i < count - 1)
        {
            /* single-block read only; multi-block uses CMD18 */
            uint8_t r2;
            if (!sd_cmd(17, addr, 0x01, &r2, 20))
                return 0;
        }
    }
    sd_end();
    return 1;
}

int hw_sd_write_sectors(uint32_t lba, const uint8_t *buf, uint32_t count)
{
    uint8_t r;
    uint32_t i, j;
    uint32_t addr = lba + lba_offset;
    for (i = 0; i < count; i++)
    {
        if (!sd_cmd(24, addr, 0x01, &r, 20))     /* CMD24 WRITE_BLOCK */
            return 0;
        spi_txrx(0xFE);
        for (j = 0; j < 512; j++)
            spi_txrx(buf[i * 512 + j]);
        spi_txrx(0xFF);
        spi_txrx(0xFF);
        r = spi_txrx(0xFF);
        if ((r & 0x05) != 0x05)
            return 0;
        for (j = 0; j < 64; j++)
        {
            r = spi_txrx(0xFF);
            if (r == 0xFF)
                break;
        }
        addr++;
    }
    sd_end();
    return 1;
}

uint64_t hw_sd_capacity_bytes(void)
{
    return capacity_bytes;
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    (void)hspi;
}
