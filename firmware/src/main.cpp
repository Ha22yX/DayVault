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

static void check_wav_file(void)
{
    FIL f;
    uint8_t hdr[44];
    UINT rd = 0;
    FRESULT r;
    char name[24];
    uint32_t seq;
    uint32_t newest;

    Serial.print("CHECK mount="); Serial.println(fs_mount_result());
    seq = fs_next_sequence();
    newest = (seq > 1) ? seq - 1 : 1;
    snprintf(name, sizeof(name), "0:/%s%03u.%s", REC_DIR_STR, (unsigned)newest, REC_EXT_STR);

    Serial.print("CHECK file="); Serial.println(name);
    if (f_open(&f, name, FA_READ) != FR_OK) {
        Serial.println("CHECK open FAIL");
        return;
    }
    r = f_read(&f, hdr, 44, &rd);
    Serial.print("CHECK hdr_fr="); Serial.print((int)r);
    Serial.print(" rd="); Serial.print(rd);
    if (rd == 44) {
        bool riff = (hdr[0]=='R'&&hdr[1]=='I'&&hdr[2]=='F'&&hdr[3]=='F'&&hdr[8]=='W'&&hdr[9]=='A'&&hdr[10]=='V'&&hdr[11]=='E');
        uint32_t data_sz = (uint32_t)hdr[40] | ((uint32_t)hdr[41]<<8) | ((uint32_t)hdr[42]<<16) | ((uint32_t)hdr[43]<<24);
        uint16_t ch = (uint16_t)(hdr[22] | (hdr[23]<<8));
        uint32_t sr = (uint32_t)hdr[24] | ((uint32_t)hdr[25]<<8) | ((uint32_t)hdr[26]<<16) | ((uint32_t)hdr[27]<<24);
        Serial.print(" riff="); Serial.print(riff ? "yes" : "NO");
        Serial.print(" ch="); Serial.print(ch);
        Serial.print(" sr="); Serial.print(sr);
        Serial.print(" dataSz="); Serial.println(data_sz);
    }
    f_close(&f);
    Serial.println("CHECK done");
}

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

static void sample_stats(void)
{
    int16_t mn = 32767, mx = -32768, prev = 0;
    long sum = 0;
    uint32_t count = 0, changes = 0;

    ringbuf_init(&audio_rb, audio_buf, sizeof(audio_buf));
    pdm_init(&audio_rb);
    pdm_start();
    uint32_t end = millis() + 3000;
    while (millis() < end) {
        int16_t s;
        while (pdm_try_read_sample(&s)) {
            if (count == 0) prev = s;
            else if (s != prev) { changes++; prev = s; }
            if (s < mn) mn = s;
            if (s > mx) mx = s;
            sum += s;
            count++;
        }
    }
    pdm_stop();
    Serial.print("SAMP count="); Serial.print(count);
    Serial.print(" min="); Serial.print(mn);
    Serial.print(" max="); Serial.print(mx);
    Serial.print(" avg="); Serial.print((int)(count ? sum / count : 0));
    Serial.print(" changes="); Serial.println(changes);
}

