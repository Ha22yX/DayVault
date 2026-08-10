#include <Arduino.h>
#include "stm32l4xx_hal.h"
#include "Config.h"
#include "SdCard.h"
#include "Fs.h"
#include "ff.h"
#include "RingBuf.h"
#include "PdmCapture.h"
#include "WavFile.h"
#include "NoiseReduction.h"
#include "DeviceTime.h"
#include "Battery.h"
#include "TransferBuffer.h"
#include "Crc32.h"
#include "ExportProtocol.h"
#include "WinUsbDevice.h"
#include <string.h>

extern "C" void SystemClock_Config(void);
extern "C" void CDC_init(void);
extern "C" void CDC_deInit(void);

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

volatile uint32_t g_dbg_step __attribute__((section(".noinit")));
volatile uint32_t g_dfu_jump __attribute__((section(".noinit")));
volatile uint32_t g_fault_pc __attribute__((section(".noinit")));
volatile uint32_t g_fault_lr __attribute__((section(".noinit")));
volatile uint32_t g_fault_step __attribute__((section(".noinit")));
volatile uint32_t g_fault_cfsr __attribute__((section(".noinit")));
volatile uint32_t g_fault_bfar __attribute__((section(".noinit")));
volatile uint32_t g_fault_mfar __attribute__((section(".noinit")));
volatile uint32_t g_fault_hfsr __attribute__((section(".noinit")));

static void dbg_step_set(uint32_t v) { g_dbg_step = v; }

extern "C" void HardFault_Handler(void)
{
    uint32_t sp = __get_MSP();
    g_fault_pc = *(volatile uint32_t*)(sp + 24u);
    g_fault_lr = *(volatile uint32_t*)(sp + 20u);
    g_fault_step = g_dbg_step;
    g_fault_cfsr = *(volatile uint32_t*)0xE000ED28u;
    g_fault_bfar = *(volatile uint32_t*)0xE000ED38u;
    g_fault_mfar = *(volatile uint32_t*)0xE000ED34u;
    g_fault_hfsr = *(volatile uint32_t*)0xE000ED2Cu;
    g_dbg_step = 0xDDDDDDDDu;
    NVIC_SystemReset();
    while (1) { }
}

static void dbg_iwdg_init(void)
{
    if ((RCC->CSR & RCC_CSR_LSION) == 0) RCC->CSR |= RCC_CSR_LSION;
    uint32_t t0 = HAL_GetTick();
    while ((RCC->CSR & RCC_CSR_LSIRDY) == 0 && (HAL_GetTick() - t0) < 50) { }
    IWDG->KR = 0x5555;
    IWDG->PR = 6;             /* /256 -> 125 Hz at 32 kHz LSI */
    IWDG->RLR = 625;          /* ~5 s */
    IWDG->KR = 0xCCCC;
    IWDG->KR = 0xAAAA;
}

static void dbg_iwdg_kick(void) { IWDG->KR = 0xAAAA; }

static void dbg_report_last_step(void)
{
    uint32_t step = g_dbg_step;
    uint32_t csr = RCC->CSR;
    if (step == 0xDDDDDDDDu) {
        Serial.print("DBG previous=FAULT");
    } else if (step != 0) {
        Serial.print("DBG previous=step"); Serial.print(step);
    } else {
        Serial.print("DBG previous=clean");
    }
    Serial.print(" csr="); Serial.print(csr, HEX);
    RCC->CSR |= RCC_CSR_RMVF;
    Serial.println();
}

static uint8_t audio_buf[PDM_RING_BYTES];
static RingBuf audio_rb;

static void print_benchmark(const char* layer, uint32_t byte_count,
                            uint32_t duration_ms, uint32_t crc32)
{
    if (duration_ms == 0) duration_ms = 1;
    const uint32_t kib_per_second = (uint32_t)(
        ((uint64_t)byte_count * 1000u) / ((uint64_t)duration_ms * 1024u));
    char crc_text[9];
    snprintf(crc_text, sizeof(crc_text), "%08lX", (unsigned long)crc32);
    Serial.print("BENCH "); Serial.print(layer);
    Serial.print(" bytes="); Serial.print(byte_count);
    Serial.print(" ms="); Serial.print(duration_ms);
    Serial.print(" kib_s="); Serial.print(kib_per_second);
    Serial.print(" crc32="); Serial.println(crc_text);
}

static int rec_name_cmp(const char* a, const char* b);

static void check_wav_file(void)
{
    DIR dir;
    FILINFO fno;
    char best[40];
    best[0] = 0;
    Serial.print("CHECK mount="); Serial.println(fs_mount_result());
    if (f_opendir(&dir, "0:/") == FR_OK) {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
            if ((fno.fattrib & AM_DIR) != 0) continue;
            if (strncmp(fno.fname, REC_DIR_STR, strlen(REC_DIR_STR)) != 0) continue;
            char* dot = strrchr(fno.fname, '.');
            if (dot == NULL || strcmp(dot + 1, REC_EXT_STR) != 0) continue;
            if (best[0] == 0 || rec_name_cmp(fno.fname, best) > 0) {
                strncpy(best, fno.fname, sizeof(best) - 1);
                best[sizeof(best) - 1] = 0;
            }
        }
        f_closedir(&dir);
    }
    if (best[0] == 0) { Serial.println("CHECK none"); return; }

    FIL f;
    uint8_t hdr[44];
    UINT rd = 0;
    char name[44];
    snprintf(name, sizeof(name), "0:/%s", best);
    Serial.print("CHECK file="); Serial.println(name);
    if (f_open(&f, name, FA_READ) != FR_OK) { Serial.println("CHECK open FAIL"); return; }
    FRESULT r = f_read(&f, hdr, 44, &rd);
    Serial.print("CHECK hdr_fr="); Serial.print((int)r);
    Serial.print(" rd="); Serial.print(rd);
    if (rd == 44) {
        bool riff = (hdr[0]=='R'&&hdr[1]=='I'&&hdr[2]=='F'&&hdr[3]=='F'&&hdr[8]=='W'&&hdr[9]=='A'&&hdr[10]=='V'&&hdr[11]=='E');
        uint32_t data_sz = (uint32_t)hdr[40] | ((uint32_t)hdr[41]<<8) | ((uint32_t)hdr[42]<<16) | ((uint32_t)hdr[43]<<24);
        uint16_t ch = (uint16_t)(hdr[22] | (hdr[23]<<8));
        uint32_t rate = (uint32_t)hdr[24] | ((uint32_t)hdr[25]<<8) | ((uint32_t)hdr[26]<<16) | ((uint32_t)hdr[27]<<24);
        uint32_t bytes = (uint32_t)hdr[4] | ((uint32_t)hdr[5]<<8) | ((uint32_t)hdr[6]<<16) | ((uint32_t)hdr[7]<<24);
        Serial.print(" riff="); Serial.print(riff ? 1 : 0);
        Serial.print(" ch="); Serial.print(ch);
        Serial.print(" rate="); Serial.print(rate);
        Serial.print(" data="); Serial.print(data_sz);
        Serial.print(" riff_sz="); Serial.print(bytes);
        Serial.println();
    }
    f_close(&f);
}

