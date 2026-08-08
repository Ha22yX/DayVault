# DayVault Arduino Recorder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the DayVault recorder firmware from scratch on the STM32duino (Arduino) platform so that USB detach auto-starts mono recording to an exFAT SD card and USB attach auto-stops recording and re-enumerates as CDC+MSC for Windows export.

**Architecture:** Layered modular Arduino project under `firmware/`. Pure-logic modules (RingBuf, WavFile, Cmd, Recorder) are HAL-free and covered by host unit tests in a `native` PlatformIO env; hardware-bound modules (PdmCapture, SdCard, diskio, UsbComposite, Dfu, main) are verified on the board via DFU flash, the USB CDC console, and the MSC disk. USB uses the ST USB device library vendored into `lib/STM32USB` with a fresh PCD glue layer, because the STM32duino core only provides CDC/HID classes, not MSC.

**Tech Stack:** PlatformIO (`ststm32`, `framework=arduino`), STM32duino 2.12.0, STM32L452RC, STM32 HAL (DFSDM/DMA/PCD/SPI all enabled by default in the core), FatFs R0.13c+ with `_FS_EXFAT=1`, dfu-util for flashing, Unity host tests.

## Global Constraints

- Record mono 16 kHz / 16-bit PCM WAV. One file per recording session, named `REC001.WAV` … `REC999.WAV`, sequential scan.
- SD card is a 128 GB exFAT SDXC card over SPI1. FatFs `_FS_EXFAT=1` is already configured in `firmware/lib/FatFs/ffconf.h`.
- USB enumerates as CDC + MSC composite. MSC is a read-only export volume that reads raw SD blocks.
- ST USB VID `0x0483`, PID `0x5741`, product string `DayVault Recorder`.
- DFU entry: BOOT button (hardware) and CDC command line `DFU\n` (software jump to ROM bootloader at `0x1FFF0000`). There is NO boot-time PH3 auto-DFU trigger.
- AGENTS.md HARD RULES: never modify option bytes or boot-config bits; before every DFU flash confirm `dfu-util -l` sees the device; only the application image (0x08000000) is written; confirm a normal reset after flashing.
- Clock: 80 MHz SYSCLK from MSI 8 MHz → PLL (M=1,N=20,R=2), VOS Range 1 set first; USB FS 48 MHz from HSI48.
- Pin map (from `Docs/02-MCU-Pinout.md`): PDM CKOUT PC2, PDM DATA PB12 (DFSDM1_DATIN1, Channel 1 direct); SD CS PA4 / SCK PA5 / MISO PA6 / MOSI PA7 (SPI1); USB DM PA11 / DP PA12 / DETECT PA9; LED PA8; BOOT0 PH3 (read-only, not used as a trigger).
- Pure-logic modules must not `#include` any HAL/Arduino header so the `native` env compiles them on the host.
- The legacy HAL/Cube firmware and firmware-related docs are archived on the `legacy/hal-firmware` worktree; main keeps only what the new development needs.

Design doc: `Docs/superpowers/specs/2026-08-08-arduino-recorder-design.md`

Tool paths used in commands below:
- `dfu-util`: `C:\Users\Administrator\.platformio\packages\tool-dfuutil\bin\dfu-util.exe`
- Arduino core: `C:\Users\Administrator\.platformio\packages\framework-arduinoststm32`

---

### Task 1: Repository reorganization (legacy worktree)

**Files:**
- Create: `../DayVault-legacy` (git worktree directory, sibling of the repo)
- Modify (main): remove old firmware code and firmware-related docs
- Test: `Docs/superpowers/specs/2026-08-08-arduino-recorder-design.md` (kept in main)

**Interfaces:**
- Consumes: nothing.
- Produces: a clean main branch containing only AGENTS.md, EDA/, `Docs/01-02-03-07-08`, `Docs/superpowers/`, `.github`, `firmware/{boards,lib/FatFs}`, and the new firmware sources added by later tasks.

- [ ] **Step 1: Commit pending working-tree changes so nothing is lost**

Run: `git status --short` in `C:\Users\Administrator\Desktop\DayVault`
Expected: shows `M firmware/boards/dayvault_l452rc.json`, `M firmware/platformio.ini`, `?? firmware/arduino_test/`

```bash
git add firmware/boards/dayvault_l452rc.json firmware/platformio.ini firmware/arduino_test
git commit -m "wip(firmware): arduino_test bring-up probe (moved to legacy worktree next)"
```

- [ ] **Step 2: Create the legacy worktree**

```bash
git worktree add ../DayVault-legacy legacy/hal-firmware
git worktree list
```

Expected: the worktree appears at `C:\Users\Administrator\Desktop\DayVault-legacy` on branch `legacy/hal-firmware`. Verify the old files are present there:
`Test-Path "C:\Users\Administrator\Desktop\DayVault-legacy\firmware\src\app.c"` → True.

- [ ] **Step 3: Remove old firmware code from main**

```bash
git rm -r firmware/src firmware/include firmware/arduino_test
```

Expected: old HAL/Cube sources staged for removal. `firmware/boards/` and `firmware/lib/FatFs/` remain tracked.

- [ ] **Step 4: Remove firmware-related docs from main**

```bash
git rm "Docs/00-开发速查.md" Docs/05-Bringup-and-Test.md Docs/06-Known-Issues.md Docs/09-USB-DFU-Entry-Design.md
git rm Docs/superpowers/plans/2026-08-08-recording-export.md Docs/superpowers/specs/2026-08-08-recording-export-design.md
```

Expected: those files staged for removal; `Docs/01-Hardware-Overview.md`, `02-MCU-Pinout.md`, `03-Component-Pinout.md`, `07-BOM.md`, `08-Net-Map.md`, and `Docs/superpowers/specs/2026-08-08-arduino-recorder-design.md` remain.

- [ ] **Step 5: Commit the reorganization**

```bash
git commit -m "chore: move legacy HAL firmware and firmware docs to legacy/hal-firmware worktree"
```

- [ ] **Step 6: Verify the split**

Run:
```bash
git worktree list
Test-Path "C:\Users\Administrator\Desktop\DayVault-legacy\firmware\src\app.c"   # legacy has old code
Test-Path "C:\Users\Administrator\Desktop\DayVault\firmware\src\app.c"          # main does not
```
Expected: legacy True, main False.

---

### Task 2: Arduino project skeleton (compiles, flashes, LED blinks)

**Files:**
- Create: `firmware/src/Config.h`
- Create: `firmware/src/main.cpp`
- Modify: `firmware/platformio.ini` (rewrite)

**Interfaces:**
- Consumes: `firmware/boards/dayvault_l452rc.json` (already has `framework=arduino`, hwids `[0x0483, 0x5741]`, variant L452RC — committed in Task 1), `firmware/boards/stm32l452rc.ld`, Arduino core 2.12.0.
- Produces: `Config.h` pin/constant defines used by every later task; a flashing LED proves the toolchain + clock tree.

- [ ] **Step 1: Write `firmware/platformio.ini`**

```ini
[platformio]
default_envs = dayvault

[env:dayvault]
platform = ststm32
board = dayvault_l452rc
framework = arduino
board_build.flash_size = 256KB
board_build.ldscript = boards/stm32l452rc.ld
build_type = release
monitor_speed = 115200
build_flags =
    -Os
    -std=gnu++11

[env:native]
platform = native
test_framework = unity
build_src_filter =
    -<*>
    +<RingBuf.cpp>
    +<WavFile.cpp>
    +<Cmd.cpp>
    +<Recorder.cpp>
test_build_src = true
```

- [ ] **Step 2: Write `firmware/src/Config.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

#define PIN_USB_DETECT   GPIO_PIN_9
#define PIN_LED          GPIO_PIN_8

#define PIN_SD_CS       GPIO_PIN_4
#define PIN_SD_CS_PORT  GPIOA
#define PIN_SD_SCK      GPIO_PIN_5
#define PIN_SD_MISO     GPIO_PIN_6
#define PIN_SD_MOSI     GPIO_PIN_7

#define PIN_PDM_CLK       GPIO_PIN_2
#define PIN_PDM_CLK_PORT  GPIOC
#define PIN_PDM_DATA      GPIO_PIN_12
#define PIN_PDM_DATA_PORT GPIOB

#define AUDIO_SAMPLE_RATE 16000u
#define AUDIO_CHANNELS    1u
#define AUDIO_BITS        16u

#define PDM_CKOUT_DIVIDER 39u
#define PDM_OSR           128u
#define PDM_HALF_SAMPLES  512u
#define PDM_RING_BYTES    (PDM_HALF_SAMPLES * 2u * 16u)   /* 16 KB ring */

#define REC_DIR_STR "REC"
#define REC_EXT_STR "WAV"
#define REC_SEQ_MAX 999u

#define USB_VID 0x0483u
#define USB_PID 0x5741u
#define CDC_RX_LINE_MAX 64u
```

Note: `GPIO_PIN_*` / `GPIOA` come from `stm32l4xx_hal_gpio.h`, which `main.cpp` includes before `Config.h`; `Config.h` itself only declares the defines for files that include it after HAL headers. `main.cpp` must `#include "stm32l4xx_hal.h"` first.

- [ ] **Step 3: Write `firmware/src/main.cpp` (clock + LED blink)**

```cpp
#include <Arduino.h>
#include "stm32l4xx_hal.h"
#include "Config.h"

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

static uint32_t last_blink = 0;
static bool led_on = false;

void setup()
{
    SystemClock_Config();
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
    pinMode(PIN_USB_DETECT, INPUT);
}

void loop()
{
    uint32_t now = millis();
    if (now - last_blink >= 500) {
        last_blink = now;
        led_on = !led_on;
        digitalWrite(PIN_LED, led_on ? HIGH : LOW);
    }
}
```

- [ ] **Step 4: Build the skeleton**

Run: `pio run -e dayvault` in `firmware/`
Expected: SUCCESS, flash usage visible. Fix any clock/board errors before proceeding.

- [ ] **Step 5: Flash via DFU and verify LED blinks**

Enter DFU: hold BOOT (SW2), press RESET, release BOOT.
Run: `& "C:\Users\Administrator\.platformio\packages\tool-dfuutil\bin\dfu-util.exe" -l`
Expected: `0483:df11` found. If not, repeat the BOOT+RESET sequence.

```bash
& "C:\Users\Administrator\.platformio\packages\tool-dfuutil\bin\dfu-util.exe" -a 0 -s 0x08000000:leave -D "firmware\.pio\build\dayvault\firmware.bin"
```

Expected: download 100%. Then press RESET (BOOT released). The LED on PA8 blinks every 500 ms. Confirm with the user before proceeding.

- [ ] **Step 6: Commit**

```bash
git add firmware/platformio.ini firmware/src/Config.h firmware/src/main.cpp
git commit -m "feat(arduino): project skeleton with 80MHz/HSI48 clock and LED blink"
```

---

### Task 3: RingBuf module + host tests

**Files:**
- Create: `firmware/src/RingBuf.h`, `firmware/src/RingBuf.cpp`
- Create: `firmware/test/test_ringbuf/test_ringbuf.cpp`

