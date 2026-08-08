#include <Arduino.h>
#include "stm32l4xx_hal.h"
#include "Config.h"
#include "SdCard.h"
#include "Fs.h"
#include "ff.h"
#include "RingBuf.h"
#include "PdmCapture.h"
#include "WavFile.h"
#include <string.h>

extern "C" void SystemClock_Config(void);

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
        while (1) { }
    }

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI | RCC_OSCILLATORTYPE_HSI48;
    RCC_OscInitStruct.MSIState = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = 0;
    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_7;
    RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 20;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV4;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { while (1) { } }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) { while (1) { } }

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) { while (1) { } }
}

#define PIN_USB_DETECT 9    /* PA9 */
#define PIN_BOOT0       51  /* PH3 */

#define SYSTEM_MEMORY_BASE 0x1FFF0000u

static uint8_t audio_buf[PDM_RING_BYTES];
static RingBuf audio_rb;

static void record_test(int seconds)
{
    WavConfig cfg;
    cfg.format = 1;
    cfg.sample_rate = AUDIO_SAMPLE_RATE;
    cfg.channels = AUDIO_CHANNELS;
    cfg.bits = AUDIO_BITS;
    cfg.block_align = (uint16_t)(AUDIO_CHANNELS * (AUDIO_BITS / 8u));
    cfg.byte_rate = cfg.sample_rate * cfg.block_align;

    FIL f;
    static uint8_t blk[512];
    static uint8_t wav_hdr_buf[44];
    uint8_t* hdr = wav_hdr_buf;
    uint32_t data_bytes = 0;
    UINT wr = 0;

    Serial.print("REC start fs=");
    Serial.print(fs_mount_result());
    if (f_open(&f, "/REC001.WAV", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        Serial.println(" open FAIL");
        return;
    }
    wav_build_header(hdr, &cfg, 0);
    if (f_write(&f, hdr, 44, &wr) != FR_OK || wr != 44) { Serial.println(" hdr FAIL"); f_close(&f); return; }
    Serial.println();

    ringbuf_init(&audio_rb, audio_buf, sizeof(audio_buf));
    pdm_init(&audio_rb);
    Serial.println(" init ok");
    pdm_start();
    Serial.println(" start ok");
    Serial.print(" APB2ENR="); Serial.print(RCC->APB2ENR, HEX);
    Serial.print(" CCIPR="); Serial.print(RCC->CCIPR, HEX);
    Serial.print(" FLTCR1="); Serial.print(DFSDM1_Filter1->FLTCR1, HEX);
    Serial.print(" start_ret="); Serial.print(pdm_start_result());
    Serial.print(" FLTISR="); Serial.println(DFSDM1_Filter1->FLTISR, HEX);

    uint32_t end = millis() + (uint32_t)seconds * 1000u;
    uint8_t chunk[64];
    size_t chunk_len = 0;
    while (millis() < end) {
        int16_t s;
        while (pdm_try_read_sample(&s)) {
            chunk[chunk_len++] = (uint8_t)s;
            chunk[chunk_len++] = (uint8_t)(s >> 8);
            if (chunk_len == sizeof(chunk)) {
                if (f_write(&f, chunk, (UINT)chunk_len, &wr) != FR_OK || wr != chunk_len) { chunk_len = 0; goto done; }
                data_bytes += wr;
                chunk_len = 0;
            }
        }
    }
done:
    if (chunk_len > 0) {
        if (f_write(&f, chunk, (UINT)chunk_len, &wr) == FR_OK) data_bytes += wr;
    }
    pdm_stop();
    wav_patch_sizes(hdr, data_bytes);
    if (f_lseek(&f, 0) == FR_OK) f_write(&f, hdr, 44, &wr);
    f_close(&f);

    Serial.print("REC done bytes=");
    Serial.print(data_bytes);
    Serial.print(" samples=");
    Serial.print(pdm_sample_count());
    Serial.print(" overruns=");
    Serial.println(pdm_overruns());
}

static void dfu_enter(void)
{
    HAL_DeInit();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    __disable_irq();

    uint32_t msp = *(volatile uint32_t *)SYSTEM_MEMORY_BASE;
    if ((msp & 0xFFF00000u) != 0x20000000u) {
        NVIC_SystemReset();   /* invalid bootloader stack: reset instead */
        return;
    }
    __set_MSP(msp);
    ((void (*)(void)) * (volatile uint32_t *)(SYSTEM_MEMORY_BASE + 4u))();
}

void setup()
{
    SystemClock_Config();
    pinMode(PIN_USB_DETECT, INPUT);
    pinMode(PIN_BOOT0, INPUT_PULLDOWN);

    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && (millis() - t) < 3000) { }

    Serial.println("DV step2 ready");
    Serial.print("usb_detect="); Serial.print(digitalRead(PIN_USB_DETECT));
    Serial.print(" boot="); Serial.println(digitalRead(PIN_BOOT0));
}