static void lfn_bringup_test(void)
{
    FIL f;
    DIR dir;
    FILINFO fno;
    UINT wr = 0;
    const char* n1 = "0:/LTEST-20260809-1110_5m32s.WAV";
    const char* n2 = "0:/LTEST-20260809-1110_5m33s.WAV";
    Serial.print("LTEST mount="); Serial.println(fs_mount_result());
    FRESULT r = f_open(&f, n1, FA_CREATE_NEW | FA_WRITE);
    Serial.print(" create_fr="); Serial.print((int)r);
    if (r == FR_OK) {
        r = f_write(&f, "LTEST-DAYVAULT", 14, &wr);
        Serial.print(" write_fr="); Serial.print((int)r);
        f_close(&f);
    }
    int found = 0;
    if (f_opendir(&dir, "0:/") == FR_OK) {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
            if (strcmp(fno.fname, "LTEST-20260809-1110_5m32s.WAV") == 0) found = 1;
        }
        f_closedir(&dir);
    }
    Serial.print(" readdir_found="); Serial.println(found);
    r = f_rename(n1, n2);
    Serial.print(" rename_fr="); Serial.print((int)r);
    r = f_unlink(n2);
    Serial.print(" unlink_fr="); Serial.println((int)r);
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
    if (f_open(&f, path, FA_READ) != FR_OK) { Serial.println("DL open FAIL"); return; }
    Serial.print("DLSTART ");
    Serial.println((uint32_t)f_size(&f));
    uint32_t total_read = 0, total_written = 0;
    while (f_read(&f, buf, sizeof(buf), &rd) == FR_OK && rd > 0) {
        dbg_iwdg_kick();
        total_read += rd;
        total_written += (uint32_t)Serial.write(buf, rd);
    }
    f_close(&f);
    Serial.print("DLEND read="); Serial.print(total_read);
    Serial.print(" wr="); Serial.println(total_written);
}

static bool serial_write_all(const uint8_t* data, size_t length)
{
    size_t offset = 0;
    uint32_t last_progress = millis();
    while (offset < length) {
        dbg_iwdg_kick();
        const size_t written = Serial.write(data + offset, length - offset);
        if (written > 0) {
            offset += written;
            last_progress = millis();
        } else if ((millis() - last_progress) >= 10000u) {
            return false;
        } else {
            delay(1);
        }
    }
    return true;
}

static void download_file2(const char* fname)
{
    FIL f;
    uint8_t* const buf = transfer_buffer(0);
    UINT rd = 0;
    char path[32];

    snprintf(path, sizeof(path), "0:/%s", fname);
    if (!fs_mount()) { Serial.println("DL2 mount FAIL"); return; }
    if (f_open(&f, path, FA_READ) != FR_OK) { Serial.println("DL2 open FAIL"); return; }
    uint32_t size = (uint32_t)f_size(&f);
    Serial.print("DLSTART "); Serial.println(size);
    uint32_t total = 0;
    uint32_t crc = 0;
    const uint32_t started_ms = millis();
    while (total < size) {
        dbg_iwdg_kick();
        if (f_read(&f, buf, (UINT)transfer_buffer_size(), &rd) != FR_OK || rd == 0) break;
        /* Stream continuously; Serial.write blocks when the CDC TX ring is full,
           providing natural backpressure without per-chunk ACK round-trips. */
        if (!serial_write_all(buf, rd)) break;
        total += rd;
        crc = crc32_update(crc, buf, rd);
    }
    Serial.flush();
    const uint32_t elapsed_ms = millis() - started_ms;
    f_close(&f);
    Serial.print("DLEND read="); Serial.println(total);
    print_benchmark("e2e", total, elapsed_ms, crc);
}

static void download_file_get2(const ExportRequest* request)
{
    FIL file;
    char path[EXPORT_FILENAME_BYTES + 4];
    snprintf(path, sizeof(path), "0:/%s", request->filename);
    if (!fs_mount()) {
        Serial.println("GET2FAIL reason=mount");
        return;
    }
    if (f_open(&file, path, FA_READ) != FR_OK) {
        Serial.println("GET2FAIL reason=open");
        return;
    }

    const uint32_t total_size = (uint32_t)f_size(&file);
    if (request->offset > total_size) {
        Serial.print("GET2FAIL reason=offset size="); Serial.println(total_size);
        f_close(&file);
        return;
    }
    if (f_lseek(&file, request->offset) != FR_OK) {
        Serial.println("GET2FAIL reason=seek");
        f_close(&file);
        return;
    }

    const uint32_t length = total_size - request->offset;
    Serial.print("GET2START size="); Serial.print(total_size);
    Serial.print(" offset="); Serial.print(request->offset);
    Serial.print(" length="); Serial.println(length);

    uint32_t sent = 0;
    uint32_t crc = 0;
    uint8_t* const buffer = transfer_buffer(0);
    const uint32_t started_ms = millis();
    while (sent < length) {
        dbg_iwdg_kick();
        const uint32_t remaining = length - sent;
        const UINT wanted = (UINT)(remaining < transfer_buffer_size()
            ? remaining : transfer_buffer_size());
        UINT read = 0;
        if (f_read(&file, buffer, wanted, &read) != FR_OK || read == 0) break;
        if (!serial_write_all(buffer, read)) break;
        crc = crc32_update(crc, buffer, read);
        sent += read;
    }
    Serial.flush();
    const uint32_t elapsed_ms = millis() - started_ms;
    f_close(&file);

    char crc_text[9];
    snprintf(crc_text, sizeof(crc_text), "%08lX", (unsigned long)crc);
    Serial.print("GET2END sent="); Serial.print(sent);
    Serial.print(" crc32="); Serial.println(crc_text);
    print_benchmark("e2e", sent, elapsed_ms, crc);
}

static bool winusb_wait_configured(uint32_t timeout_ms)
{
    const uint32_t started = millis();
    while (!winusb_is_configured()) {
        dbg_iwdg_kick();
        if (digitalRead(PIN_USB_DETECT) == LOW ||
            (millis() - started) >= timeout_ms) {
            return false;
        }
        delay(1);
    }
    return true;
}

static bool winusb_write_transfer(const uint8_t* data, size_t length)
{
    const uint32_t timeout_ms = 10000u;
    uint32_t started = millis();
    while (!winusb_send(data, length)) {
        dbg_iwdg_kick();
        if (!winusb_is_configured() || (millis() - started) >= timeout_ms) {
            return false;
        }
        delay(1);
    }

    started = millis();
    while (winusb_tx_busy()) {
        dbg_iwdg_kick();
        if (!winusb_is_configured() || (millis() - started) >= timeout_ms) {
            return false;
        }
        delay(1);
    }
    return true;
}