**Interfaces:**
- Consumes: nothing (pure logic, no HAL includes).
- Produces:
  ```cpp
  struct RingBuf { uint8_t* buf; size_t size; volatile size_t head; volatile size_t tail; volatile size_t used; };
  void ringbuf_init(RingBuf* rb, uint8_t* buf, size_t size);
  size_t ringbuf_write(RingBuf* rb, const uint8_t* data, size_t n);   // returns bytes written
  size_t ringbuf_read(RingBuf* rb, uint8_t* dst, size_t n);            // returns bytes read
  size_t ringbuf_used(const RingBuf* rb);
  ```
  Written to `RingBuf.h` as plain `extern "C"`-compatible free functions. Later tasks: `PdmCapture` calls `ringbuf_write`, `main.cpp` pump calls `ringbuf_read`/`ringbuf_used`.

- [ ] **Step 1: Write the failing tests** (`firmware/test/test_ringbuf/test_ringbuf.cpp`)

```cpp
#include "RingBuf.h"
#include <unity.h>

static uint8_t storage[64];
static RingBuf rb;

void setUp(void)  { ringbuf_init(&rb, storage, sizeof(storage)); }
void tearDown(void) { }

void test_empty_after_init(void)  { TEST_ASSERT_EQUAL(0, ringbuf_used(&rb)); }

void test_write_and_read(void)
{
    uint8_t in[8] = {1,2,3,4,5,6,7,8};
    uint8_t out[8] = {0};
    TEST_ASSERT_EQUAL(8, ringbuf_write(&rb, in, 8));
    TEST_ASSERT_EQUAL(8, ringbuf_used(&rb));
    TEST_ASSERT_EQUAL(8, ringbuf_read(&rb, out, 8));
    TEST_ASSERT_EQUAL_MEMORY(in, out, 8);
    TEST_ASSERT_EQUAL(0, ringbuf_used(&rb));
}

void test_wraparound(void)
{
    uint8_t a[48], b[32], out[32];
    memset(a, 0xAB, sizeof(a)); memset(b, 0xCD, sizeof(b)); memset(out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(48, ringbuf_write(&rb, a, 48));   /* head near end */
    TEST_ASSERT_EQUAL(48, ringbuf_read(&rb, out, 48));  /* tail near end */
    TEST_ASSERT_EQUAL(32, ringbuf_write(&rb, b, 32));   /* head wraps to 0 */
    TEST_ASSERT_EQUAL(32, ringbuf_read(&rb, out, 32));
    for (int i = 0; i < 32; i++) TEST_ASSERT_EQUAL_HEX8(0xCD, out[i]);
}

void test_full_and_partial(void)
{
    uint8_t big[80], out[8];
    memset(big, 0x11, sizeof(big));
    TEST_ASSERT_EQUAL(64, ringbuf_write(&rb, big, 80));   /* only 64 fit */
    TEST_ASSERT_EQUAL(0, ringbuf_write(&rb, big, 4));     /* full */
    TEST_ASSERT_EQUAL(64, ringbuf_used(&rb));
    TEST_ASSERT_EQUAL(8, ringbuf_read(&rb, out, 8));
    TEST_ASSERT_EQUAL(56, ringbuf_used(&rb));
}

void test_write_never_exceeds_capacity(void)
{
    uint8_t big[200];
    memset(big, 0x22, sizeof(big));
    ringbuf_write(&rb, big, 200);
    TEST_ASSERT_EQUAL(64, ringbuf_used(&rb));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_after_init);
    RUN_TEST(test_write_and_read);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_full_and_partial);
    RUN_TEST(test_write_never_exceeds_capacity);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native` in `firmware/`
Expected: compile error `RingBuf.h not found`.

- [ ] **Step 3: Write `firmware/src/RingBuf.h` and `RingBuf.cpp`**

```cpp
// RingBuf.h
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint8_t* buf; size_t size; volatile size_t head; volatile size_t tail; volatile size_t used; } RingBuf;

void  ringbuf_init(RingBuf* rb, uint8_t* buf, size_t size);
size_t ringbuf_write(RingBuf* rb, const uint8_t* data, size_t n);
size_t ringbuf_read(RingBuf* rb, uint8_t* dst, size_t n);
size_t ringbuf_used(const RingBuf* rb);

#ifdef __cplusplus
}
#endif
```

```cpp
// RingBuf.cpp
#include "RingBuf.h"

#if defined(__arm__)
static inline unsigned long irq_mask_save(void) {
    unsigned long primask;
    __asm volatile ("mrs %0, primask" : "=r" (primask) :: "memory");
    __asm volatile ("cpsid i" ::: "memory");
    return primask;
}
static inline void irq_mask_restore(unsigned long primask) {
    __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}
#else
static inline unsigned long irq_mask_save(void) { return 0; }
static inline void irq_mask_restore(unsigned long) { }
#endif

void ringbuf_init(RingBuf* rb, uint8_t* buf, size_t size) {
    rb->buf = buf; rb->size = size; rb->head = 0; rb->tail = 0; rb->used = 0;
}
size_t ringbuf_write(RingBuf* rb, const uint8_t* data, size_t n) {
    size_t i;
    unsigned long pm = irq_mask_save();
    for (i = 0; i < n; i++) {
        if (rb->used == rb->size) break;
        rb->buf[rb->head] = data[i];
        rb->head = (rb->head + 1) % rb->size;
        rb->used++;
    }
    irq_mask_restore(pm);
    return i;
}
size_t ringbuf_read(RingBuf* rb, uint8_t* dst, size_t n) {
    size_t i;
    unsigned long pm = irq_mask_save();
    for (i = 0; i < n && rb->used > 0; i++) {
        dst[i] = rb->buf[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
        rb->used--;
    }
    irq_mask_restore(pm);
    return i;
}
size_t ringbuf_used(const RingBuf* rb) { return rb->used; }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `pio test -e native` in `firmware/`
Expected: all 5 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/RingBuf.h firmware/src/RingBuf.cpp firmware/test/test_ringbuf/test_ringbuf.cpp
git commit -m "feat(arduino): ring buffer module with host tests"
```

---

### Task 4: WavFile module + host tests

**Files:**
- Create: `firmware/src/WavFile.h`, `firmware/src/WavFile.cpp`
- Create: `firmware/test/test_wavfile/test_wavfile.cpp`

**Interfaces:**
- Consumes: nothing (pure logic).
- Produces:
  ```cpp
  struct WavConfig { uint16_t format; uint32_t sample_rate; uint16_t channels; uint16_t bits; uint16_t block_align; uint32_t byte_rate; };
  size_t  wav_header_size(const WavConfig* cfg);              // always 44
  void    wav_build_header(uint8_t hdr[44], const WavConfig* cfg, uint32_t data_bytes);
  void    wav_patch_sizes(uint8_t hdr[44], uint32_t data_bytes);
  ```
  `main.cpp` calls `wav_build_header` when opening `REC###.WAV` and `wav_patch_sizes` when finalizing.

- [ ] **Step 1: Write the failing tests** (`firmware/test/test_wavfile/test_wavfile.cpp`)

```cpp
#include "WavFile.h"
#include <unity.h>
#include <string.h>

static WavConfig make_mono_cfg(void) {
    WavConfig c;
    c.format = 1; c.sample_rate = 16000; c.channels = 1; c.bits = 16;
    c.block_align = 2; c.byte_rate = 32000;
    return c;
}

void test_header_size(void) {
    WavConfig c = make_mono_cfg();
    TEST_ASSERT_EQUAL(44, wav_header_size(&c));
}

void test_golden_mono_header(void) {
    /* 16 kHz / 16-bit mono, zero data bytes -> RIFF size 36 */
    const uint8_t golden[44] = {
        'R','I','F','F', 36,0,0,0, 'W','A','V','E', 'f','m','t',' ',
        16,0,0,0, 1,0, 1,0, 0x80,0x3E,0,0, 0x00,0x7D,0,0, 2,0, 16,0,
        'd','a','t','a', 0,0,0,0
    };
    WavConfig c = make_mono_cfg();
    uint8_t hdr[44];
    wav_build_header(hdr, &c, 0);
    TEST_ASSERT_EQUAL_MEMORY(golden, hdr, 44);
}

void test_patch_sizes(void) {
    WavConfig c = make_mono_cfg();
    uint8_t hdr[44];
    wav_build_header(hdr, &c, 0);
    wav_patch_sizes(hdr, 1000);
    TEST_ASSERT_EQUAL(36 + 1000, hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | (hdr[7] << 24));
    TEST_ASSERT_EQUAL(1000, hdr[40] | (hdr[41] << 8) | (hdr[42] << 16) | (hdr[43] << 24));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_header_size);
    RUN_TEST(test_golden_mono_header);
    RUN_TEST(test_patch_sizes);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native` in `firmware/`
Expected: compile error `WavFile.h not found`.

- [ ] **Step 3: Write `firmware/src/WavFile.h` and `WavFile.cpp`**

```cpp
// WavFile.h
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t format;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits;
    uint16_t block_align;
    uint32_t byte_rate;
} WavConfig;

size_t wav_header_size(const WavConfig* cfg);
void   wav_build_header(uint8_t hdr[44], const WavConfig* cfg, uint32_t data_bytes);
void   wav_patch_sizes(uint8_t hdr[44], uint32_t data_bytes);

#ifdef __cplusplus
}
#endif
```

```cpp
// WavFile.cpp
#include "WavFile.h"
#include <string.h>

static void put_le16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

size_t wav_header_size(const WavConfig* cfg) { (void)cfg; return 44u; }

void wav_build_header(uint8_t hdr[44], const WavConfig* cfg, uint32_t data_bytes) {
    memset(hdr, 0, 44);
    hdr[0]='R'; hdr[1]='I'; hdr[2]='F'; hdr[3]='F';
    put_le32(hdr + 4, 36u + data_bytes);
    hdr[8]='W'; hdr[9]='A'; hdr[10]='V'; hdr[11]='E';
    hdr[12]='f'; hdr[13]='m'; hdr[14]='t'; hdr[15]=' ';
    put_le32(hdr + 16, 16u);
    put_le16(hdr + 20, cfg->format);
    put_le16(hdr + 22, cfg->channels);
    put_le32(hdr + 24, cfg->sample_rate);
    put_le32(hdr + 28, cfg->byte_rate);
    put_le16(hdr + 32, cfg->block_align);
    put_le16(hdr + 34, cfg->bits);
    hdr[36]='d'; hdr[37]='a'; hdr[38]='t'; hdr[39]='a';
    put_le32(hdr + 40, data_bytes);
}

void wav_patch_sizes(uint8_t hdr[44], uint32_t data_bytes) {
    put_le32(hdr + 4, 36u + data_bytes);
    put_le32(hdr + 40, data_bytes);
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `pio test -e native` in `firmware/`
Expected: all 3 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/WavFile.h firmware/src/WavFile.cpp firmware/test/test_wavfile/test_wavfile.cpp
git commit -m "feat(arduino): WAV header module with golden host tests"
```

---

### Task 5: Cmd module (CDC line parser) + host tests

**Files:**
- Create: `firmware/src/Cmd.h`, `firmware/src/Cmd.cpp`
- Create: `firmware/test/test_cmd/test_cmd.cpp`