static void download_file(const char* fname)
{
    FIL f;
    uint8_t buf[512];
    UINT rd = 0;
    char path[32];

    Serial.print("DL mount="); Serial.println(fs_mount_result());
    snprintf(path, sizeof(path), "0:/%s", fname);
    if (f_open(&f, path, FA_READ) != FR_OK) { Serial.println("DL open FAIL"); fs_unmount(); return; }
    Serial.print("DLSTART ");
    Serial.println((uint32_t)f_size(&f));
    while (f_read(&f, buf, sizeof(buf), &rd) == FR_OK && rd > 0) {
        Serial.write(buf, rd);
    }
    f_close(&f);
    fs_unmount();
    Serial.println("DLEND");
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

static WavConfig rec_cfg;
static FIL rec_file;
static bool rec_active = false;
static uint32_t rec_data_bytes = 0;
static uint32_t rec_seq = 1;
static uint32_t rec_start_ms = 0;
static uint32_t rec_discard = 0;
static uint8_t rec_chunk[64];
static size_t rec_chunk_len = 0;
static int rec_err = 0;

static void rec_flush_chunk(void)
{
    UINT wr = 0;
    if (rec_chunk_len > 0) {
        if (f_write(&rec_file, rec_chunk, (UINT)rec_chunk_len, &wr) == FR_OK) rec_data_bytes += wr;
        rec_chunk_len = 0;
    }
}

static void rec_start(void)
{
    char name[24];
    UINT wr = 0;
    uint8_t hdr[44];
    if (rec_active) return;
    rec_err = 0;
    if (!fs_mount()) { rec_err = 1; return; }
    rec_seq = fs_next_sequence();
    snprintf(name, sizeof(name), "0:/%s%03u.%s", REC_DIR_STR, (unsigned)rec_seq, REC_EXT_STR);
    if (f_open(&rec_file, name, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) { rec_err = 2; fs_unmount(); return; }
    wav_build_header(hdr, &rec_cfg, 0);
    if (f_write(&rec_file, hdr, 44, &wr) != FR_OK || wr != 44) { rec_err = 3; f_close(&rec_file); fs_unmount(); return; }
    rec_data_bytes = 0;
    rec_chunk_len = 0;
    rec_discard = 32;
    ringbuf_init(&audio_rb, audio_buf, sizeof(audio_buf));
    pdm_init(&audio_rb);
    pdm_start();
    rec_start_ms = millis();
    rec_active = true;
}

static int rec_read_sample(int16_t* s);

static void rec_stop(void)
{
    int16_t s;
    UINT wr = 0;
    uint8_t hdr[44];
    if (!rec_active) return;
    pdm_stop();
    while (rec_read_sample(&s)) {
        rec_chunk[rec_chunk_len++] = (uint8_t)s;
        rec_chunk[rec_chunk_len++] = (uint8_t)(s >> 8);
        if (rec_chunk_len == sizeof(rec_chunk)) rec_flush_chunk();
    }
    rec_flush_chunk();
    uint32_t elapsed = millis() - rec_start_ms;
    if (elapsed > 0 && rec_data_bytes > 0) {
        uint32_t rate = (uint32_t)(((uint64_t)rec_data_bytes * 1000u) / (2u * (uint64_t)elapsed));
        if (rate < 1000u) rate = 1000u;
        if (rate > 48000u) rate = 48000u;
        rec_cfg.sample_rate = rate;
        rec_cfg.byte_rate = rate * rec_cfg.block_align;
    }
    wav_build_header(hdr, &rec_cfg, rec_data_bytes);
    if (f_lseek(&rec_file, 0) == FR_OK) f_write(&rec_file, hdr, 44, &wr);
    f_sync(&rec_file);
    f_close(&rec_file);
    fs_unmount();
    rec_active = false;
    Serial.print("AUTO stop err="); Serial.print(rec_err);
    Serial.print(" bytes="); Serial.print(rec_data_bytes);
    Serial.print(" rate="); Serial.println(rec_cfg.sample_rate);
}

static int rec_read_sample(int16_t* s)
{
    while (rec_discard > 0) {
        int16_t tmp;
        if (!pdm_try_read_sample(&tmp)) return 0;
        rec_discard--;
    }
    return pdm_try_read_sample(s);
}

static void rec_poll_samples(void)
{
    int16_t s;
    while (rec_read_sample(&s)) {
        rec_chunk[rec_chunk_len++] = (uint8_t)s;
        rec_chunk[rec_chunk_len++] = (uint8_t)(s >> 8);
        if (rec_chunk_len == sizeof(rec_chunk)) rec_flush_chunk();
    }
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

    rec_cfg.format = 1;
    rec_cfg.sample_rate = AUDIO_SAMPLE_RATE;
    rec_cfg.channels = AUDIO_CHANNELS;
    rec_cfg.bits = AUDIO_BITS;
    rec_cfg.block_align = (uint16_t)(AUDIO_CHANNELS * (AUDIO_BITS / 8u));
    rec_cfg.byte_rate = rec_cfg.sample_rate * rec_cfg.block_align;
}

void loop()
{
    static uint32_t last_tick = 0;
    static int last_usb = -1;

    if (Serial.available()) {
        static char line[64];
        static size_t n = 0;
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                if (n > 0) {
                    line[n] = 0;
                    if (strncmp(line, "LIST", 4) == 0) {
                        DIR dir; FILINFO fno;
                        Serial.print("LIST mount="); Serial.println(fs_mount_result());
                        if (f_opendir(&dir, "0:/") == FR_OK) {
                            while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
                                if ((fno.fattrib & AM_DIR) == 0) {
                                    Serial.print("  "); Serial.print(fno.fname);
                                    Serial.print(" "); Serial.println((uint32_t)fno.fsize);
                                }
                            }
                            f_closedir(&dir);
                        }
                        Serial.println("LIST done");
                    } else                     if (strncmp(line, "SAMP", 4) == 0) {
                        sample_stats();
                    } else if (strncmp(line, "DOWNLOAD ", 9) == 0) {
                        download_file(line + 9);
                    } else if (strncmp(line, "REC", 3) == 0 && line[3] != ' ') {
                        rec_start();
                        Serial.print("REC started seq="); Serial.println(rec_seq);
                    } else if (strncmp(line, "STOP", 4) == 0) {
                        rec_stop();
                    } else if (strncmp(line, "CHECK", 5) == 0) {
                        check_wav_file();
                    } else if (strncmp(line, "CAPT", 4) == 0) {
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

    /* USB detect -> auto recording */
    int usb = digitalRead(PIN_USB_DETECT);
    if (last_usb < 0) {
        last_usb = usb;
        if (usb == LOW) rec_start();   /* booted with USB detached -> start recording */
    }
    if (usb != last_usb) {
        last_usb = usb;
        if (usb == LOW) {
            rec_start();
        } else {
            rec_stop();
        }
    }
    if (rec_active) rec_poll_samples();
}