static bool winusb_write_all(const uint8_t* data, size_t length)
{
    return winusb_write_transfer(data, length);
}

static bool winusb_wait_ack(uint32_t timeout_ms)
{
    const uint32_t started = millis();
    while (!winusb_ack_received()) {
        dbg_iwdg_kick();
        if (!winusb_is_configured() || (millis() - started) >= timeout_ms) {
            return false;
        }
        delay(1);
    }
    return true;
}

static const uint32_t kWinUsbTrailerReserve = 192u;
static const uint32_t kWinUsbTransferBytes =
    TRANSFER_BUFFER_BYTES - kWinUsbTrailerReserve;
static const uint32_t kWinUsbAckTimeoutMs = 30000u;

static bool winusb_write_completion_frame(uint8_t* buffer, size_t data_length,
                                          uint32_t sent, uint32_t elapsed_ms,
                                          uint32_t crc)
{
    if (buffer == nullptr || data_length > kWinUsbTransferBytes) return false;
    size_t frame_length = data_length;
    const size_t trailer_length = export_format_completion(
        (char*)(buffer + frame_length),
        transfer_buffer_size() - frame_length,
        "bulk", sent, elapsed_ms, crc);
    if (trailer_length == 0u) return false;
    frame_length += trailer_length;

    if ((frame_length % 64u) == 0u) {
        if (frame_length >= transfer_buffer_size()) return false;
        buffer[frame_length++] = '\n';
    }
    return winusb_write_all(buffer, frame_length);
}

static void download_file_bulk2(const ExportRequest* request)
{
    FIL file;
    char path[EXPORT_FILENAME_BYTES + 4];
    snprintf(path, sizeof(path), "0:/%s", request->filename);
    if (!fs_mount()) {
        Serial.println("BULK2FAIL reason=mount");
        return;
    }
    if (f_open(&file, path, FA_READ) != FR_OK) {
        Serial.println("BULK2FAIL reason=open");
        return;
    }

    const uint32_t total_size = (uint32_t)f_size(&file);
    f_close(&file);
    if (request->offset > total_size) {
        Serial.print("BULK2FAIL reason=offset size="); Serial.println(total_size);
        return;
    }

    const uint32_t length = total_size - request->offset;
    Serial.print("BULK2READY size="); Serial.print(total_size);
    Serial.print(" offset="); Serial.print(request->offset);
    Serial.print(" length="); Serial.println(length);
    Serial.flush();
    delay(100);

    CDC_deInit();
    delay(100);
    bool started = winusb_start();
    bool connected = started && winusb_wait_configured(15000u);
    uint32_t sent = 0;
    uint32_t crc = 0;
    uint32_t elapsed_ms = 0;
    bool completion_sent = false;

    if (connected && f_open(&file, path, FA_READ) == FR_OK) {
        if (f_lseek(&file, request->offset) == FR_OK) {
            char header[96];
            const int header_length = snprintf(
                header, sizeof(header),
                "GET2START size=%lu offset=%lu length=%lu\r\n",
                (unsigned long)total_size,
                (unsigned long)request->offset,
                (unsigned long)length);
            connected = header_length > 0 &&
                winusb_write_all((const uint8_t*)header, (size_t)header_length);

            uint8_t* const buffer = transfer_buffer(0);
            const uint32_t transfer_started = millis();
            while (connected && sent < length) {
                dbg_iwdg_kick();
                const uint32_t remaining = length - sent;
                const UINT wanted = (UINT)(remaining < kWinUsbTransferBytes
                    ? remaining : kWinUsbTransferBytes);
                UINT read = 0;
                if (f_read(&file, buffer, wanted, &read) != FR_OK || read == 0) {
                    connected = false;
                    break;
                }
                crc = crc32_update(crc, buffer, read);
                sent += read;
                if (sent == length) {
                    elapsed_ms = millis() - transfer_started;
                    connected = winusb_write_completion_frame(
                        buffer, read, sent, elapsed_ms, crc);
                    completion_sent = connected;
                } else if (read != wanted || !winusb_write_all(buffer, read)) {
                    connected = false;
                    break;
                }
            }
            elapsed_ms = millis() - transfer_started;
            if (connected && length == 0u) {
                connected = winusb_write_completion_frame(
                    buffer, 0u, 0u, elapsed_ms, 0u);
                completion_sent = connected;
            }
        }
        f_close(&file);
    }

    if (connected && completion_sent) winusb_wait_ack(kWinUsbAckTimeoutMs);

    if (started) winusb_stop();
    delay(100);
    CDC_init();
}

static void benchmark_winusb(void)
{
    const uint32_t total_size = 2u * 1024u * 1024u;
    uint8_t* const buffer = transfer_buffer(1);
    const uint32_t block_size = kWinUsbTransferBytes;
    for (uint32_t i = 0; i < block_size; ++i) buffer[i] = (uint8_t)i;

    Serial.print("BULK2READY size="); Serial.print(total_size);
    Serial.print(" offset=0 length="); Serial.print(total_size);
    Serial.println(" mode=bench");
    Serial.flush();
    delay(100);

    CDC_deInit();
    delay(100);
    const bool started = winusb_start();
    bool connected = started && winusb_wait_configured(15000u);
    uint32_t sent = 0;
    uint32_t crc = 0;
    uint32_t elapsed_ms = 0;
    bool completion_sent = false;

    if (connected) {
        char header[96];
        const int header_length = snprintf(
            header, sizeof(header),
            "GET2START size=%lu offset=0 length=%lu\r\n",
            (unsigned long)total_size, (unsigned long)total_size);
        connected = header_length > 0 &&
            winusb_write_all((const uint8_t*)header, (size_t)header_length);

        const uint32_t transfer_started = millis();
        while (connected && sent < total_size) {
            dbg_iwdg_kick();
            uint32_t length = block_size;
            if (sent + length > total_size) length = total_size - sent;
            crc = crc32_update(crc, buffer, length);
            sent += length;
            bool write_ok;
            if (sent == total_size) {
                elapsed_ms = millis() - transfer_started;
                write_ok = winusb_write_completion_frame(
                    buffer, length, sent, elapsed_ms, crc);
                completion_sent = write_ok;
            } else {
                write_ok = winusb_write_all(buffer, length);
            }
            if (!write_ok) {
                connected = false;
                break;
            }
        }
        elapsed_ms = millis() - transfer_started;
    }

    if (connected && completion_sent) winusb_wait_ack(kWinUsbAckTimeoutMs);

    if (started) winusb_stop();
    delay(100);
    CDC_init();
}