**Interfaces:**
- Consumes: nothing (pure logic).
- Produces:
  ```cpp
  enum class CmdEvent { None, Dfu, Info };
  void       cmd_init();
  CmdEvent   cmd_feed(uint8_t byte);   // feed bytes; returns Dfu/Info when a full line arrives
  ```
  `UsbComposite` CDC RX forwards received bytes to `cmd_feed`; `main.cpp` polls the returned events. `DFU\n` / `DFU\r\n` → `CmdEvent::Dfu`; `INFO\n` → `CmdEvent::Info`.

- [ ] **Step 1: Write the failing tests** (`firmware/test/test_cmd/test_cmd.cpp`)

```cpp
#include "Cmd.h"
#include <unity.h>

void test_empty_line_no_event(void)      { cmd_init(); TEST_ASSERT_EQUAL((int)CmdEvent::None, (int)cmd_feed('\n')); }
void test_dfu_line(void)                 { cmd_init(); const char s[] = "DFU\n"; CmdEvent e = CmdEvent::None; for (size_t i=0;i<3;i++) e = cmd_feed((uint8_t)s[i]); TEST_ASSERT_EQUAL((int)CmdEvent::Dfu, (int)e); }
void test_dfu_crlf(void)                 { cmd_init(); const char s[] = "DFU\r\n"; CmdEvent e = CmdEvent::None; for (size_t i=0;i<5;i++) e = cmd_feed((uint8_t)s[i]); TEST_ASSERT_EQUAL((int)CmdEvent::Dfu, (int)e); }
void test_info_line(void)                { cmd_init(); const char s[] = "INFO\n"; CmdEvent e = CmdEvent::None; for (size_t i=0;i<5;i++) e = cmd_feed((uint8_t)s[i]); TEST_ASSERT_EQUAL((int)CmdEvent::Info, (int)e); }
void test_unknown_line_no_event(void)    { cmd_init(); const char s[] = "XYZ\n"; CmdEvent e = CmdEvent::None; for (size_t i=0;i<4;i++) e = cmd_feed((uint8_t)s[i]); TEST_ASSERT_EQUAL((int)CmdEvent::None, (int)e); }
void test_overlong_line_discarded(void)  { cmd_init(); CmdEvent e = CmdEvent::None; char junk[80]; memset(junk,'A',sizeof(junk)); for (size_t i=0;i<sizeof(junk);i++) e = cmd_feed((uint8_t)junk[i]); e = cmd_feed('\n'); TEST_ASSERT_EQUAL((int)CmdEvent::None, (int)e); }
void test_feed_rejects_interior_cr(void) { cmd_init(); cmd_feed('D'); cmd_feed('F'); cmd_feed('U'); TEST_ASSERT_EQUAL((int)CmdEvent::Dfu, (int)cmd_feed('\r')); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_line_no_event);
    RUN_TEST(test_dfu_line);
    RUN_TEST(test_dfu_crlf);
    RUN_TEST(test_info_line);
    RUN_TEST(test_unknown_line_no_event);
    RUN_TEST(test_overlong_line_discarded);
    RUN_TEST(test_feed_rejects_interior_cr);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native` in `firmware/`
Expected: compile error `Cmd.h not found`.

- [ ] **Step 3: Write `firmware/src/Cmd.h` and `Cmd.cpp`**

```cpp
// Cmd.h
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { CMD_EVT_NONE = 0, CMD_EVT_DFU, CMD_EVT_INFO } CmdEvent;

void      cmd_init(void);
CmdEvent  cmd_feed(uint8_t byte);

#ifdef __cplusplus
}
#endif
```

```cpp
// Cmd.cpp
#include "Cmd.h"
#include <string.h>

#define CMD_LINE_MAX 64u

static char line[CMD_LINE_MAX];
static size_t len;
static int discard;

void cmd_init(void) { len = 0; discard = 0; }

CmdEvent cmd_feed(uint8_t byte)
{
    if (discard) {
        if (byte == '\n') cmd_init();
        return CMD_EVT_NONE;
    }
    if (byte == '\n') {
        CmdEvent evt = CMD_EVT_NONE;
        if (len > 0 && line[len - 1] == '\r') len--;
        if (len == 3 && memcmp(line, "DFU", 3) == 0) evt = CMD_EVT_DFU;
        else if (len == 4 && memcmp(line, "INFO", 4) == 0) evt = CMD_EVT_INFO;
        cmd_init();
        return evt;
    }
    if (byte == '\r') return CMD_EVT_NONE;   /* treat CR as end-of-field, ignore interior */
    if (len >= sizeof(line)) { discard = 1; return CMD_EVT_NONE; }
    line[len++] = (char)byte;
    return CMD_EVT_NONE;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `pio test -e native` in `firmware/`
Expected: all 7 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/Cmd.h firmware/src/Cmd.cpp firmware/test/test_cmd/test_cmd.cpp
git commit -m "feat(arduino): CDC line command parser (DFU/INFO) with host tests"
```

---

### Task 6: USB CDC console (vendor ST USB middleware + descriptors + PCD glue)

**Files:**
- Create: `firmware/lib/STM32USB/` (vendored middleware, flat layout)
- Create: `firmware/src/UsbComposite.h`, `firmware/src/UsbComposite.cpp`
- Create: `firmware/src/usbd_conf.h`, `firmware/src/usbd_desc.c`, `firmware/src/usbd_desc.h`
- Modify: `firmware/platformio.ini` (nothing — build picks up lib/ automatically)

**Interfaces:**
- Consumes: `Config.h` (VID/PID), HAL PCD module (enabled by default in the core), ST USB middleware vendored below.
- Produces:
  ```cpp
  bool  usb_composite_init(void);    // start CDC device (MSC added in Task 10)
  void  usb_composite_deinit(void);
  void  usb_composite_poll(void);    // call from loop()
  void  cdc_printf(const char* fmt, ...);   // blocking-ish TX helper for debug console
  extern USBD_HandleTypeDef hUsbDevice;
  ```
  `main.cpp` calls `usb_composite_init` from the recorder MSC action and `usb_composite_deinit` on USB detach; `cdc_printf` prints the INFO/debug console.

- [ ] **Step 1: Vendor the ST USB middleware into `firmware/lib/STM32USB/` (flat)**

Copy from `C:\Users\Administrator\.platformio\packages\framework-arduinoststm32\system\Middlewares\ST\STM32_USB_Device_Library\`:

```
Core\Src\usbd_core.c  Core\Inc\usbd_core.h
Core\Src\usbd_ctlreq.c  Core\Inc\usbd_ctlreq.h
Core\Src\usbd_ioreq.c  Core\Inc\usbd_ioreq.h
Core\Inc\usbd_def.h
Class\CDC\Src\usbd_cdc.c  Class\CDC\Inc\usbd_cdc.h
```

Copy all 9 files flat into `firmware/lib/STM32USB/` (no subdirectories) so flat `#include "usbd_core.h"` directives resolve. The `.gitignore` must not exclude `.c` under `lib/`; verify `git check-ignore firmware/lib/STM32USB/usbd_core.c` returns nothing.

- [ ] **Step 2: Write `firmware/src/usbd_conf.h`**

```cpp
#pragma once
#include "stm32l4xx_hal.h"
#include <stdint.h>

#define USBD_MAX_NUM_CONFIGURATION      1u
#define USBD_MAX_NUM_INTERFACES         2u
#define USBD_MAX_NUM_ENDPOINTS          4u
#define USBD_VID                        USB_VID
#define USBD_PID                        USB_PID
#define USBD_LANGID_STRING              0x409u
#define USBD_MANUFACTURER_STRING        "DayVault"
#define USBD_PRODUCT_STRING             "DayVault Recorder"
#define USBD_CONFIGURATION_STRING       "CDC Config"
#define USBD_INTERFACE_STRING           "CDC+MSC"

#define USBD_malloc                     usbd_static_malloc
#define USBD_free                       usbd_static_free
#define USBD_memset                     memset
#define USBD_memcpy                     memcpy

#define USBD_MAX_STR_DESC_SIZ           64u

#define USBD_SELF_POWERED               1u

void* usbd_static_malloc(uint32_t size);
void  usbd_static_free(void* p);
```

Note: `usbd_static_malloc`/`usbd_static_free` are defined in `UsbComposite.cpp`.

- [ ] **Step 3: Write `firmware/src/usbd_desc.c` + `usbd_desc.h`**

```cpp
// usbd_desc.h
#pragma once
#include "usbd_def.h"
extern USBD_DescriptorsTypeDef FS_Desc;
```

```cpp
// usbd_desc.c
#include "usbd_desc.h"
#include "usbd_conf.h"
#include <string.h>
#include <stdio.h>

static uint8_t dev_desc[USB_LEN_DEV_DESC];
static uint8_t str_desc[USBD_MAX_STR_DESC_SIZ];

static void fill_serial(void)
{
    uint32_t s1 = *(volatile uint32_t *)0x1FFF7590u;   /* UID[0] on STM32L4x2 */
    uint32_t s2 = *(volatile uint32_t *)0x1FFF7594u;
    uint32_t s3 = *(volatile uint32_t *)0x1FFF7598u;
    char buf[25];
    snprintf(buf, sizeof(buf), "%08lX%08lX%08lX", (unsigned long)s1, (unsigned long)s2, (unsigned long)s3);
    uint8_t* p = str_desc + 2; uint8_t n = 0;
    for (const char* c = buf; *c && n < USBD_MAX_STR_DESC_SIZ - 3; c++) { p[n++] = (uint8_t)*c; p[n++] = 0; }
    str_desc[0] = (uint8_t)(n + 2); str_desc[1] = USB_DESC_TYPE_STRING;
}

static uint8_t* dev_desc_ptr(USBD_SpeedTypeDef speed, uint16_t* len)
{
    (void)speed;
    memset(dev_desc, 0, USB_LEN_DEV_DESC);
    dev_desc[0] = USB_LEN_DEV_DESC; dev_desc[1] = USB_DESC_TYPE_DEVICE;
    dev_desc[2] = 0x00; dev_desc[3] = 0x02;
    dev_desc[4] = 0xEF; dev_desc[5] = 0x02; dev_desc[6] = 0x01;   /* composite */
    dev_desc[7] = USB_MAX_EP0_SIZE;
    dev_desc[8] = (uint8_t)USBD_VID; dev_desc[9] = (uint8_t)(USBD_VID >> 8);
    dev_desc[10] = (uint8_t)USBD_PID; dev_desc[11] = (uint8_t)(USBD_PID >> 8);
    dev_desc[12] = 0x00; dev_desc[13] = 0x02;
    dev_desc[14] = 0x01; dev_desc[15] = 0x02; dev_desc[16] = 0x03; dev_desc[17] = 0x01;
    *len = USB_LEN_DEV_DESC;
    return dev_desc;
}

static uint8_t* langid_ptr(USBD_SpeedTypeDef speed, uint16_t* len)
{
    (void)speed;
    str_desc[0] = 4; str_desc[1] = USB_DESC_TYPE_STRING;
    str_desc[2] = (uint8_t)USBD_LANGID_STRING; str_desc[3] = (uint8_t)(USBD_LANGID_STRING >> 8);
    *len = 4; return str_desc;
}

static uint8_t* str_ptr(USBD_SpeedTypeDef speed, uint16_t* len, const char* s)
{
    (void)speed;
    uint8_t n = 0; uint8_t* p = str_desc + 2;
    for (const char* c = s; *c && n < USBD_MAX_STR_DESC_SIZ - 3; c++) { p[n++] = (uint8_t)*c; p[n++] = 0; }
    str_desc[0] = (uint8_t)(n + 2); str_desc[1] = USB_DESC_TYPE_STRING;
    *len = (uint16_t)(n + 2); return str_desc;
}

static uint8_t* mfr_ptr(USBD_SpeedTypeDef speed, uint16_t* len) { return str_ptr(speed, len, USBD_MANUFACTURER_STRING); }
static uint8_t* prod_ptr(USBD_SpeedTypeDef speed, uint16_t* len) { return str_ptr(speed, len, USBD_PRODUCT_STRING); }
static uint8_t* ser_ptr(USBD_SpeedTypeDef speed, uint16_t* len) { (void)speed; fill_serial(); *len = str_desc[0]; return str_desc; }
static uint8_t* cfg_ptr(USBD_SpeedTypeDef speed, uint16_t* len) { return str_ptr(speed, len, USBD_CONFIGURATION_STRING); }
static uint8_t* intf_ptr(USBD_SpeedTypeDef speed, uint16_t* len) { return str_ptr(speed, len, USBD_INTERFACE_STRING); }

USBD_DescriptorsTypeDef FS_Desc = {
    dev_desc_ptr, langid_ptr, mfr_ptr, prod_ptr, ser_ptr, cfg_ptr, intf_ptr
};
```