void loop()
{
    static uint32_t last_tick = 0;

    if (Serial.available()) {
        static char line[64];
        static size_t n = 0;
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                if (n > 0) {
                    line[n] = 0;
                    if (strncmp(line, "CAPT", 4) == 0) {
                        record_test(5);
                    } else if (strncmp(line, "DFU", 3) == 0) {
                        Serial.println("entering DFU...");
                        Serial.flush();
                        dfu_enter();
                    } else if (strncmp(line, "INFO", 4) == 0) {
                        Serial.print("INFO usb_detect="); Serial.print(digitalRead(PIN_USB_DETECT));
                        Serial.print(" boot="); Serial.print(digitalRead(PIN_BOOT0));
                        Serial.print(" up="); Serial.print(millis());
                        Serial.print(" sd=");
                        if (sd_capacity_bytes() > 0) { Serial.print(sd_capacity_bytes()); Serial.print("B"); }
                        else { Serial.print("none"); }
                        Serial.println();
                    } else if (strncmp(line, "MOUNT", 5) == 0) {
                        Serial.print("MOUNT fr=");
                        Serial.println(fs_mount_result());
                    } else if (strncmp(line, "MBR", 3) == 0) {
                        uint8_t mbr[512];
                        bool ok = sd_init() && sd_read_sectors(0, mbr, 1);
                        if (!ok) { Serial.println("MBR read FAIL"); }
                        else {
                            Serial.print("sig55AA=");
                            Serial.print((mbr[510] == 0x55 && mbr[511] == 0xAA) ? "yes" : "no");
                            Serial.print(" ptype=");
                            Serial.print(mbr[446 + 4], HEX);
                            Serial.print(" partLBA=");
                            Serial.println((uint32_t)mbr[446 + 8] | ((uint32_t)mbr[446 + 9] << 8) | ((uint32_t)mbr[446 + 10] << 16) | ((uint32_t)mbr[446 + 11] << 24));
                            Serial.print("EFI-part=");
                            Serial.println((mbr[450] == 'E' && mbr[451] == 'F' && mbr[452] == 'I' && mbr[453] == ' ') ? "GPT" : "MBR");
                            Serial.print("s0[0..15]=");
                            for (int i = 0; i < 16; i++) { if (mbr[i] < 16) Serial.print("0"); Serial.print(mbr[i], HEX); Serial.print(" "); }
                            Serial.println();
                            Serial.print("s0[3..10]=");
                            for (int i = 3; i < 11; i++) Serial.print((char)mbr[i]);
                            Serial.println();
                        }
                    } else if (strncmp(line, "SDWRITE", 7) == 0) {
                        uint8_t pat[512];
                        uint8_t rd[512];
                        for (int i = 0; i < 512; i++) pat[i] = (uint8_t)(i & 0xFF);
                        bool w = sd_init() && sd_write_sectors(2, pat, 1);
                        bool r = sd_init() && sd_read_sectors(2, rd, 1);
                        bool same = w && r && (memcmp(pat, rd, 512) == 0);
                        Serial.print("SDWRITE w="); Serial.print(w ? "OK" : "FAIL");
                        Serial.print(" r="); Serial.print(r ? "OK" : "FAIL");
                        Serial.print(" match="); Serial.println(same ? "yes" : "no");
                    } else if (strncmp(line, "WRITE", 5) == 0) {
                        static const uint8_t payload[] = "DayVault exFAT write test 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ";
                        FIL f;
                        int r;
                        UINT wr = 0;
                        r = fs_mount_result();
                        if (r == FR_OK) r = f_open(&f, "/TEST1.TXT", FA_CREATE_ALWAYS | FA_WRITE);
                        Serial.print("WRITE open_fr="); Serial.print((int)r);
                        if (r == FR_OK) {
                            r = f_write(&f, payload, sizeof(payload) - 1, &wr);
                            Serial.print(" write_fr="); Serial.print((int)r);
                            Serial.print(" wrote="); Serial.print(wr);
                        }
                        if (r == FR_OK) r = f_close(&f);
                        Serial.print(" close_fr="); Serial.print((int)r);
                        if (r == FR_OK) {
                            r = f_open(&f, "/TEST1.TXT", FA_READ);
                            Serial.print(" reopen_fr="); Serial.print((int)r);
                            if (r == FR_OK) {
                                uint8_t rbuf[80];
                                UINT rd = 0;
                                r = f_read(&f, rbuf, sizeof(rbuf), &rd);
                                bool ok = (r == FR_OK && rd == sizeof(payload) - 1 && memcmp(rbuf, payload, rd) == 0);
                                Serial.print(" read_fr="); Serial.print((int)r);
                                Serial.print(" rd="); Serial.print(rd);
                                Serial.print(" match="); Serial.println(ok ? "yes" : "no");
                                f_close(&f);
                            }
                        }
                    } else if (strncmp(line, "SD", 2) == 0) {
                        bool ok = sd_init();
                        Serial.print("SD init="); Serial.print(ok ? "OK" : "FAIL");
                        if (ok) { Serial.print(" cap="); Serial.print(sd_capacity_bytes()); Serial.println("B"); }
                        else { Serial.println(); }
                    } else {
                        Serial.print("? "); Serial.println(line);
                    }
                    n = 0;
                }
            } else if (n < sizeof(line) - 1) {
                line[n++] = c;
            }
        }
    }

    uint32_t now = millis();
    if (now - last_tick >= 1000) {
        last_tick = now;
        if (Serial) {
            Serial.print("tick usb_detect="); Serial.print(digitalRead(PIN_USB_DETECT));
            Serial.print(" boot="); Serial.println(digitalRead(PIN_BOOT0));
        }
    }
}