/* Jump into the system bootloader. Called from the very start of setup() after a
   DFU-requested reset, so the chip is in a near-pristine state: IWDG not yet
   armed, USB reset, clocks restored to MSI. */
static void dfu_jump_from_boot(void)
{
    g_dfu_jump = 0;
    HAL_DeInit();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    __disable_irq();

    __HAL_RCC_USB_FORCE_RESET();      /* clean USB peripheral for the bootloader */
    __HAL_RCC_USB_RELEASE_RESET();

    RCC->CFGR &= ~RCC_CFGR_SW;        /* SYSCLK -> MSI first, then PLL off */
    while ((RCC->CFGR & RCC_CFGR_SWS) != 0u) { }
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY) { }

    uint32_t msp = *(volatile uint32_t *)SYSTEM_MEMORY_BASE;
    if ((msp & 0xFFF00000u) != 0x20000000u) {
        NVIC_SystemReset();   /* invalid bootloader stack: reset instead */
        return;
    }
    __set_MSP(msp);
    ((void (*)(void)) * (volatile uint32_t *)(SYSTEM_MEMORY_BASE + 4u))();
    while (1) { }
}

/* Request DFU: set the noinit flag and reset. On reboot, setup() checks the flag
   (before the IWDG is armed / the app initializes) and jumps into the bootloader
   from a clean state. */
static void dfu_enter(void)
{
    g_dfu_jump = 0xDFA5u;
    NVIC_SystemReset();
    while (1) { }
}

static WavConfig rec_cfg;
static FIL rec_file;
static bool rec_active = false;
static uint32_t rec_data_bytes = 0;
#define REC_SYNC_INTERVAL_MS 1000u
#define REC_CIRC_INTERVAL_MS 30000u
static uint32_t rec_circ_last_ms = 0;
static uint32_t rec_seq = 1;
static uint32_t rec_start_ms = 0;
static uint32_t rec_last_sync_ms = 0;
static uint32_t rec_discard = 0;
static uint8_t rec_chunk[64];
static size_t rec_chunk_len = 0;
static int rec_err = 0;
static char rec_name[40];          /* current file path (set in rec_start) */
static uint8_t rec_name_kind = 0;  /* 0 = seq fallback, 1 = timestamp */
#define CIRC_FREE_BYTES (64u * 1024u * 1024u)   /* delete oldest below this free space */

static void rec_flush_chunk(void)
{
    UINT wr = 0;
    if (rec_chunk_len > 0) {
        if (f_write(&rec_file, rec_chunk, (UINT)rec_chunk_len, &wr) == FR_OK) rec_data_bytes += wr;
        rec_chunk_len = 0;
    }
}

static void rec_duration_str(char* out, size_t len, uint32_t secs)
{
    uint32_t h = secs / 3600u, m = (secs / 60u) % 60u, s = secs % 60u;
    if (h > 0) snprintf(out, len, "_%luh%02lum%02lus", (unsigned long)h, (unsigned)m, (unsigned)s);
    else       snprintf(out, len, "_%lum%02lus",       (unsigned)m,    (unsigned)s);
}

static int rec_name_cmp(const char* a, const char* b)
{
    bool ta = (a[3] == '-');   /* REC-YYYYMMDD-HHMM... : timestamp */
    bool tb = (b[3] == '-');
    if (ta != tb) return ta ? 1 : -1;   /* timestamp names always "newer" than seq names */
    return strcmp(a, b);
}