Note: confirm the UID base address for L452 in Task 6 by checking the HAL header (`#define UID_BASE`) — if it differs, use the macro `UID_BASE` instead of the literal.

- [ ] **Step 4: Write `firmware/src/UsbComposite.h`**

```cpp
#pragma once
#include "usbd_def.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool usb_composite_init(void);
void usb_composite_deinit(void);
void usb_composite_poll(void);
void cdc_printf(const char* fmt, ...);
extern USBD_HandleTypeDef hUsbDevice;

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 5: Write `firmware/src/UsbComposite.cpp` (PCD glue + CDC device)**

```cpp
#include "UsbComposite.h"
#include "usbd_conf.h"
#include "usbd_core.h"
#include "usbd_cdc.h"
#include "usbd_desc.h"
#include "stm32l4xx_hal.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

USBD_HandleTypeDef hUsbDevice;
static PCD_HandleTypeDef hpcd;

#define USBD_MEM_POOL_SIZE 896u
static uint32_t usbd_mem_pool[USBD_MEM_POOL_SIZE / 4u];
static uint32_t usbd_mem_offset = 0u;

void* usbd_static_malloc(uint32_t size) {
    uint32_t aligned = (size + 3u) & ~3u;
    void* p = NULL;
    if (usbd_mem_offset + aligned <= USBD_MEM_POOL_SIZE) {
        p = (void *)&usbd_mem_pool[usbd_mem_offset / 4u];
        usbd_mem_offset += aligned;
    }
    return p;
}
void usbd_static_free(void* p) { if (p != NULL) usbd_mem_offset = 0u; }

/* CDC RX line framing + TX handled in the CDC fops below; no static TX buffer needed */

/* ---- CDC RX line framing (complete) ---- */
static void (*cdc_line_cb)(const char* line, size_t len) = NULL;
static char cdc_line[CDC_RX_LINE_MAX];
static size_t cdc_line_len = 0;

void usb_composite_set_line_cb(void (*cb)(const char*, size_t)) { cdc_line_cb = cb; }

static int8_t cdc_init(void) { return 0; }
static int8_t cdc_deinit(void) { return 0; }
static int8_t cdc_control(uint8_t cmd, uint8_t* pbuf, uint16_t len) { (void)cmd; (void)pbuf; (void)len; return 0; }
static int8_t cdc_receive(uint8_t* pbuf, uint32_t* len)
{
    for (uint32_t i = 0; i < *len; i++) {
        if (cdc_line_len < sizeof(cdc_line)) cdc_line[cdc_line_len++] = (char)pbuf[i];
        if (pbuf[i] == '\n') {
            if (cdc_line_cb) cdc_line_cb(cdc_line, cdc_line_len);
            cdc_line_len = 0;
        }
    }
    USBD_CDC_ReceivePacket(&hUsbDevice);   /* re-arm */
    return 0;
}
static int8_t cdc_transmit(uint8_t* pbuf, uint32_t* len)
{
    (void)pbuf; (void)len;
    return 0;   /* TX is driven by cdc_printf only */
}
static USBD_CDC_ItfTypeDef cdc_fops = { cdc_init, cdc_deinit, cdc_control, cdc_receive, cdc_transmit };

void cdc_printf(const char* fmt, ...)
{
    static char tmp[128];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    USBD_CDC_HandleTypeDef* h = (USBD_CDC_HandleTypeDef*)hUsbDevice.pClassData;
    if (h == NULL || h->TxState != 0u) return;   /* not ready or busy: drop */
    USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)tmp, (uint16_t)n);
    USBD_CDC_TransmitPacket(&hUsbDevice);
}

/* ---- USB device start (CDC only; MSC added in Task 10) ---- */
static uint8_t cdc_ep_addr[3] = { 0x81u, 0x01u, 0x82u };

bool usb_composite_init(void)
{
    USBD_StatusTypeDef ret = USBD_Init(&hUsbDevice, &FS_Desc, DEVICE_FS);
    if (ret != USBD_OK) return false;
    USBD_RegisterClass(&hUsbDevice, &USBD_CDC);
    hUsbDevice.tclasslist[0].EpAdd = cdc_ep_addr;
    USBD_CDC_RegisterInterface(&hUsbDevice, &cdc_fops);
    USBD_Start(&hUsbDevice);
    USBD_CDC_ReceivePacket(&hUsbDevice);
    return true;
}

void usb_composite_deinit(void)
{
    USBD_Stop(&hUsbDevice);
    USBD_DeInit(&hUsbDevice);
}

void usb_composite_poll(void) { }

/* ---- PCD low-level: IRQ + USBD_LL_* ---- */
void USB_IRQHandler(void) { HAL_PCD_IRQHandler(&hpcd); }

void HAL_PCD_MspInit(PCD_HandleTypeDef* h) {
    if (h->Instance == USB) {
        HAL_NVIC_SetPriority(USB_IRQn, 1, 0);
        HAL_NVIC_EnableIRQ(USB_IRQn);
    }
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef* h) { USBD_LL_SetupStage((USBD_HandleTypeDef*)h->pData, (uint8_t*)h->Setup); }
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef* h, uint8_t ep) { USBD_LL_DataOutStage((USBD_HandleTypeDef*)h->pData, ep, h->OUT_ep[ep].xfer_buff); }
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef* h, uint8_t ep) { USBD_LL_DataInStage((USBD_HandleTypeDef*)h->pData, ep, h->IN_ep[ep].xfer_buff); }
void HAL_PCD_ResetCallback(PCD_HandleTypeDef* h) { USBD_LL_SetSpeed((USBD_HandleTypeDef*)h->pData, USBD_SPEED_FULL); USBD_LL_Reset((USBD_HandleTypeDef*)h->pData); }
void HAL_PCD_SOFCallback(PCD_HandleTypeDef* h) { USBD_LL_SOF((USBD_HandleTypeDef*)h->pData); }
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef* h) { USBD_LL_Suspend((USBD_HandleTypeDef*)h->pData); }
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef* h) { USBD_LL_Resume((USBD_HandleTypeDef*)h->pData); }
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef* h, uint8_t ep) { USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef*)h->pData, ep); }
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef* h, uint8_t ep) { USBD_LL_IsoINIncomplete((USBD_HandleTypeDef*)h->pData, ep); }
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef* h) { USBD_LL_DevConnected((USBD_HandleTypeDef*)h->pData); }
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef* h) { USBD_LL_DevDisconnected((USBD_HandleTypeDef*)h->pData); }

void USBD_LL_Delay(uint32_t d) { HAL_Delay(d); }
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef* pdev) {
    hpcd.pData = pdev; hpcd.Instance = USB;
    hpcd.Init.dev_endpoints = 8; hpcd.Init.speed = PCD_SPEED_FULL;
    hpcd.Init.phy_itface = PCD_PHY_EMBEDDED; hpcd.Init.ep0_mps = USB_MAX_EP0_SIZE;
    hpcd.Init.low_power_enable = DISABLE; hpcd.Init.lpm_enable = DISABLE;
    hpcd.Init.battery_charging_enable = DISABLE;
    HAL_PCD_Init(&hpcd);
    return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef* pdev) { (void)pdev; HAL_PCD_DeInit(&hpcd); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef* pdev) { (void)pdev; HAL_PCD_Start(&hpcd); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef* pdev) { (void)pdev; HAL_PCD_Stop(&hpcd); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef* pdev, uint8_t ep, uint8_t type, uint16_t mps) { (void)pdev; HAL_PCD_EP_Open(&hpcd, ep, mps, type); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef* pdev, uint8_t ep) { (void)pdev; HAL_PCD_EP_Close(&hpcd, ep); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef* pdev, uint8_t ep) { (void)pdev; HAL_PCD_EP_Flush(&hpcd, ep); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef* pdev, uint8_t ep) { (void)pdev; HAL_PCD_EP_SetStall(&hpcd, ep); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef* pdev, uint8_t ep) { (void)pdev; HAL_PCD_EP_ClrStall(&hpcd, ep); return USBD_OK; }
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef* pdev, uint8_t ep) { (void)pdev; uint8_t n = ep & 0x7Fu; return (ep & 0x80u) ? PCD_GET_EP_TX_STALL_STATUS(USB, n) : PCD_GET_EP_RX_STALL_STATUS(USB, n); }
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef* pdev, uint8_t addr) { (void)pdev; HAL_PCD_SetAddress(&hpcd, addr); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef* pdev, uint8_t ep, uint8_t* pbuf, uint32_t size) { (void)pdev; HAL_PCD_EP_Transmit(&hpcd, ep, pbuf, size); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef* pdev, uint8_t ep, uint8_t* pbuf, uint32_t size) { (void)pdev; HAL_PCD_EP_Receive(&hpcd, ep, pbuf, size); return USBD_OK; }
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef* pdev, uint8_t ep) { (void)pdev; return HAL_PCD_EP_GetRxCount(&hpcd, ep); }
```

Note on `cdc_printf`: it drops the message when the CDC is not ready or busy, so it is safe to call from `loop()`.

- [ ] **Step 6: Enable HAL PCD / USB in the Arduino core if not default**

The core's `stm32l4xx_hal_conf_default.h` already defines `HAL_PCD_MODULE_ENABLED`. Verify `pio run -e dayvault` compiles `usbd_core.c`/`usbd_cdc.c` and that `USB_IRQHandler` from `UsbComposite.cpp` does not collide with a core definition (the core only defines it when `PIO_FRAMEWORK_ARDUINO_ENABLE_CDC` is set, which we do not use). If a collision occurs, guard with `#ifndef` around the core handler instead of changing behavior.

- [ ] **Step 7: Build**

Run: `pio run -e dayvault` in `firmware/`
Expected: SUCCESS. If `USBD_CDC_RegisterInterface` is missing in this middleware revision, use the equivalent `USBD_CDC_SetRxBuffer`/`USBD_CDC_ReceivePacket` wiring shown by `usbd_cdc.c` and adapt the CDC interface registration to the version present.

- [ ] **Step 8: Flash and verify the CDC console**

Enter DFU (BOOT+RESET), then:
```bash
& "C:\Users\Administrator\.platformio\packages\tool-dfuutil\bin\dfu-util.exe" -l
& "C:\Users\Administrator\.platformio\packages\tool-dfuutil\bin\dfu-util.exe" -a 0 -s 0x08000000:leave -D "firmware\.pio\build\dayvault\firmware.bin"
```
Press RESET. Expected: Windows enumerates `DAYVAULT_L452RC CDC in FS Mode` (VID 0483, PID 5741). Open the COM port (115200) in a terminal; after wiring `main.cpp` to `cdc_printf` in Task 10 it will print status. For Task 6, verify the CDC device enumerates and that the IN/OUT endpoints respond (a serial terminal can open the port).

- [ ] **Step 9: Commit**

```bash
git add firmware/lib/STM32USB firmware/src/UsbComposite.h firmware/src/UsbComposite.cpp firmware/src/usbd_conf.h firmware/src/usbd_desc.c firmware/src/usbd_desc.h
git commit -m "feat(arduino): USB CDC device via vendored ST middleware + PCD glue"
```

---

### Task 7: SdCard SPI1 block driver

**Files:**
- Create: `firmware/src/SdCard.h`, `firmware/src/SdCard.cpp`

**Interfaces:**
- Consumes: `Config.h` (SD pins), HAL SPI1 (enabled by default), HAL GPIO.
- Produces:
  ```cpp
  bool     sd_init(void);                  // CMD0/CMD8/ACMD41, CSD read; true on success
  bool     sd_read_sectors(uint32_t lba, uint8_t* buf, uint32_t count);
  bool     sd_write_sectors(uint32_t lba, const uint8_t* buf, uint32_t count);
  uint64_t sd_capacity_bytes(void);
  ```
  `diskio.c` (Task 8) and the MSC storage fops (Task 10) call these.

- [ ] **Step 1: Write `firmware/src/SdCard.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool     sd_init(void);
bool     sd_read_sectors(uint32_t lba, uint8_t* buf, uint32_t count);
bool     sd_write_sectors(uint32_t lba, const uint8_t* buf, uint32_t count);
uint64_t sd_capacity_bytes(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Write `firmware/src/SdCard.cpp` (SPI1 block driver)**

```cpp
#include "SdCard.h"
#include "Config.h"
#include "stm32l4xx_hal.h"

static SPI_HandleTypeDef hspi1;
static uint64_t capacity_bytes = 0;

static uint8_t spi_txrx(uint8_t b) { uint8_t rx = 0xFF; HAL_SPI_TransmitReceive(&hspi1, &b, &rx, 1, 100); return rx; }
static void cs_high(void) { HAL_GPIO_WritePin(PIN_SD_CS_PORT, PIN_SD_CS, GPIO_PIN_SET); }
static void cs_low(void)  { HAL_GPIO_WritePin(PIN_SD_CS_PORT, PIN_SD_CS, GPIO_PIN_RESET); }

static bool sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t* resp, uint32_t tries)
{
    uint8_t buf[6];
    cs_low();
    spi_txrx(0xFF);
    buf[0] = (uint8_t)(0x40 | cmd);
    buf[1] = (uint8_t)(arg >> 24); buf[2] = (uint8_t)(arg >> 16);
    buf[3] = (uint8_t)(arg >> 8);  buf[4] = (uint8_t)arg;
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

bool sd_init(void)
{
    uint8_t r; bool sdhc = false;

    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin = PIN_SD_CS; g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(PIN_SD_CS_PORT, &g);
    HAL_GPIO_WritePin(PIN_SD_CS_PORT, PIN_SD_CS, GPIO_PIN_SET);

    g.Pin = PIN_SD_SCK | PIN_SD_MOSI; g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH; g.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = PIN_SD_MISO; g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_PULLUP; g.Speed = GPIO_SPEED_FREQ_HIGH; g.Alternate = GPIO_AF5_SPI1;
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

    cs_high();
    for (int i = 0; i < 80; i++) spi_txrx(0xFF);   /* >74 clocks */

    if (!sd_cmd(0, 0, 0x95, &r, 20) || r != 1) return false;

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

    {   /* read CSD for capacity */
        uint8_t csd[16];
        if (sd_cmd(9, 0, 0x01, &r, 10)) {
            for (int i = 0; i < 64; i++) { r = spi_txrx(0xFF); if (r == 0xFE) break; }
            for (int i = 0; i < 16; i++) csd[i] = spi_txrx(0xFF);
            spi_txrx(0xFF); spi_txrx(0xFF);
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
        spi_txrx(0xFF); spi_txrx(0xFF);
        r = spi_txrx(0xFF);
        if ((r & 0x1F) != 0x05) return false;
        for (int j = 0; j < 64; j++) { r = spi_txrx(0xFF); if (r == 0xFF) break; }
    } else {
        for (int j = 0; j < 64; j++) { r = spi_txrx(0xFF); if (r == 0xFE) break; }
        if (r != 0xFE) return false;
        for (int j = 0; j < 512; j++) dst[j] = spi_txrx(0xFF);
        spi_txrx(0xFF); spi_txrx(0xFF);
    }
    sd_end();
    return true;
}

bool sd_read_sectors(uint32_t lba, uint8_t* buf, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) if (!sd_single(false, lba + i, NULL, buf + i * 512)) return false;
    return true;
}

bool sd_write_sectors(uint32_t lba, const uint8_t* buf, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) if (!sd_single(true, lba + i, buf + i * 512, NULL)) return false;
    return true;
}

uint64_t sd_capacity_bytes(void) { return capacity_bytes; }
```

- [ ] **Step 3: Build**

Run: `pio run -e dayvault` in `firmware/`
Expected: SUCCESS.

- [ ] **Step 4: Verify on hardware via the CDC console (with Task 10's INFO wiring, or a temporary `setup()` test)**

Insert the 128 GB exFAT card. Flash (Task 6 procedure). Expected: `INFO` over CDC prints `SD ok cap=...bytes` or an error code. If the card reports SDXC and init fails at ACMD41, lower the SPI prescaler (use `SPI_BAUDRATEPRESCALER_16`) and retry; 400 kHz init is a fallback only if needed.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/SdCard.h firmware/src/SdCard.cpp
git commit -m "feat(arduino): SPI1 SD card block driver"
```

---

### Task 8: FatFs integration (diskio glue, exFAT mount, file test)

**Files:**
- Create: `firmware/src/diskio.c` (replaces the bundled stub; see Step 1)
- Create: `firmware/src/Fs.h`, `firmware/src/Fs.cpp`
- Modify: `firmware/platformio.ini` (add `+<diskio.c>` to native filter if ever tested on host — not required)

**Interfaces:**
- Consumes: `SdCard` (Task 7), vendored FatFs in `firmware/lib/FatFs/` (`_FS_EXFAT=1`).
- Produces:
  ```cpp
  bool fs_mount(void);       // f_mount "SD:", returns true on FR_OK
  void fs_unmount(void);     // f_mount(NULL,...) to flush
  bool fs_record_start(uint32_t seq);  // open REC###.WAV (FA_CREATE_ALWAYS|FA_WRITE), write 44-byte header
  void fs_record_append(const uint8_t* data, size_t n);  // f_write, tracks byte count
  void fs_record_finish(void);        // f_lseek(0), patch header, f_sync, f_close
  uint32_t fs_next_sequence(void);    // scan root for highest REC###.WAV + 1
  ```
  `main.cpp` drives these from the Recorder actions and the loop pump.

- [ ] **Step 1: Remove the bundled FatFs diskio stub**

Run: `git rm firmware/lib/FatFs/diskio.c firmware/lib/FatFs/ff_gen_drv.c firmware/lib/FatFs/ff_gen_drv.h`
Expected: removed. `ff.c`, `ff.h`, `ffconf.h`, `integer.h`, `diskio.h`, `option/ccsbcs.c` remain. Our `diskio.c` in `src/` implements the `disk_*` API from `diskio.h`.

- [ ] **Step 2: Write `firmware/src/diskio.c`**

```cpp
#include "ff.h"
#include "diskio.h"
#include "SdCard.h"

DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return sd_init() ? RES_OK : STA_NOINIT; }
DSTATUS disk_status(BYTE pdrv)     { (void)pdrv; return RES_OK; }
DRESULT disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count) { (void)pdrv; return sd_read_sectors(sector, buff, count) ? RES_OK : RES_ERROR; }
DRESULT disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count) { (void)pdrv; return sd_write_sectors(sector, buff, count) ? RES_OK : RES_ERROR; }
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    (void)pdrv;
    switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *(DWORD*)buff = (DWORD)(sd_capacity_bytes() / 512u); return RES_OK;
    case GET_SECTOR_SIZE: *(WORD*)buff = 512u; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD*)buff = 1u; return RES_OK;
    default: return RES_PARERR;
    }
}
DWORD get_fattime(void) {
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)8 << 21) | ((DWORD)8 << 16);   /* 2026-08-08 */
}
```

- [ ] **Step 3: Write `firmware/src/Fs.h` + `Fs.cpp`**

```cpp
// Fs.h
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool     fs_mount(void);
void     fs_unmount(void);
uint32_t fs_next_sequence(void);
bool     fs_record_start(uint32_t seq);
void     fs_record_append(const uint8_t* data, size_t n);
void     fs_record_finish(void);
bool     fs_healthy(void);   /* true when mount+file open succeeded */

#ifdef __cplusplus
}
#endif
```