static void rec_start(void)
{
    UINT wr = 0;
    uint8_t hdr[44];
    if (rec_active) return;
    rec_err = 0;
    if (!fs_mount()) { rec_err = 1; return; }

    rec_name_kind = 0;
    if (dt_time_is_set()) {
        char stem[16];
        dt_format_stem(stem, sizeof(stem));
        for (uint8_t n = 0; n < 10; n++) {
            if (n == 0) snprintf(rec_name, sizeof(rec_name), "0:/REC-%s.WAV", stem);
            else        snprintf(rec_name, sizeof(rec_name), "0:/REC-%s_%u.WAV", stem, (unsigned)n);
            if (f_open(&rec_file, rec_name, FA_CREATE_NEW | FA_WRITE) == FR_OK) { rec_name_kind = 1; break; }
        }
    }
    if (!rec_name_kind) {
        rec_seq = fs_next_sequence();
        snprintf(rec_name, sizeof(rec_name), "0:/%s%03u.%s", REC_DIR_STR, (unsigned)rec_seq, REC_EXT_STR);
        if (f_open(&rec_file, rec_name, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) { rec_err = 2; fs_unmount(); return; }
    }

    wav_build_header(hdr, &rec_cfg, 0);
    if (f_write(&rec_file, hdr, 44, &wr) != FR_OK || wr != 44) { rec_err = 3; f_close(&rec_file); fs_unmount(); return; }
    f_sync(&rec_file);               /* durable header before any data (power-loss safety) */
    rec_data_bytes = 0;
    rec_chunk_len = 0;
    rec_discard = 32;
    ringbuf_init(&audio_rb, audio_buf, sizeof(audio_buf));
    pdm_init(&audio_rb);
    pdm_start();
    rec_start_ms = millis();
    rec_last_sync_ms = millis();
    rec_active = true;
    fs_make_space(CIRC_FREE_BYTES, rec_name + 3);   /* free room before a long recording */
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
    if (rec_name_kind == 1) {
        uint32_t secs = rec_cfg.byte_rate ? (rec_data_bytes / (uint32_t)rec_cfg.byte_rate) : 0;
        char dur[24], newname[64];
        rec_duration_str(dur, sizeof(dur), secs);
        char* dot = strrchr(rec_name, '.');
        if (dot != NULL) {
            snprintf(newname, sizeof(newname), "%.*s%s.WAV", (int)(dot - rec_name), rec_name, dur);
            if (f_rename(rec_name, newname) == FR_OK) strncpy(rec_name, newname, sizeof(rec_name) - 1);
        }
    }
    f_close(&rec_file);
    rec_active = false;
    Serial.print("AUTO stop err="); Serial.print(rec_err);
    Serial.print(" bytes="); Serial.print(rec_data_bytes);
    Serial.print(" rate="); Serial.println(rec_cfg.sample_rate);
}

static int rec_read_sample(int16_t* s)
{
    while (rec_discard > 0) {
        int16_t tmp;
        if (pdm_dma_read(&tmp, 1) != 1) return 0;
        rec_discard--;
    }
    return pdm_dma_read(s, 1);
}

static void rec_poll_samples(void)
{
    int16_t tmp[128];
    int n = pdm_dma_read(tmp, 128);
    for (int i = 0; i < n; i++) {
        int16_t s = tmp[i];
        rec_chunk[rec_chunk_len++] = (uint8_t)s;
        rec_chunk[rec_chunk_len++] = (uint8_t)(s >> 8);
        if (rec_chunk_len == sizeof(rec_chunk)) rec_flush_chunk();
    }
}

static void rec_checkpoint(void)
{
    if (!rec_active) return;
    uint8_t hdr[44];
    UINT wr = 0;
    wav_build_header(hdr, &rec_cfg, rec_data_bytes);
    if (f_lseek(&rec_file, 0) == FR_OK && f_write(&rec_file, hdr, 44, &wr) == FR_OK) {
        f_sync(&rec_file);
    }
    f_lseek(&rec_file, 44u + rec_data_bytes);
    rec_last_sync_ms = millis();
}

static void exti_usb_wake_enable(void)
{
    SYSCFG->EXTICR[2] |= SYSCFG_EXTICR3_EXTI9_PA;   /* PA9 -> EXTI9 */
    EXTI->IMR1 |= EXTI_IMR1_IM9;
    EXTI->RTSR1 |= EXTI_RTSR1_RT9;                  /* rising edge = USB attach */
    EXTI->FTSR1 |= EXTI_FTSR1_FT9;
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

static void low_battery_enter_stop(void)
{
    if (rec_active) rec_stop();   /* clean finalize: rebuild header + sync + close */
    dt_set_wake(4);                       /* wake every 4 s to refresh IWDG + re-check */
    dbg_iwdg_kick();                      /* refresh right before sleeping */
    HAL_SuspendTick();
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
    /* --- resumed (RTC wake or USB EXTI) --- */
    SystemClock_Config();                 /* PLL off in STOP -> restore 80 MHz */
    HAL_ResumeTick();
}

void setup()
{
    if (g_dfu_jump == 0xDFA5u) dfu_jump_from_boot();   /* DFU requested -> enter bootloader before app init */
    SystemClock_Config();
    pinMode(PIN_USB_DETECT, INPUT);
    pinMode(PIN_BOOT0, INPUT_PULLDOWN);

    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && (millis() - t) < 3000) { }

    Serial.println("DV step2 ready");
    dbg_report_last_step();
    Serial.print("usb_detect="); Serial.print(digitalRead(PIN_USB_DETECT));
    Serial.print(" boot="); Serial.println(digitalRead(PIN_BOOT0));

    fs_mount();
    fs_free_bytes();          /* ~10 s exFAT bitmap scan, caches free_clst; before IWDG so no reset risk */

    dbg_iwdg_init();
    dt_init();
    bat_init();
    exti_usb_wake_enable();

    rec_cfg.format = 1;
    rec_cfg.sample_rate = AUDIO_SAMPLE_RATE;
    rec_cfg.channels = AUDIO_CHANNELS;
    rec_cfg.bits = AUDIO_BITS;
    rec_cfg.block_align = (uint16_t)(AUDIO_CHANNELS * (AUDIO_BITS / 8u));
    rec_cfg.byte_rate = rec_cfg.sample_rate * rec_cfg.block_align;
}

#define BAT_SLEEP_MV   3000u
#define BAT_RESUME_MV  3300u
#define BAT_HIST_N     20u   /* 20 s of history */
#define BAT_WIN_N      10u   /* average over the previous 10 s */
#define BAT_SLEEP_SECS 10u   /* the 10 s average must stay below for 10 continuous seconds */

static uint16_t bat_hist[20];
static uint8_t  bat_hist_idx = 0;
static uint8_t  bat_hist_cnt = 0;

static void bat_hist_push(uint16_t mv)
{
    bat_hist[bat_hist_idx] = mv;
    bat_hist_idx = (bat_hist_idx + 1) % BAT_HIST_N;
    if (bat_hist_cnt < BAT_HIST_N) bat_hist_cnt++;
}

/* Average of the previous 10 s (the 10 most recent samples). */
static uint16_t bat_win_avg(void)
{
    if (bat_hist_cnt < BAT_WIN_N) return 0xFFFFu;   /* not enough history yet */
    uint32_t sum = 0;
    for (int i = 0; i < (int)BAT_WIN_N; i++) {
        sum += bat_hist[(bat_hist_idx + BAT_HIST_N - 1u - (uint8_t)i) % BAT_HIST_N];
    }
    return (uint16_t)(sum / BAT_WIN_N);
}

static void bat_hist_reset(void)
{
    bat_hist_idx = 0;
    bat_hist_cnt = 0;
}

void loop()
{
    static uint32_t last_tick = 0;
    static int last_usb = -1;
    static int usb_pending = -1;
    static uint32_t usb_pending_since = 0;
    static uint8_t bat_asleep = 0;
    static uint32_t last_bat_ms = 0;
    static uint32_t low_secs = 0;

loop_restart:
    dbg_iwdg_kick();

    if (bat_asleep) {
        /* periodic wake (RTC 4 s): push a reading; resume when the previous-10 s
           average is above the resume threshold or USB attaches */
        uint16_t mv = bat_millivolts();
        bat_hist_push(mv);
        uint16_t avg = bat_win_avg();
        if (avg >= BAT_RESUME_MV || digitalRead(PIN_USB_DETECT) == HIGH) {
            bat_asleep = 0;              /* charged (e.g. USB) -> resume normal operation */
            dt_wake_off();               /* stop periodic 4 s RTC wake */
            bat_hist_reset();
            low_secs = 0;
        } else {
            low_battery_enter_stop();    /* still low -> back to sleep (re-arms RTC wake) */
            goto loop_restart;           /* skip remaining checks this pass */
        }
    } else if ((millis() - last_bat_ms) >= 1000) {
        last_bat_ms = millis();
        uint16_t mv = bat_millivolts();
        bat_hist_push(mv);
        uint16_t avg = bat_win_avg();
        /* each second, look at the previous-10 s average; it must stay below the
           threshold for BAT_SLEEP_SECS continuous seconds before sleeping */
        if (avg != 0xFFFFu && avg < BAT_SLEEP_MV && digitalRead(PIN_USB_DETECT) == LOW) {
            if (low_secs < 200u) low_secs++;
            if (low_secs >= BAT_SLEEP_SECS) {
                low_battery_enter_stop();   /* returns on wake; re-checks below */
                bat_asleep = 1;             /* latched until avg > BAT_RESUME_MV or USB */
                low_secs = 0;
                goto loop_restart;          /* skip remaining checks this pass */
            }
        } else {
            low_secs = 0;
        }
    }

    if (Serial.available()) {
        static char line[128];
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
                    } else                     if (strncmp(line, "DMAT", 4) == 0) {
                        dbg_step_set(10);
                        pdm_init(&audio_rb);
                        dbg_step_set(20);
                        pdm_start();
                        dbg_step_set(30);
                        uint32_t e = millis() + 2000;
                        uint32_t cnt = 0;
                        uint32_t lo = 0xFFFFFFFF;
                        int64_t ssum = 0, ssq = 0;
                        int32_t mn = 32767, mx = -32768;
                        while (millis() < e) {
                            int16_t tmp[256];
                            int n = pdm_dma_read(tmp, 256);
                            cnt += (uint32_t)n;
                            for (int i = 0; i < n; i++) {
                                ssum += tmp[i];
                                ssq += (int64_t)tmp[i] * tmp[i];
                                if (tmp[i] < mn) mn = tmp[i];
                                if (tmp[i] > mx) mx = tmp[i];
                            }
                            if (n < lo) lo = (uint32_t)n;
                            if (millis() % 250 == 0) dbg_step_set(40);
                        }
                        dbg_step_set(50);
                        uint32_t ch = 0;
                        int32_t halv = (int32_t)(DFSDM1_Filter1->FLTRDATAR & 0xFFFFu);
                        int32_t rms = (cnt > 0) ? (int32_t)sqrt((double)ssq / cnt) : 0;
                        Serial.print("DMAT cnt="); Serial.print(cnt);
                        Serial.print(" rms="); Serial.print(rms);
                        Serial.print(" dc="); Serial.print((int32_t)(ssum / (cnt ? (int64_t)cnt : 1)));
                        Serial.print(" mn="); Serial.print(mn); Serial.print(" mx="); Serial.print(mx);
                        Serial.print(" hal="); Serial.print(halv);
                        Serial.print(" ndtr="); Serial.print(DMA1_Channel5->CNDTR);
                        Serial.print(" start="); Serial.print(pdm_start_result());
                        Serial.println();
                        pdm_stop();
                        dbg_step_set(0);
                    } else if (strncmp(line, "ITST", 4) == 0) {
                        pdm_itst_start();
                        uint32_t e = millis() + 2000;
                        while (millis() < e) { }
                        Serial.print("ITST isr="); Serial.println(pdm_isr_count_now());
                        pdm_stop();
                    } else if (strncmp(line, "SAMP", 4) == 0) {
                        sample_stats();
                    } else if (strncmp(line, "DOWNLOAD ", 9) == 0) {
                        download_file(line + 9);
                    } else if (strcmp(line, "BULKSPEED") == 0) {
                        if (rec_active) rec_stop();
                        benchmark_winusb();
                    } else if (strncmp(line, "BULK2 ", 6) == 0) {
                        char request_line[128];
                        snprintf(request_line, sizeof(request_line),
                                 "GET2 %s", line + 6);
                        ExportRequest request;
                        if (export_parse_get2(request_line, &request)) {
                            if (rec_active) rec_stop();
                            download_file_bulk2(&request);
                        } else {
                            Serial.println("BULK2FAIL reason=request");
                        }
                    } else if (strncmp(line, "GET2 ", 5) == 0) {
                        ExportRequest request;
                        if (export_parse_get2(line, &request)) download_file_get2(&request);
                        else Serial.println("GET2FAIL reason=request");
                    } else if (strncmp(line, "DL2 ", 4) == 0) {
                        download_file2(line + 4);
                    } else if (strncmp(line, "REC", 3) == 0 && line[3] != ' ') {
                        rec_start();
                        Serial.print("REC started seq="); Serial.print(rec_seq);
                        Serial.print(" name="); Serial.println(rec_name);
                    } else if (strncmp(line, "STOP", 4) == 0) {
                        rec_stop();
                    } else if (strncmp(line, "CHECK", 5) == 0) {
                        check_wav_file();
                    } else if (strncmp(line, "CIRC", 4) == 0) {
                        fs_mount_result();
                        uint64_t fb = fs_free_bytes();
                        int del = fs_make_space(CIRC_FREE_BYTES, rec_active ? rec_name + 3 : NULL);
                        uint64_t fa = fs_free_bytes();
                        Serial.print("CIRC free_before="); Serial.print((uint32_t)(fb >> 20)); Serial.print("MB");
                        Serial.print(" deleted="); Serial.print(del);
                        Serial.print(" free_after="); Serial.print((uint32_t)(fa >> 20)); Serial.println("MB");
                    } else if (strncmp(line, "DELOLDEST", 9) == 0) {
                        int del = fs_delete_oldest(rec_active ? rec_name + 3 : NULL);
                        Serial.print("DELOLDEST deleted="); Serial.println(del);
                    } else if (strncmp(line, "LTEST", 5) == 0) {
                        lfn_bringup_test();
                    } else if (strncmp(line, "CAPT", 4) == 0) {
                        record_test(5);
                    } else if (strncmp(line, "SDSPEED", 7) == 0) {
                        DIR dir; FILINFO fno; char fname[40]; fname[0] = 0;
                        uint64_t bestsz = 0;
                        if (f_opendir(&dir, "0:/") == FR_OK) {
                            while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
                                if (fno.fattrib & AM_DIR) continue;
                                if (fno.fsize > bestsz) { bestsz = fno.fsize; strncpy(fname, fno.fname, sizeof(fname) - 1); }
                            }
                            f_closedir(&dir);
                        }
                        FIL f2;
                        uint8_t* const big = transfer_buffer(0);
                        char p2[48];
                        snprintf(p2, sizeof(p2), "0:/%s", fname);
                        if (f_open(&f2, p2, FA_READ) != FR_OK) { Serial.println("SDSPEED open FAIL"); return; }
                        uint32_t rd2 = 0, n2 = 0;
                        uint32_t crc2 = 0;
                        sd_reset_stats();
                        uint32_t t0 = millis();
                        while (rd2 < 2u * 1024u * 1024u) {
                            dbg_iwdg_kick();
                            UINT r2;
                            if (f_read(&f2, big, (UINT)transfer_buffer_size(), &r2) != FR_OK || r2 == 0) break;
                            crc2 = crc32_update(crc2, big, r2);
                            rd2 += r2; n2++;
                        }
                        uint32_t dt = millis() - t0;
                        f_close(&f2);
                        Serial.print("SDSPEED bytes="); Serial.print(rd2);
                        Serial.print(" ms="); Serial.print(dt);
                        Serial.print(" KB/s="); Serial.println(dt ? (rd2 * 1000u / dt / 1024u) : 0u);
                        print_benchmark("sd", rd2, dt, crc2);
                        sd_stats_t sd_stats;
                        sd_get_stats(&sd_stats);
                        Serial.print("SDSTATS cmd17="); Serial.print(sd_stats.single_read_commands);
                        Serial.print(" cmd18="); Serial.print(sd_stats.multi_read_commands);
                        Serial.print(" fallback="); Serial.print(sd_stats.multi_read_fallbacks);
                        Serial.print(" sectors="); Serial.print(sd_stats.sectors_read);
                        Serial.print(" errors="); Serial.println(sd_stats.read_errors);
                    } else if (strncmp(line, "SPEED", 5) == 0) {
                        uint8_t* const sp_buf = transfer_buffer(1);
                        const uint32_t sp_block = (uint32_t)transfer_buffer_size();
                        for (uint32_t i = 0; i < sp_block; i++) sp_buf[i] = (uint8_t)i;
                        uint32_t sp_total = 2u * 1024u * 1024u;
                        uint32_t sp_crc = 0;
                        Serial.print("SPEEDSTART "); Serial.println(sp_total);
                        uint32_t sp_started = millis();
                        uint32_t sp_sent = 0;
                        while (sp_sent < sp_total) {
                            dbg_iwdg_kick();
                            uint32_t n = sp_block;
                            if (sp_sent + n > sp_total) n = sp_total - sp_sent;
                            if (!serial_write_all(sp_buf, n)) break;
                            sp_crc = crc32_update(sp_crc, sp_buf, n);
                            sp_sent += n;
                        }
                        Serial.flush();
                        uint32_t sp_elapsed = millis() - sp_started;
                        Serial.println("SPEEDEND");
                        print_benchmark("usb", sp_sent, sp_elapsed, sp_crc);
                    } else if (strncmp(line, "BAT10", 5) == 0) {
                        uint32_t sum = 0, cnt = 0, mn = 0xFFFFFFFF, mx = 0;
                        uint32_t t0 = millis();
                        while ((millis() - t0) < 10000) {
                            dbg_iwdg_kick();   /* keep IWDG alive during the 10 s blocking read */
                            uint16_t mv = bat_millivolts();
                            sum += mv; cnt++;
                            if (mv < mn) mn = mv;
                            if (mv > mx) mx = mv;
                            delay(50);
                        }
                        Serial.print("BAT10 avg="); Serial.print(sum / cnt);
                        Serial.print("mV min="); Serial.print(mn);
                        Serial.print("mV max="); Serial.print(mx);
                        Serial.print("mV n="); Serial.println(cnt);
                    } else if (strncmp(line, "DFU", 3) == 0) {
                        Serial.println("entering DFU...");
                        Serial.flush();
                        dfu_enter();
                    } else if (strncmp(line, "DSCAN", 5) == 0) {
                        static int16_t scan_buf[4096];
                        pdm_init(&audio_rb);
                        __HAL_RCC_DMA1_CLK_ENABLE();
                        __HAL_RCC_DMA2_CLK_ENABLE();
                        for (int filt = 0; filt < 2; filt++) {
                            uint32_t* reg = (filt == 0) ? (uint32_t*)&DFSDM1_Filter0->FLTRDATAR : (uint32_t*)&DFSDM1_Filter1->FLTRDATAR;
                            for (int di = 0; di < 2; di++) {
                                DMA_Request_TypeDef* cselr = (di == 0) ? DMA1_CSELR : DMA2_CSELR;
                                DMA_Channel_TypeDef* chans[7] = {0};
                                if (di == 0) { chans[0]=DMA1_Channel1; chans[1]=DMA1_Channel2; chans[2]=DMA1_Channel3; chans[3]=DMA1_Channel4; chans[4]=DMA1_Channel5; chans[5]=DMA1_Channel6; chans[6]=DMA1_Channel7; }
                                else { chans[0]=DMA2_Channel1; chans[1]=DMA2_Channel2; chans[2]=DMA2_Channel3; chans[3]=DMA2_Channel4; chans[4]=DMA2_Channel5; chans[5]=DMA2_Channel6; chans[6]=DMA2_Channel7; }
                                for (int ci = 0; ci < 7; ci++) {
                                    for (int r = 0; r < 16; r++) {
                                        DMA_Channel_TypeDef* dma = chans[ci];
                                        dma->CCR = 0;
                                        uint32_t shift = (uint32_t)ci * 4u;
                                        cselr->CSELR &= ~(0xFu << shift);
                                        cselr->CSELR |= ((uint32_t)r << shift);
                                        dma->CPAR = (uint32_t)reg + 2u;
                                        dma->CMAR = (uint32_t)scan_buf;
                                        dma->CNDTR = 4096;
                                        dma->CCR = DMA_CCR_EN_Msk | DMA_CCR_CIRC_Msk | DMA_CCR_MINC_Msk
                                                 | DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0;
                                        DFSDM1_Filter0->FLTCR1 &= ~DFSDM_FLTCR1_DFEN;
                                        DFSDM1_Filter1->FLTCR1 &= ~DFSDM_FLTCR1_DFEN;
                                        DFSDM1_Filter0->FLTICR = DFSDM_FLTICR_CLRROVRF | DFSDM_FLTICR_CLRJOVRF;
                                        DFSDM1_Filter1->FLTICR = DFSDM_FLTICR_CLRROVRF | DFSDM_FLTICR_CLRJOVRF;
                                        DFSDM1_Filter0->FLTCR1 |= DFSDM_FLTCR1_DFEN | DFSDM_FLTCR1_RSWSTART;
                                        DFSDM1_Filter1->FLTCR1 |= DFSDM_FLTCR1_DFEN | DFSDM_FLTCR1_RSWSTART;
                                        uint32_t t0 = millis();
                                        while ((millis() - t0) < 12) { }
                                        uint32_t ndtr = dma->CNDTR;
                                        dma->CCR = 0;
                                        DFSDM1_Filter0->FLTCR1 &= ~DFSDM_FLTCR1_DFEN;
                                        DFSDM1_Filter1->FLTCR1 &= ~DFSDM_FLTCR1_DFEN;
                                        if (ndtr < 4095u) {
                                            Serial.print("HIT f"); Serial.print(filt);
                                            Serial.print(" d"); Serial.print(di + 1);
                                            Serial.print("c"); Serial.print(ci + 1);
                                            Serial.print(" r"); Serial.print(r);
                                            Serial.print(" nd="); Serial.println(ndtr);
                                        }
                                    }
                                }
                            }
                        }
                        Serial.println("DSCAN done");
                    } else if (strncmp(line, "NF", 2) == 0) {
                        pdm_init(&audio_rb);
                        DFSDM1_Channel1->CHCFGR1 = (DFSDM1_Channel1->CHCFGR1 & ~DFSDM_CHCFGR1_DATMPX) | (1u << DFSDM_CHCFGR1_DATMPX_Pos);
                        DFSDM1_Channel1->CHDATINR = 0xAAAAu;
                        pdm_start();
                        int16_t buf[2048];
                        int64_t ssq = 0;
                        uint32_t cnt = 0;
                        uint32_t e = millis() + 1000;
                        while (millis() < e) {
                            int n = pdm_dma_read(buf, 2048);
                            for (int i = 0; i < n; i++) ssq += (int64_t)buf[i] * buf[i];
                            cnt += (uint32_t)n;
                        }
                        int32_t rms = cnt ? (int32_t)sqrt((double)ssq / cnt) : 0;
                        Serial.print("NF cnt="); Serial.print(cnt);
                        Serial.print(" rms="); Serial.println(rms);
                        pdm_stop();
                    } else if (strncmp(line, "DUAL", 4) == 0) {
                        pdm_init(&audio_rb);
                        pdm_start();
                        uint32_t e = millis() + 1500;
                        int16_t tmp[256];
                        while (millis() < e) { pdm_dma_read(tmp, 256); }
                        int32_t u1r, u2r, cr, nn;
                        pdm_dual_diag(&u1r, &u2r, &cr, &nn);
                        Serial.print("DUAL u1="); Serial.print(u1r);
                        Serial.print(" u2="); Serial.print(u2r);
                        Serial.print(" corr="); Serial.print(cr);
                        Serial.print(" n="); Serial.println(nn);
                        pdm_stop();
                    } else if (strncmp(line, "RAW", 3) == 0) {
                        pdm_init(&audio_rb);
                        pdm_start();
                        uint32_t e = millis() + 1500;
                        int16_t tmp[256];
                        while (millis() < e) { pdm_dma_read(tmp, 256); }
                        int32_t rr, zz, pp, nn;
                        pdm_raw_diag(&rr, &zz, &pp, &nn);
                        Serial.print("RAW rms="); Serial.print(rr);
                        Serial.print(" zcr="); Serial.print(zz);
                        Serial.print(" peak="); Serial.print(pp);
                        Serial.print(" n="); Serial.println(nn);
                        pdm_stop();
                    } else if (strncmp(line, "DBG", 3) == 0) {
                        Serial.print("DBG step="); Serial.print(g_dbg_step, HEX);
                        Serial.print(" fstep="); Serial.print(g_fault_step, HEX);
                        Serial.print(" pc="); Serial.print(g_fault_pc, HEX);
                        Serial.print(" lr="); Serial.print(g_fault_lr, HEX);
                        Serial.print(" cfsr="); Serial.print(g_fault_cfsr, HEX);
                        Serial.print(" bfar="); Serial.print(g_fault_bfar, HEX);
                        Serial.print(" mfar="); Serial.print(g_fault_mfar, HEX);
                        Serial.print(" hfsr="); Serial.print(g_fault_hfsr, HEX);
                        Serial.print(" csr="); Serial.print(RCC->CSR, HEX);
                        Serial.print(" up="); Serial.println(millis());
                        g_dbg_step = 0;
                    } else if (strncmp(line, "SETTIME ", 8) == 0) {
                        char* sp = strchr(line + 8, ' ');
                        uint32_t unix;
                        int32_t tz = INT32_MIN;
                        if (sp != NULL && sp[1] != '\0') { *sp = 0; tz = (int32_t)strtol(sp + 1, NULL, 10); }
                        unix = (uint32_t)strtoul(line + 8, NULL, 10);
                        dt_set_unix(unix);
                        if (tz != INT32_MIN) dt_set_tz(tz);
                        char tb[32];
                        dt_format_local(tb, sizeof(tb));
                        Serial.print("TIME set to "); Serial.println(tb);
                    } else if (strncmp(line, "SETTZ ", 6) == 0) {
                        int32_t tz = (int32_t)strtol(line + 6, NULL, 10);
                        dt_set_tz(tz);
                        Serial.print("TZ set to "); Serial.println(tz);
                    } else if (strncmp(line, "TIME", 4) == 0) {
                        Serial.print("TR="); Serial.print(RTC->TR, HEX);
                        Serial.print(" DR="); Serial.print(RTC->DR, HEX);
                        Serial.print(" SSR="); Serial.print(RTC->SSR, HEX);
                        Serial.print(" CR="); Serial.print(RTC->CR, HEX);
                        Serial.print(" ISR="); Serial.print(RTC->ISR, HEX);
                        Serial.print(" LSE="); Serial.print((RCC->BDCR & RCC_BDCR_LSERDY) ? 1 : 0);
                        Serial.print(" LSION="); Serial.print((RCC->CSR & RCC_CSR_LSION) ? 1 : 0);
                        Serial.print(" BKP1="); Serial.print(RTC->BKP1R, HEX);
                        Serial.println();
                    } else if (strncmp(line, "INFO", 4) == 0) {
                        Serial.print("INFO usb_detect="); Serial.print(digitalRead(PIN_USB_DETECT));
                        Serial.print(" boot="); Serial.print(digitalRead(PIN_BOOT0));
                        Serial.print(" up="); Serial.print(millis());
                        Serial.print(" sysclk="); Serial.print(HAL_RCC_GetSysClockFreq());
                        Serial.print(" ckin_div="); Serial.print((DFSDM1_Channel0->CHCFGR1 & DFSDM_CHCFGR1_CKOUTDIV) >> DFSDM_CHCFGR1_CKOUTDIV_Pos);
                        Serial.print(" fltcr1="); Serial.print(DFSDM1_Filter1->FLTCR1, HEX);
                        Serial.print(" sd=");
                        if (sd_capacity_bytes() > 0) { Serial.print(sd_capacity_bytes()); Serial.print("B"); }
                        else { Serial.print("none"); }
                        Serial.print(" free=");
                        Serial.print((uint32_t)(fs_free_bytes() >> 20)); Serial.print("MB");
                        char tb[32];
                        dt_format_local(tb, sizeof(tb));
                        Serial.print(" time="); Serial.print(tb);
                        Serial.print(" bat="); Serial.print(bat_millivolts()); Serial.print("mV");
                        Serial.print(" pct="); Serial.print(bat_percent());
                        Serial.print(" tz="); Serial.print(dt_get_tz());
                        Serial.print(" time_set="); Serial.print(dt_time_is_set() ? 1 : 0);
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

    /* USB detect -> auto recording (debounced 100 ms) */
    int usb = digitalRead(PIN_USB_DETECT);
    if (last_usb < 0) {
        last_usb = usb;
        if (usb == LOW) rec_start();   /* booted with USB detached -> start recording */
    }
    if (usb != last_usb) {
        last_usb = usb;
        usb_pending = usb;
        usb_pending_since = millis();
    }
    if (usb_pending >= 0 && (millis() - usb_pending_since) >= 100) {
        if (usb_pending == 0) rec_start(); else rec_stop();
        usb_pending = -1;
    }
    if (rec_active) {
        rec_poll_samples();
        if ((millis() - rec_last_sync_ms) >= REC_SYNC_INTERVAL_MS) rec_checkpoint();
        if ((millis() - rec_circ_last_ms) >= REC_CIRC_INTERVAL_MS) {
            rec_circ_last_ms = millis();
            fs_make_space(CIRC_FREE_BYTES, rec_name + 3);
        }
    }
}