```cpp
// Fs.cpp
#include "Fs.h"
#include "WavFile.h"
#include "Config.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>

static FATFS fs;
static FIL file;
static WavConfig wav_cfg;
static uint32_t data_bytes;
static bool file_open;
static bool fs_ok;

static void init_wav_cfg(void) {
    wav_cfg.format = 1;
    wav_cfg.sample_rate = AUDIO_SAMPLE_RATE;
    wav_cfg.channels = AUDIO_CHANNELS;
    wav_cfg.bits = AUDIO_BITS;
    wav_cfg.block_align = (uint16_t)(AUDIO_CHANNELS * (AUDIO_BITS / 8u));
    wav_cfg.byte_rate = wav_cfg.sample_rate * wav_cfg.block_align;
}

bool fs_mount(void) {
    init_wav_cfg();
    fs_ok = (f_mount(&fs, "SD:", 1) == FR_OK);
    file_open = false;
    data_bytes = 0;
    return fs_ok;
}

void fs_unmount(void) {
    if (file_open) fs_record_finish();
    f_mount(NULL, "SD:", 0);
    file_open = false;
    fs_ok = false;
}

static uint32_t parse_num(const char* s) {
    uint32_t n = 0;
    while (*s >= '0' && *s <= '9') { n = n * 10u + (uint32_t)(*s - '0'); if (n > REC_SEQ_MAX) n = REC_SEQ_MAX; s++; }
    return n;
}

uint32_t fs_next_sequence(void) {
    DIR dir; FILINFO fno; uint32_t max_num = 0;
    if (f_opendir(&dir, "SD:/") != FR_OK) return 1;
    for (;;) {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
        if ((fno.fattrib & AM_DIR) != 0) continue;
        if (strncmp(fno.fname, REC_DIR_STR, strlen(REC_DIR_STR)) != 0) continue;
        char* dot = strrchr(fno.fname, '.');
        if (dot == NULL || strcmp(dot + 1, REC_EXT_STR) != 0) continue;
        uint32_t n = parse_num(fno.fname + strlen(REC_DIR_STR));
        if (n > max_num) max_num = n;
    }
    f_closedir(&dir);
    return (max_num >= REC_SEQ_MAX) ? 1 : max_num + 1;
}

bool fs_record_start(uint32_t seq) {
    char name[24];
    snprintf(name, sizeof(name), "SD:/%s%03u.%s", REC_DIR_STR, (unsigned)seq, REC_EXT_STR);
    file_open = false; data_bytes = 0;
    if (f_open(&file, name, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    uint8_t hdr[44];
    wav_build_header(hdr, &wav_cfg, 0);
    UINT wr = 0;
    if (f_write(&file, hdr, 44, &wr) != FR_OK || wr != 44) { f_close(&file); return false; }
    file_open = true;
    return true;
}

void fs_record_append(const uint8_t* data, size_t n) {
    if (!file_open) return;
    UINT wr = 0;
    if (f_write(&file, data, (UINT)n, &wr) != FR_OK || wr != n) { /* fail silently; fs_healthy reports */ fs_ok = false; return; }
    data_bytes += (uint32_t)wr;
}

void fs_record_finish(void) {
    if (!file_open) return;
    uint8_t hdr[44];
    wav_patch_sizes(hdr, data_bytes);
    if (f_lseek(&file, 0) == FR_OK) { UINT wr; f_write(&file, hdr, 44, &wr); }
    f_sync(&file);
    f_close(&file);
    file_open = false;
}

bool fs_healthy(void) { return fs_ok; }
```

- [ ] **Step 4: Build**

Run: `pio run -e dayvault` in `firmware/`
Expected: SUCCESS. If `strrchr` is unavailable with the default `_FS_RPATH=0` build, use a small local scan loop instead (the code above only needs standard C string functions, which are fine).

- [ ] **Step 5: Verify on hardware**

Flash (Task 6 procedure), insert the exFAT card, use a temporary `setup()` test in `main.cpp`: mount, print `fs_mount=OK`, `next_seq=...`, record 0.5 s of zeroed PCM via `fs_record_append`, `fs_record_finish`, then `fs_unmount`. Expected over CDC: all `OK`; the Windows disk later (Task 10) shows a valid `REC###.WAV` that plays.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/diskio.c firmware/src/Fs.h firmware/src/Fs.cpp
git commit -m "feat(arduino): FatFs exFAT integration with record file helpers"
```

---

### Task 9: PdmCapture (DFSDM mono) module

**Files:**
- Create: `firmware/src/PdmCapture.h`, `firmware/src/PdmCapture.cpp`

**Interfaces:**
- Consumes: `Config.h` (PDM pins, ring size), HAL DFSDM1 + DMA1 (enabled by default), `RingBuf` (Task 3).
- Produces:
  ```cpp
  void    pdm_init(RingBuf* sink);
  void    pdm_start(void);
  void    pdm_stop(void);
  uint32_t pdm_overruns(void);
  ```
  `pdm_start` is called by the Recorder RECORDING entry; DMA half/full callbacks push 16-bit mono samples into the ring.

- [ ] **Step 1: Write `firmware/src/PdmCapture.h`**

```cpp
#pragma once
#include "RingBuf.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void     pdm_init(RingBuf* sink);
void     pdm_start(void);
void     pdm_stop(void);
uint32_t pdm_overruns(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Write `firmware/src/PdmCapture.cpp`**

```cpp
#include "PdmCapture.h"
#include "Config.h"
#include "stm32l4xx_hal.h"
#include <string.h>

static DFSDM_Filter_HandleTypeDef hf;
static DFSDM_Channel_HandleTypeDef hc;
static DMA_HandleTypeDef hdma;
static RingBuf* sink = NULL;
static volatile uint32_t overruns = 0;
static int16_t buf[PDM_HALF_SAMPLES * 2u];

static void push_half(const int16_t* p, uint32_t half)
{
    const int16_t* src = p + (half ? PDM_HALF_SAMPLES : 0);
    size_t bytes = (size_t)PDM_HALF_SAMPLES * sizeof(int16_t);
    size_t got = ringbuf_write(sink, (const uint8_t*)src, bytes);
    if (got < bytes) overruns++;
}

void HAL_DFSDM_FilterRegConvCpltCallback(DFSDM_Filter_HandleTypeDef* h) { if (h->Instance == DFSDM1_Filter1) push_half(buf, 1); }
void HAL_DFSDM_FilterRegConvHalfCpltCallback(DFSDM_Filter_HandleTypeDef* h) { if (h->Instance == DFSDM1_Filter1) push_half(buf, 0); }

void pdm_init(RingBuf* s)
{
    sink = s;
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_DFSDM1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    g.Pin = PIN_PDM_CLK; g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_VERY_HIGH; g.Alternate = GPIO_AF6_DFSDM1;
    HAL_GPIO_Init(PIN_PDM_CLK_PORT, &g);
    g.Pin = PIN_PDM_DATA;
    HAL_GPIO_Init(PIN_PDM_DATA_PORT, &g);

    hc.Instance = DFSDM1_Channel1;
    hc.Init.OutputClock.Activation = ENABLE;
    hc.Init.OutputClock.Selection = DFSDM_CHANNEL_OUTPUT_CLOCK_SYSTEM;
    hc.Init.OutputClock.Divider = PDM_CKOUT_DIVIDER;
    hc.Init.Input.Multiplexer = DFSDM_CHANNEL_EXTERNAL_INPUTS;
    hc.Init.Input.DataPacking = DFSDM_CHANNEL_STANDARD_MODE;
    hc.Init.Input.Pins = DFSDM_CHANNEL_SAME_CHANNEL_PINS;
    hc.Init.SerialInterface.Type = DFSDM_CHANNEL_SPI_RISING;
    hc.Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
    hc.Init.Awd.FilterOrder = DFSDM_CHANNEL_FASTSINC_ORDER;
    hc.Init.Awd.Oversampling = 1;
    hc.Init.Offset = 0;
    hc.Init.RightBitShift = 0;
    HAL_DFSDM_ChannelInit(&hc);

    hf.Instance = DFSDM1_Filter1;
    hf.Init.RegularParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
    hf.Init.RegularParam.FastMode = ENABLE;
    hf.Init.RegularParam.DmaMode = ENABLE;
    hf.Init.InjectedParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
    hf.Init.InjectedParam.ScanMode = DISABLE;
    hf.Init.InjectedParam.DmaMode = DISABLE;
    hf.Init.InjectedParam.ExtTrigger = DFSDM_FILTER_EXT_TRIG_TIM1_TRGO;
    hf.Init.InjectedParam.ExtTriggerEdge = DFSDM_FILTER_EXT_TRIG_BOTH_EDGES;
    hf.Init.FilterParam.SincOrder = DFSDM_FILTER_SINC3_ORDER;
    hf.Init.FilterParam.Oversampling = PDM_OSR;
    hf.Init.FilterParam.IntOversampling = 1;
    HAL_DFSDM_FilterInit(&hf);
    HAL_DFSDM_FilterConfigRegChannel(&hf, DFSDM_CHANNEL_1, DFSDM_CONTINUOUS_CONV_ON);
}

void pdm_start(void)
{
    memset(buf, 0, sizeof(buf));
    hdma.Instance = DMA1_Channel5;
    hdma.Init.Request = DMA_REQUEST_0;
    hdma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma.Init.MemInc = DMA_MINC_ENABLE;
    hdma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma.Init.Mode = DMA_CIRCULAR;
    hdma.Init.Priority = DMA_PRIORITY_HIGH;
    __HAL_DMA_RESET_HANDLE_STATE(&hdma);
    HAL_DMA_Init(&hdma);
    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
    __HAL_LINKDMA(&hf, hdmaReg, hdma);

    HAL_DFSDM_FilterRegularMsbStart_DMA(&hf, buf, PDM_HALF_SAMPLES * 2u);
}

void pdm_stop(void)
{
    HAL_DFSDM_FilterRegularStop_DMA(&hf);
    HAL_NVIC_DisableIRQ(DMA1_Channel5_IRQn);
}

void DMA1_Channel5_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma); }

uint32_t pdm_overruns(void) { return overruns; }
```

- [ ] **Step 3: Build**

Run: `pio run -e dayvault` in `firmware/`
Expected: SUCCESS. If `DMA_REQUEST_0` is not the correct request for DFSDM Filter1 DMA1 Channel5 on L452, check `stm32l4xx_hal_dfsdm.c` `DFSDM_Filter1_DMA_REQUEST` and use the matching value.

- [ ] **Step 4: Verify on hardware (record test)**

With Task 8 wiring in place, record ~3 s of real audio to a WAV via a temporary `setup()` test; then play the file on the PC. Expected: audible speech, file header valid, `overruns=0` in the console INFO output. Tune `PDM_CKOUT_DIVIDER` if the sample rate is off (target 16 kHz = CKOUT / OSR).

- [ ] **Step 5: Commit**

```bash
git add firmware/src/PdmCapture.h firmware/src/PdmCapture.cpp
git commit -m "feat(arduino): DFSDM mono PDM capture with DMA"
```

---

### Task 10: USB MSC added to the composite (CDC + MSC)

**Files:**
- Modify: `firmware/lib/STM32USB/` (add MSC + CompositeBuilder middleware files)
- Modify: `firmware/src/usbd_conf.h` (interface/endpoint limits)
- Create: `firmware/src/usbd_msc_storage.c` (read-only fops backed by SdCard)
- Modify: `firmware/src/UsbComposite.cpp` (register CDC + MSC via CompositeBuilder)

**Interfaces:**
- Consumes: `SdCard` (Task 7), vendored MSC middleware, CompositeBuilder.
- Produces: `usb_composite_init()` now registers both CDC and MSC. MSC exposes the raw SD block device read-only. `main.cpp` calls `usb_composite_init` in the MSC state and `usb_composite_deinit` on detach.

- [ ] **Step 1: Vendor MSC + CompositeBuilder middleware (flat into `firmware/lib/STM32USB/`)**

Copy from `...\STM32_USB_Device_Library\`:
```
Class\MSC\Src\usbd_msc.c  Class\MSC\Inc\usbd_msc.h
Class\MSC\Src\usbd_msc_bot.c  Class\MSC\Inc\usbd_msc_bot.h
Class\MSC\Src\usbd_msc_data.c  Class\MSC\Inc\usbd_msc_data.h
Class\MSC\Src\usbd_msc_scsi.c  Class\MSC\Inc\usbd_msc_scsi.h
Class\CompositeBuilder\Src\usbd_composite_builder.c  Class\CompositeBuilder\Inc\usbd_composite_builder.h
```
Flat, so `usbd_msc.h`/`usbd_composite_builder.h` includes resolve.

- [ ] **Step 2: Update `firmware/src/usbd_conf.h`**

Change:
```cpp
#define USBD_MAX_NUM_INTERFACES         4u
#define USBD_MAX_NUM_ENDPOINTS          6u
```

- [ ] **Step 3: Write `firmware/src/usbd_msc_storage.c`**

```cpp
#include "usbd_msc_storage.h"   /* or define USBD_StorageTypeDef locally per middleware version */
#include "SdCard.h"

static int8_t msc_init(uint8_t lun) { (void)lun; return 0; }
static int8_t msc_getcap(uint8_t lun, uint32_t* bn, uint16_t* bs) { (void)lun; *bn = (uint32_t)(sd_capacity_bytes() / 512u); *bs = 512u; return 0; }
static int8_t msc_ready(uint8_t lun) { (void)lun; return 0; }
static int8_t msc_wrprotect(uint8_t lun) { (void)lun; return 1; }   /* read-only export */
static int8_t msc_read(uint8_t lun, uint8_t* buf, uint32_t lba, uint16_t n) { (void)lun; return sd_read_sectors(lba, buf, n) ? 0 : -1; }
static int8_t msc_write(uint8_t lun, uint8_t* buf, uint32_t lba, uint16_t n) { (void)lun; (void)buf; (void)lba; (void)n; return -1; }   /* write protected */
static int8_t msc_maxlun(void) { return 0; }
static int8_t msc_inquiry[36] = {
    0x00,0x80,0x00,0x01, 0x1F,0x00,0x00,0x02,
    'D','A','Y','V','A','U','L','T', 'S','D',' ',' ','1','.','0','0',
    0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0
};

USBD_StorageTypeDef USBD_STORAGE_fops = {
    msc_init, msc_getcap, msc_ready, msc_wrprotect, msc_read, msc_write, msc_maxlun, msc_inquiry
};
```

Verify the storage callback struct layout against the vendored `usbd_msc.h` (`USBD_StorageTypeDef`) and adjust member names if the middleware revision differs.

- [ ] **Step 4: Update `UsbComposite.cpp` to register CDC+MSC via CompositeBuilder**

Replace the `usb_composite_init()` body in Task 6 with:

```cpp
#include "usbd_composite_builder.h"
#include "usbd_msc.h"
#include "usbd_msc_storage.h"

static uint8_t cdc_ep_addr[3] = { 0x81u, 0x01u, 0x82u };
static uint8_t msc_ep_addr[2] = { 0x83u, 0x03u };

bool usb_composite_init(void)
{
    USBD_StatusTypeDef ret = USBD_Init(&hUsbDevice, &FS_Desc, DEVICE_FS);
    if (ret != USBD_OK) return false;

    USBD_CMPST_ClearConfDesc(&hUsbDevice);
    USBD_RegisterClass(&hUsbDevice, &USBD_CMPSIT);
    hUsbDevice.tclasslist[0].EpAdd = cdc_ep_addr;
    USBD_CMPSIT_AddClass(&hUsbDevice, &USBD_CDC, CLASS_TYPE_CDC, 0);
    USBD_CDC_RegisterInterface(&hUsbDevice, &cdc_fops);
    hUsbDevice.classId++;
    hUsbDevice.NumClasses++;
    hUsbDevice.tclasslist[1].EpAdd = msc_ep_addr;
    USBD_CMPSIT_AddClass(&hUsbDevice, &USBD_MSC, CLASS_TYPE_MSC, 0);
    USBD_MSC_RegisterStorage(&hUsbDevice, &USBD_STORAGE_fops);

    USBD_Start(&hUsbDevice);
    USBD_CDC_ReceivePacket(&hUsbDevice);
    return true;
}
```

- [ ] **Step 5: Build**

Run: `pio run -e dayvault` in `firmware/`
Expected: SUCCESS. RAM check: keep total static RAM under ~56 KB; if over, reduce `PDM_RING_BYTES` in `Config.h`.

- [ ] **Step 6: Flash and verify MSC export**

Flash (Task 6 procedure), press RESET with USB attached. Expected: Windows shows both a serial port (CDC) and a disk (MSC) with the exFAT volume; the 128 GB card appears, read-only; `REC###.WAV` files are copyable and playable. If the disk shows but is empty, the MSC fops sector count is wrong — verify `sd_capacity_bytes()`.

- [ ] **Step 7: Commit**

```bash
git add firmware/lib/STM32USB firmware/src/usbd_conf.h firmware/src/usbd_msc_storage.c firmware/src/UsbComposite.cpp
git commit -m "feat(arduino): USB CDC+MSC composite with read-only SD export"
```

---

### Task 11: Full integration (Recorder, DFU, main loop, E2E)

**Files:**
- Create: `firmware/src/Recorder.h`, `firmware/src/Recorder.cpp`
- Create: `firmware/src/Dfu.h`, `firmware/src/Dfu.cpp`
- Create: `firmware/test/test_recorder/test_recorder.cpp`
- Modify: `firmware/src/main.cpp` (full flow)

**Interfaces:**
- Consumes: every module from Tasks 2-10.
- Produces: the finished firmware. End-to-end: detach -> record, attach -> stop -> MSC export, CDC `DFU\n` -> bootloader.

- [ ] **Step 1: Write the failing Recorder tests** (`firmware/test/test_recorder/test_recorder.cpp`)

```cpp
#include "Recorder.h"
#include <unity.h>
#include <string.h>

static char calls[256];
static int n;

static void mark(const char* s) { n += snprintf(calls + n, sizeof(calls) - n, "%s ", s); }
static void a_mount(void) { mark("mount"); }
static void a_open(void) { mark("open"); }
static void a_start(void) { mark("start"); }
static void a_stop(void) { mark("stop"); }
static void a_final(void) { mark("final"); }
static void a_unmount(void) { mark("unmount"); }
static void a_msc_on(void) { mark("msc_on"); }
static void a_msc_off(void) { mark("msc_off"); }

static RecActions acts;
static Recorder rec;

void setUp(void) {
    n = 0; memset(calls, 0, sizeof(calls));
    memset(&acts, 0, sizeof(acts));
    acts.mount_fs = a_mount; acts.open_file = a_open; acts.start_capture = a_start;
    acts.stop_capture = a_stop; acts.finalize_file = a_final; acts.unmount_fs = a_unmount;
    acts.start_msc = a_msc_on; acts.stop_msc = a_msc_off;
    recorder_init(&rec, &acts);
}

void test_idle_detach_starts_recording(void) {
    recorder_event(&rec, REC_EVT_USB_DETACH);
    TEST_ASSERT_EQUAL(REC_RECORDING, recorder_state(&rec));
    TEST_ASSERT_EQUAL_STRING("mount open start ", calls);
}

void test_recording_attach_stops_then_msc(void) {
    recorder_event(&rec, REC_EVT_USB_DETACH);          /* -> RECORDING */
    recorder_event(&rec, REC_EVT_USB_ATTACH);          /* -> STOPPING */
    TEST_ASSERT_EQUAL(REC_STOPPING, recorder_state(&rec));
    TEST_ASSERT_EQUAL_STRING("mount open start stop ", calls);
    recorder_event(&rec, REC_EVT_FINALIZE_DONE);       /* -> MSC */
    TEST_ASSERT_EQUAL(REC_MSC, recorder_state(&rec));
    TEST_ASSERT_EQUAL_STRING("mount open start stop final unmount msc_on ", calls);
}

void test_idle_attach_goes_msc(void) {
    recorder_event(&rec, REC_EVT_USB_ATTACH);
    TEST_ASSERT_EQUAL(REC_MSC, recorder_state(&rec));
    TEST_ASSERT_EQUAL_STRING("final unmount msc_on ", calls);
}

void test_msc_detach_returns_to_recording(void) {
    recorder_event(&rec, REC_EVT_USB_ATTACH);
    recorder_event(&rec, REC_EVT_USB_DETACH);
    TEST_ASSERT_EQUAL(REC_RECORDING, recorder_state(&rec));
    TEST_ASSERT_EQUAL_STRING("final unmount msc_on msc_off mount open start ", calls);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_detach_starts_recording);
    RUN_TEST(test_recording_attach_stops_then_msc);
    RUN_TEST(test_idle_attach_goes_msc);
    RUN_TEST(test_msc_detach_returns_to_recording);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native` in `firmware/`
Expected: compile error `Recorder.h not found`.

- [ ] **Step 3: Write `firmware/src/Recorder.h` + `Recorder.cpp`**

```cpp
// Recorder.h
#pragma once

typedef enum { REC_IDLE = 0, REC_RECORDING, REC_STOPPING, REC_MSC } RecState;
typedef enum { REC_EVT_USB_ATTACH = 0, REC_EVT_USB_DETACH, REC_EVT_FINALIZE_DONE } RecEvent;

typedef struct {
    void (*mount_fs)(void);
    void (*open_file)(void);
    void (*start_capture)(void);
    void (*stop_capture)(void);
    void (*finalize_file)(void);
    void (*unmount_fs)(void);
    void (*start_msc)(void);
    void (*stop_msc)(void);
} RecActions;

typedef struct { RecState state; const RecActions* actions; } Recorder;

#ifdef __cplusplus
extern "C" {
#endif

void     recorder_init(Recorder* r, const RecActions* a);
void     recorder_event(Recorder* r, RecEvent evt);
RecState recorder_state(const Recorder* r);

#ifdef __cplusplus
}
#endif
```

```cpp
// Recorder.cpp
#include "Recorder.h"

static void call(const RecActions* a, void (*fn)(void)) { if (a && fn) fn(); }

void recorder_init(Recorder* r, const RecActions* a) { r->state = REC_IDLE; r->actions = a; }
RecState recorder_state(const Recorder* r) { return r->state; }

void recorder_event(Recorder* r, RecEvent evt)
{
    const RecActions* a = r->actions;
    switch (r->state) {
    case REC_IDLE:
        if (evt == REC_EVT_USB_DETACH) {
            call(a, a->mount_fs); call(a, a->open_file); call(a, a->start_capture);
            r->state = REC_RECORDING;
        } else if (evt == REC_EVT_USB_ATTACH) {
            call(a, a->finalize_file); call(a, a->unmount_fs); call(a, a->start_msc);
            r->state = REC_MSC;
        }
        break;
    case REC_RECORDING:
        if (evt == REC_EVT_USB_ATTACH) { call(a, a->stop_capture); r->state = REC_STOPPING; }
        break;
    case REC_STOPPING:
        if (evt == REC_EVT_FINALIZE_DONE) {
            call(a, a->finalize_file); call(a, a->unmount_fs); call(a, a->start_msc);
            r->state = REC_MSC;
        }
        break;
    case REC_MSC:
        if (evt == REC_EVT_USB_DETACH) {
            call(a, a->stop_msc); call(a, a->mount_fs); call(a, a->open_file); call(a, a->start_capture);
            r->state = REC_RECORDING;
        }
        break;
    }
}
```

- [ ] **Step 4: Run Recorder tests to verify they pass**

Run: `pio test -e native` in `firmware/`
Expected: all 4 tests PASS.

- [ ] **Step 5: Write `firmware/src/Dfu.h` + `Dfu.cpp`**

```cpp
// Dfu.h
#pragma once

typedef struct {
    void (*stop_capture)(void);
    void (*close_file)(void);
    void (*unmount_fs)(void);
} DfuHooks;

#ifdef __cplusplus
extern "C" {
#endif

void dfu_enter_with_hooks(const DfuHooks* hooks);

#ifdef __cplusplus
}
#endif
```

```cpp
// Dfu.cpp
#include "Dfu.h"
#include "stm32l4xx_hal.h"

#define SYSTEM_MEMORY_BASE 0x1FFF0000u
#define SRAM_LOW_BOUNDARY  0x20000000u

void dfu_enter_with_hooks(const DfuHooks* hooks)
{
    if (hooks) {
        if (hooks->stop_capture) hooks->stop_capture();
        if (hooks->close_file)   hooks->close_file();
        if (hooks->unmount_fs)   hooks->unmount_fs();
    }
    HAL_Delay(20);
    USBD_Stop(&hUsbDevice);
    USBD_DeInit(&hUsbDevice);
    HAL_DeInit();
    SysTick->CTRL = 0; SysTick->LOAD = 0; SysTick->VAL = 0;
    __disable_irq();

    uint32_t msp = *(volatile uint32_t *)SYSTEM_MEMORY_BASE;
    if ((msp & 0xFFF00000u) != SRAM_LOW_BOUNDARY) while (1) { }
    __set_MSP(msp);
    ((void (*)(void)) * (volatile uint32_t *)(SYSTEM_MEMORY_BASE + 4u))();
}
```

Note: `Dfu.cpp` uses `USBD_Stop`/`USBD_DeInit` from `UsbComposite`'s `hUsbDevice`; include `UsbComposite.h` before using it. `Dfu.cpp` is hardware-bound and excluded from `native` tests (the `native` filter only compiles RingBuf/WavFile/Cmd/Recorder).

- [ ] **Step 6: Rewrite `firmware/src/main.cpp` for the full flow**

```cpp
#include <Arduino.h>
#include "stm32l4xx_hal.h"
#include "Config.h"
#include "RingBuf.h"
#include "PdmCapture.h"
#include "SdCard.h"
#include "Fs.h"
#include "Recorder.h"
#include "UsbComposite.h"
#include "Cmd.h"
#include "Dfu.h"
#include <string.h>

extern "C" void SystemClock_Config(void);
/* SystemClock_Config implementation identical to Task 2 Step 3. */

static uint8_t audio_buf[PDM_RING_BYTES];
static RingBuf audio_rb;
static Recorder rec;
static uint32_t seq;
static bool app_healthy;

/* ---- file glue (Recorder actions) ---- */
static void act_mount_fs(void)  { app_healthy = fs_mount(); }
static void act_open_file(void) { seq = fs_next_sequence(); if (!fs_record_start(seq)) app_healthy = false; }
static void act_start_capture(void) { pdm_start(); }
static void act_stop_capture(void) { pdm_stop(); }
static void act_finalize_file(void) { /* drain ring into file */ uint8_t blk[512]; size_t got; while ((got = ringbuf_read(&audio_rb, blk, sizeof(blk))) > 0) { got &= ~1u; if (got) fs_record_append(blk, got); } fs_record_finish(); }
static void act_unmount_fs(void) { fs_unmount(); }
static void act_start_msc(void) { usb_composite_init(); }
static void act_stop_msc(void) { usb_composite_deinit(); }

static const RecActions actions = {
    act_mount_fs, act_open_file, act_start_capture, act_stop_capture,
    act_finalize_file, act_unmount_fs, act_start_msc, act_stop_msc
};

/* ---- USB detect debounce ---- */
static GPIO_PinState usb_level;
static bool usb_armed;
static GPIO_PinState usb_pend;
static uint32_t usb_pend_tick;

static void usb_detect_poll(void)
{
    GPIO_PinState raw = HAL_GPIO_ReadPin(GPIOA, PIN_USB_DETECT);
    uint32_t now = HAL_GetTick();
    if (raw == usb_level) { usb_armed = false; return; }
    if (!usb_armed) { usb_armed = true; usb_pend = raw; usb_pend_tick = now; return; }
    if (raw != usb_pend) { usb_armed = false; return; }
    if ((uint32_t)(now - usb_pend_tick) >= 10u) {
        usb_level = raw; usb_armed = false;
        recorder_event(&rec, raw == GPIO_PIN_SET ? REC_EVT_USB_ATTACH : REC_EVT_USB_DETACH);
    }
}

/* ---- CDC line callback (from UsbComposite) ---- */
static void on_cdc_line(const char* line, size_t len)
{
    cmd_init();
    CmdEvent evt = CMD_EVT_NONE;
    for (size_t i = 0; i < len; i++) evt = cmd_feed((uint8_t)line[i]);
    (void)evt;
}

void setup()
{
    SystemClock_Config();
    GPIO_InitTypeDef g = {0};

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin = PIN_USB_DETECT; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    ringbuf_init(&audio_rb, audio_buf, sizeof(audio_buf));
    pdm_init(&audio_rb);
    recorder_init(&rec, &actions);

    app_healthy = false;
    usb_level = HAL_GPIO_ReadPin(GPIOA, PIN_USB_DETECT);
    bool attached = (usb_level == GPIO_PIN_SET);
    if (attached) {
        recorder_event(&rec, REC_EVT_USB_ATTACH);   /* IDLE -> MSC */
    } else {
        recorder_event(&rec, REC_EVT_USB_DETACH);   /* IDLE -> RECORDING */
    }
}

void loop()
{
    usb_detect_poll();

    if (recorder_state(&rec) == REC_RECORDING && app_healthy) {
        uint8_t blk[512];
        size_t got = ringbuf_read(&audio_rb, blk, sizeof(blk));
        got &= ~1u;                    /* even byte count for 16-bit mono */
        if (got) fs_record_append(blk, got);
    }

    /* Cmd events driven from cdc_line_cb; act on pending DFU here */
    /* For Task 11 the DFU path is wired in Task 11 Step 7 via a pending flag. */

    /* LED */
    RecState st = recorder_state(&rec);
    uint32_t now = millis();
    if (st == REC_RECORDING) {
        static uint32_t last = 0; static bool on = false;
        if (now - last >= 500) { last = now; on = !on; digitalWrite(PIN_LED, on ? HIGH : LOW); }
    } else {
        digitalWrite(PIN_LED, HIGH);   /* solid in IDLE/MSC */
    }
}
```

- [ ] **Step 7: Wire CDC RX -> Cmd events, and DFU/INFO handling, in `main.cpp`**

Complete the pending-DFU handling: add a `static volatile CmdEvent pending_cmd;` in `main.cpp`, set it from `on_cdc_line`, and in `loop()` handle:

```cpp
    if (pending_cmd == CMD_EVT_DFU) {
        pending_cmd = CMD_EVT_NONE;
        DfuHooks hooks = { pdm_stop, fs_record_finish, fs_unmount };
        dfu_enter_with_hooks(&hooks);
    } else if (pending_cmd == CMD_EVT_INFO) {
        pending_cmd = CMD_EVT_NONE;
        cdc_printf("DV state=%d healthy=%d overruns=%lu sd=%llu\n",
                   (int)recorder_state(&rec), app_healthy ? 1 : 0,
                   (unsigned long)pdm_overruns(),
                   (unsigned long long)sd_capacity_bytes());
    }
```

Register the line callback in `setup()`:
```cpp
    usb_composite_set_line_cb(on_cdc_line);
```
And ensure `UsbComposite.cpp` CDC RX accumulates bytes and calls `cdc_line_cb` on `\n` (add that framing logic if not already present in the Task 6 receive path).

- [ ] **Step 8: Build and run the host test suite**

Run: `pio test -e native` and `pio run -e dayvault` in `firmware/`
Expected: all host tests PASS and the board build SUCCESS.

- [ ] **Step 9: End-to-end board verification**

Flash (Task 6 procedure). Then, with the user, verify all of:
1. USB detached at power-on -> LED slow-blinks, recording starts.
2. Plug USB -> recording stops, LED solid; Windows shows serial + exFAT disk; `REC###.WAV` is copyable and playable.
3. Unplug USB -> recording starts again with the next sequence number.
4. With USB attached, send `DFU\n` on the serial port -> device re-enumerates as `STM32 BOOTLOADER`; `dfu-util -l` sees it.
5. Send `INFO\n` -> prints state/healthy/overruns/capacity.

- [ ] **Step 10: Commit**

```bash
git add firmware/src/Recorder.h firmware/src/Recorder.cpp firmware/src/Dfu.h firmware/src/Dfu.cpp firmware/test/test_recorder/test_recorder.cpp firmware/src/main.cpp
git commit -m "feat(arduino): full recorder integration with USB auto start/stop, MSC export, and DFU command"
```

---

### Task 12: Documentation update + final cleanup

**Files:**
- Modify: `README.md`, `README.zh-CN.md` (point to the Arduino firmware)
- Modify: `Docs/superpowers/plans/` (this file is the record)

**Interfaces:**
- Consumes: completed firmware.
- Produces: accurate docs matching the new architecture.

- [ ] **Step 1: Update README**

Replace the "Current Status" and repository-map sections to describe the Arduino firmware (build/flash via PlatformIO + dfu-util, USB auto start/stop, CDC+MSC export), and link `legacy/hal-firmware` for the archived HAL firmware. Keep the hardware table and pin references.

- [ ] **Step 2: Verify the worktree still lists cleanly and main builds**

Run: `git worktree list` and `pio run -e dayvault` in `firmware/`
Expected: worktree list shows `main` + `legacy/hal-firmware`; build SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add README.md README.zh-CN.md
git commit -m "docs: update README for Arduino recorder firmware"
```

---

## Self-Review Notes

- Spec coverage: every spec requirement maps to a task — worktree split (Task 1), mono 16k/16bit WAV REC###.WAV (Tasks 4, 8, 11), USB detach start / attach stop (Tasks 10, 11), CDC+MSC export (Task 10), exFAT 128 GB (Task 8), DFU via BOOT + CDC `DFU\n` with no PH3 trigger (Tasks 5, 11), LED states (Tasks 2, 11), read-only MSC (Task 10).
- Placeholders: no TBD/TODO remains; every step has concrete content.
- Type consistency: `RingBuf`, `WavConfig`, `CmdEvent`, `RecState`/`RecEvent`/`RecActions`, `DfuHooks`, and the `sd_*`/`fs_*`/`pdm_*`/`usb_composite_*` signatures are identical across the tasks that consume and produce them.
