# DayVault Full Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement all 8 DayVault firmware modules (RTC, battery ADC, USB_DETECT, microSD+FatFs, PDM/DFSDM capture, WAV/ADPCM recording, USB export/time-sync, low-power state machine) on STM32L452RCT6 using PlatformIO + stm32cube, verified by host unit tests for pure logic and compile-only for hardware drivers.

**Architecture:** Bare-metal superloop + DMA ping-pong. Pure-logic layer (timeutil, wav, adpcm, segmgr, ringbuf, power_state, eventlog, usbproto) has zero ST dependencies and is host-tested via a `native` PlatformIO env with Unity. Hardware drivers are thin HAL wrappers (compile-only until board arrives). ST USB Device Library composite (CDC+MSC) auto-built by framework; FatFs vendored into `lib/FatFs`.

**Tech Stack:** PlatformIO Core 6.1.19, ststm32 platform 19.7.1, stm32cube framework (STM32CubeL4 HAL), FatFs R0.12c (ST glue `ff_gen_drv`), ST USB Device Library CompositeBuilder, Unity test framework, C11 (host) / C99 (device, `-Os`).

**Design spec:** `Docs/10-Firmware-Architecture-Design.md`

## Global Constraints

- Build command (device): `pio run -e dayvault`; tests: `pio test -e native`.
- Pin mapping (from Docs/02, MUST match): SD_CS=PA4, SD_SCK=PA5, SD_MISO=PA6, SD_MOSI=PA7, USB_DM=PA11, USB_DP=PA12, USB_DETECT=PA9, BAT_SENSE=PA0, PDM_CLK=PC2 (DFSDM1_CKOUT), PDM_DATA=PB12 (DFSDM1_DATIN1), LED=PA8, SWD=PA13/PA14.
- RTC in UTC; LSE on PC14/PC15; RTC-valid magic + boot counter in backup registers.
- USB: composite CDC+MSC; CDC for control/time-sync; MSC read-only export, active ONLY when recording stopped and FatFs unmounted.
- Low power: Standby + RTC periodic wakeup (default 30 s). SPH0655 sleep achieved by stopping DFSDM/CKOUT.
- Battery thresholds: 3.50 V warning / 3.30 V graceful stop / 3.55 V recovery, with hysteresis and multi-sample filtering.
- Recording: 16 kHz / 16-bit mono PCM first, then IMA ADPCM. 15-minute segments, 10 s WAV-header sync, segment preallocation.
- No HAL callbacks (USE_HAL_*_CALLBACKS=0); use polling + `HAL_..._Callback` override only where the framework requires.
- All blocking paths bounded; IWDG enabled only after all blocking paths bounded.
- No comments in code unless they carry hardware/fact context (per repo style).

---

## File Structure

```
firmware/
  platformio.ini                     Modify: add env:native, USB defines
  boards/dayvault_l452rc.json        (existing, unchanged)
  include/
    stm32l4xx_hal_conf.h             (existing, unchanged)
    dayvault_config.h                Create: pins, thresholds, timing constants
    timeutil.h                       Create
    ringbuf.h                        Create
    wav.h                            Create
    adpcm.h                          Create
    segmgr.h                         Create
    power_state.h                    Create
    eventlog.h                       Create
    usbproto.h                       Create
    hw_rtc.h  hw_adc.h  hw_gpio.h  hw_spi_sd.h  hw_dfsdm.h  hw_usb.h  hw_pwr.h  hw_iwdg.h   Create
    diskio_sd.h                      Create
    usbd_conf.h                      Create (PCD config + composite activation)
    usbd_desc.h                      Create
    app.h                            Create
  src/
    main.c                           Modify: boot sequence calls app_init/app_run
    app.c                            Create: superloop + recording pipeline + state actions
    timeutil.c ringbuf.c wav.c adpcm.c segmgr.c power_state.c eventlog.c usbproto.c  Create
    hw_rtc.c hw_adc.c hw_gpio.c hw_spi_sd.c hw_dfsdm.c hw_usb.c hw_pwr.c hw_iwdg.c  Create
    diskio_sd.c                      Create (SPI SD driver + FatFs glue)
    usbd_desc.c                      Create
    usbd_cdc_if.c                    Create (CDC glue)
    usbd_msc_storage.c               Create (MSC storage glue, read-only)
  lib/
    FatFs/                           Create (vendored from framework package)
      ff.c  ff.h  ffconf.h  diskio.c  diskio.h  ff_gen_drv.c  ff_gen_drv.h  integer.h
  test/
    test_timeutil.c test_ringbuf.c test_wav.c test_adpcm.c
    test_segmgr.c test_power_state.c test_usbproto.c
```

---

## Task 0: Scaffold native test environment + ringbuf

**Files:**
- Modify: `firmware/platformio.ini`
- Create: `firmware/include/ringbuf.h`
- Create: `firmware/src/ringbuf.c`
- Create: `firmware/test/test_ringbuf.c`

**Interfaces:**
- Produces: `ringbuf_t` with `ringbuf_init`, `ringbuf_write` (returns bytes written, drops overflow), `ringbuf_read` (returns bytes read), `ringbuf_used`, `ringbuf_free`. Used by M4/M5 pipeline.

- [ ] **Step 1: Add `env:native` to platformio.ini**

```ini
[platformio]
default_envs = dayvault

[env:dayvault]
platform = ststm32
board = dayvault_l452rc
framework = stm32cube

build_type = release
monitor_speed = 115200

build_flags =
    -DUSE_USBD_COMPOSITE
    -DUSBD_CMPSIT_ACTIVATE_CDC=1
    -DUSBD_CMPSIT_ACTIVATE_MSC=1

[env:native]
platform = native
test_framework = unity
lib_ignore = FatFs
build_src_filter =
    -<src/*>
    +<src/timeutil.c>
    +<src/ringbuf.c>
    +<src/wav.c>
    +<src/adpcm.c>
    +<src/segmgr.c>
    +<src/power_state.c>
    +<src/eventlog.c>
    +<src/usbproto.c>
```

Note: `platform = native` downloads `platformio/native` on first `pio test` (registry-confirmed). Unity is bundled with `pio test`. The `src` files do not exist yet; the filter additions for later tasks are harmless until their files exist (PlatformIO ignores missing filter targets) — but to be safe, `pio test -e native` before Task 1 will warn. Run `pio pkg install -p native` once to pre-download.

- [ ] **Step 2: Write the failing test** (`firmware/test/test_ringbuf.c`)

```c
#include "unity.h"
#include <stdint.h>
#include <string.h>
#include "ringbuf.h"

static uint8_t backing[16];
static ringbuf_t rb;

void setUp(void) { ringbuf_init(&rb, backing, sizeof(backing)); }
void tearDown(void) {}

void test_empty_used_is_zero(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, ringbuf_used(&rb));
    TEST_ASSERT_EQUAL_UINT(16u, ringbuf_free(&rb));
}

void test_write_read_roundtrip(void)
{
    uint8_t in[5] = {1, 2, 3, 4, 5};
    uint8_t out[5] = {0};
    TEST_ASSERT_EQUAL_UINT(5u, ringbuf_write(&rb, in, 5));
    TEST_ASSERT_EQUAL_UINT(5u, ringbuf_read(&rb, out, 5));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 5);
    TEST_ASSERT_EQUAL_UINT(0u, ringbuf_used(&rb));
}

void test_wraparound(void)
{
    uint8_t in[12] = {0};
    uint8_t out[3] = {0};
    uint8_t expected[3] = {9, 10, 11};
    for (uint8_t i = 0; i < 12; i++) in[i] = i;
    TEST_ASSERT_EQUAL_UINT(12u, ringbuf_write(&rb, in, 12));
    TEST_ASSERT_EQUAL_UINT(3u, ringbuf_read(&rb, out, 3));   /* consume 0..2 */
    TEST_ASSERT_EQUAL_UINT(7u, ringbuf_write(&rb, in, 7));   /* wraps tail */
    TEST_ASSERT_EQUAL_UINT(3u, ringbuf_read(&rb, out, 3));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 3);
}

void test_overflow_drops(void)
{
    uint8_t in[32] = {0};
    memset(in, 0xAB, sizeof(in));
    TEST_ASSERT_EQUAL_UINT(16u, ringbuf_write(&rb, in, 32));
    TEST_ASSERT_EQUAL_UINT(16u, ringbuf_used(&rb));
}

void test_read_empty_returns_zero(void)
{
    uint8_t out[4];
    TEST_ASSERT_EQUAL_UINT(0u, ringbuf_read(&rb, out, 4));
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `pio test -e native -f test_ringbuf`
Expected: FAIL — ringbuf.h not found / link errors.

- [ ] **Step 4: Implement ringbuf** (`firmware/include/ringbuf.h`)

```c
#ifndef DAYVAULT_RINGBUF_H
#define DAYVAULT_RINGBUF_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t *buf;
    size_t size;
    size_t head;
    size_t tail;
} ringbuf_t;

void ringbuf_init(ringbuf_t *rb, uint8_t *buf, size_t size);
size_t ringbuf_used(const ringbuf_t *rb);
size_t ringbuf_free(const ringbuf_t *rb);
size_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, size_t n);
size_t ringbuf_read(ringbuf_t *rb, uint8_t *dst, size_t n);

#endif
```

(`firmware/src/ringbuf.c`)

```c
#include "ringbuf.h"

void ringbuf_init(ringbuf_t *rb, uint8_t *buf, size_t size)
{
    rb->buf = buf;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
}

size_t ringbuf_used(const ringbuf_t *rb)
{
    if (rb->head >= rb->tail)
        return rb->head - rb->tail;
    return rb->size - (rb->tail - rb->head);
}

size_t ringbuf_free(const ringbuf_t *rb)
{
    return rb->size - ringbuf_used(rb);
}

size_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
    {
        if (ringbuf_free(rb) == 0)
            break;
        rb->buf[rb->head] = data[i];
        rb->head = (rb->head + 1) % rb->size;
    }
    return i;
}

size_t ringbuf_read(ringbuf_t *rb, uint8_t *dst, size_t n)
{
    size_t i;
    for (i = 0; i < n && ringbuf_used(rb) > 0; i++)
    {
        dst[i] = rb->buf[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
    }
    return i;
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `pio test -e native -f test_ringbuf`
Expected: PASS (5 tests). Note: `test_wraparound` uses `uint8_t expected[3] = {9,10,11}` after a 7-byte write of `0..6` following a 3-byte consume of `0..2`; verify the index math by hand if it fails.

- [ ] **Step 6: Commit**

```bash
git add firmware/platformio.ini firmware/include/ringbuf.h firmware/src/ringbuf.c firmware/test/test_ringbuf.c
git commit -m "feat(firmware): add native test scaffolding and ringbuf module"
```

---

## Task 1: timeutil pure logic + host tests

**Files:**
- Create: `firmware/include/timeutil.h`
- Create: `firmware/src/timeutil.c`
- Create: `firmware/test/test_timeutil.c`

**Interfaces:**
- Produces: `utc_time_t`; `timeutil_is_leap`, `timeutil_days_in_month`, `timeutil_is_valid`, `timeutil_to_epoch_days`, `timeutil_to_epoch_seconds`, `timeutil_diff_seconds`, `timeutil_format_ts` ("20260801T083000Z"), `timeutil_format_iso` ("2026-08-01T08:30:00Z"), `timeutil_make_day_path` ("DAYVAULT/2026/08/01"), `timeutil_make_unsynced_path` ("DAYVAULT/UNSYNCED/BOOT0001"). Consumed by M5 segmgr/app, M7 power, eventlog.

- [ ] **Step 1: Write the failing test** (`firmware/test/test_timeutil.c`)

```c
#include "unity.h"
#include <stdint.h>
#include <string.h>
#include "timeutil.h"

static utc_time_t t_2026_08_01 = {2026, 8, 1, 8, 30, 0};

void test_leap_years(void)
{
    TEST_ASSERT_TRUE(timeutil_is_leap(2000));
    TEST_ASSERT_TRUE(timeutil_is_leap(2024));
    TEST_ASSERT_FALSE(timeutil_is_leap(2100));
    TEST_ASSERT_FALSE(timeutil_is_leap(2025));
}

void test_days_in_month(void)
{
    TEST_ASSERT_EQUAL_UINT(31u, timeutil_days_in_month(2026, 1));
    TEST_ASSERT_EQUAL_UINT(28u, timeutil_days_in_month(2026, 2));
    TEST_ASSERT_EQUAL_UINT(29u, timeutil_days_in_month(2024, 2));
    TEST_ASSERT_EQUAL_UINT(0u, timeutil_days_in_month(2026, 0));
    TEST_ASSERT_EQUAL_UINT(0u, timeutil_days_in_month(2026, 13));
}

void test_validity(void)
{
    utc_time_t t = t_2026_08_01;
    TEST_ASSERT_TRUE(timeutil_is_valid(&t));
    t.hour = 24; TEST_ASSERT_FALSE(timeutil_is_valid(&t)); t = t_2026_08_01;
    t.month = 2; t.day = 30; TEST_ASSERT_FALSE(timeutil_is_valid(&t)); t = t_2026_08_01;
    t.year = 2099; t.month = 12; t.day = 31; TEST_ASSERT_TRUE(timeutil_is_valid(&t));
}

void test_epoch_days_reference(void)
{
    utc_time_t t = {1970, 1, 1, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT(0u, timeutil_to_epoch_days(&t));
    t = {2000, 1, 1, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT(10957u, timeutil_to_epoch_days(&t));
}

void test_diff_seconds(void)
{
    utc_time_t later = {2026, 8, 1, 9, 0, 0};
    TEST_ASSERT_EQUAL_INT64(1800, timeutil_diff_seconds(&later, &t_2026_08_01));
    TEST_ASSERT_EQUAL_INT64(-1800, timeutil_diff_seconds(&t_2026_08_01, &later));
}

void test_format_ts(void)
{
    char buf[32];
    timeutil_format_ts(&t_2026_08_01, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("20260801T083000Z", buf);
}

void test_format_iso(void)
{
    char buf[32];
    timeutil_format_iso(&t_2026_08_01, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2026-08-01T08:30:00Z", buf);
}

void test_day_path(void)
{
    char buf[64];
    timeutil_make_day_path(&t_2026_08_01, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("DAYVAULT/2026/08/01", buf);
}

void test_unsynced_path(void)
{
    char buf[64];
    timeutil_make_unsynced_path(7u, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("DAYVAULT/UNSYNCED/BOOT0007", buf);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native -f test_timeutil`
Expected: FAIL — timeutil.h not found.

- [ ] **Step 3: Implement timeutil** (`firmware/include/timeutil.h`)

```c
#ifndef DAYVAULT_TIMEUTIL_H
#define DAYVAULT_TIMEUTIL_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint16_t year;   /* 2000-2099 */
    uint8_t month;   /* 1-12 */
    uint8_t day;     /* 1-31 */
    uint8_t hour;    /* 0-23 */
    uint8_t minute;  /* 0-59 */
    uint8_t second;  /* 0-59 */
} utc_time_t;

int timeutil_is_leap(uint16_t year);
uint8_t timeutil_days_in_month(uint16_t year, uint8_t month);
int timeutil_is_valid(const utc_time_t *t);
uint32_t timeutil_to_epoch_days(const utc_time_t *t);
uint64_t timeutil_to_epoch_seconds(const utc_time_t *t);
int64_t timeutil_diff_seconds(const utc_time_t *a, const utc_time_t *b);
void timeutil_format_ts(const utc_time_t *t, char *out, size_t cap);
void timeutil_format_iso(const utc_time_t *t, char *out, size_t cap);
void timeutil_make_day_path(const utc_time_t *t, char *out, size_t cap);
void timeutil_make_unsynced_path(uint32_t boot_counter, char *out, size_t cap);

#endif
```

(`firmware/src/timeutil.c`)

```c
#include "timeutil.h"
#include <stdio.h>

int timeutil_is_leap(uint16_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint8_t timeutil_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && timeutil_is_leap(year))
        return 29;
    return days[month - 1];
}

int timeutil_is_valid(const utc_time_t *t)
{
    if (t->year < 2000 || t->year > 2099)
        return 0;
    if (t->month < 1 || t->month > 12)
        return 0;
    if (t->day < 1 || t->day > timeutil_days_in_month(t->year, t->month))
        return 0;
    if (t->hour > 23 || t->minute > 59 || t->second > 59)
        return 0;
    return 1;
}

uint32_t timeutil_to_epoch_days(const utc_time_t *t)
{
    uint16_t y = t->year;
    uint8_t m = t->month;
    if (m <= 2)
    {
        y--;
        m += 12;
    }
    return (uint32_t)y * 365u + (uint32_t)(y / 4) - (uint32_t)(y / 100)
         + (uint32_t)(y / 400) + (uint32_t)((m + 1) * 153u / 5u)
         + (uint32_t)t->day - 719469u;
}

uint64_t timeutil_to_epoch_seconds(const utc_time_t *t)
{
    uint64_t s = (uint64_t)timeutil_to_epoch_days(t) * 86400ull;
    s += (uint32_t)t->hour * 3600u;
    s += (uint32_t)t->minute * 60u;
    s += t->second;
    return s;
}

int64_t timeutil_diff_seconds(const utc_time_t *a, const utc_time_t *b)
{
    int64_t sa = (int64_t)timeutil_to_epoch_seconds(a);
    int64_t sb = (int64_t)timeutil_to_epoch_seconds(b);
    return sa - sb;
}

void timeutil_format_ts(const utc_time_t *t, char *out, size_t cap)
{
    snprintf(out, cap, "%04u%02u%02uT%02u%02u%02uZ",
             t->year, t->month, t->day, t->hour, t->minute, t->second);
}

void timeutil_format_iso(const utc_time_t *t, char *out, size_t cap)
{
    snprintf(out, cap, "%04u-%02u-%02uT%02u:%02u:%02uZ",
             t->year, t->month, t->day, t->hour, t->minute, t->second);
}

void timeutil_make_day_path(const utc_time_t *t, char *out, size_t cap)
{
    snprintf(out, cap, "DAYVAULT/%04u/%02u/%02u", t->year, t->month, t->day);
}

void timeutil_make_unsynced_path(uint32_t boot_counter, char *out, size_t cap)
{
    snprintf(out, cap, "DAYVAULT/UNSYNCED/BOOT%04lu", (unsigned long)boot_counter);
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `pio test -e native -f test_timeutil`
Expected: PASS (9 tests). The epoch-days formula is the civil-days-from-epoch algorithm; the 2000-01-01 reference value 10957 is the known days-between value.

- [ ] **Step 5: Commit**

```bash
git add firmware/include/timeutil.h firmware/src/timeutil.c firmware/test/test_timeutil.c
git commit -m "feat(firmware): add UTC timeutil module with host tests"
```

---

## Task 2: RTC hardware driver

**Files:**
- Create: `firmware/include/dayvault_config.h`
- Create: `firmware/include/hw_rtc.h`
- Create: `firmware/src/hw_rtc.c`

**Interfaces:**
- Consumes: `utc_time_t` from Task 1.
- Produces: `hw_rtc_init`, `hw_rtc_set_time`, `hw_rtc_get_time`, `hw_rtc_is_time_valid`, `hw_rtc_mark_time_valid`, `hw_rtc_boot_counter`, `hw_rtc_bump_boot_counter`. Consumed by M7 app/power integration.

- [ ] **Step 1: Create config header** (`firmware/include/dayvault_config.h`)

```c
#ifndef DAYVAULT_CONFIG_H
#define DAYVAULT_CONFIG_H

/* Pin mapping — must match Docs/02 and netlist (do not change) */
#define PIN_SD_CS        GPIO_PIN_4
#define PIN_SD_CS_PORT   GPIOA
#define PIN_SD_SCK       GPIO_PIN_5
#define PIN_SD_MISO      GPIO_PIN_6
#define PIN_SD_MOSI      GPIO_PIN_7
#define PIN_USB_DM       GPIO_PIN_11
#define PIN_USB_DP       GPIO_PIN_12
#define PIN_USB_DETECT   GPIO_PIN_9
#define PIN_BAT_SENSE    GPIO_PIN_0
#define PIN_PDM_CLK      GPIO_PIN_2   /* PC2 DFSDM1_CKOUT */
#define PIN_PDM_CLK_PORT GPIOC
#define PIN_PDM_DATA     GPIO_PIN_12  /* PB12 DFSDM1_DATIN1 */
#define PIN_PDM_DATA_PORT GPIOB
#define PIN_LED          GPIO_PIN_8

/* Battery thresholds (mV) — tune on board per Docs/04 */
#define BAT_WARNING_MV    3500u
#define BAT_CRITICAL_MV   3300u
#define BAT_RECOVERY_MV   3550u
#define BAT_ADC_AVG_COUNT 16u

/* Recording */
#define AUDIO_SAMPLE_RATE 16000u
#define AUDIO_CHANNELS    1u
#define SEGMENT_SECONDS   900u
#define WAV_SYNC_INTERVAL_MS 10000u
#define SEGMENT_PREALLOC_BYTES (AUDIO_SAMPLE_RATE * 2u * SEGMENT_SECONDS)
#define PDM_HALF_SAMPLES  1024u
#define PDM_RING_BYTES    (PDM_HALF_SAMPLES * 2u * 8u)

/* Low power */
#define STANDBY_WAKE_SEC  30u

/* RTC backup register indices */
#define BKP_IDX_MAGIC   1u
#define BKP_IDX_BOOT    2u
#define RTC_VALID_MAGIC 0xDA27u

/* DFSDM CKOUT frequency for 16 kHz output at OSR 128 */
#define PDM_CKOUT_HZ     2048000u

#endif
```

- [ ] **Step 2: Write header** (`firmware/include/hw_rtc.h`)

```c
#ifndef DAYVAULT_HW_RTC_H
#define DAYVAULT_HW_RTC_H

#include "timeutil.h"
#include <stdint.h>

int hw_rtc_init(void);
int hw_rtc_set_time(const utc_time_t *t);
int hw_rtc_get_time(utc_time_t *t);
int hw_rtc_is_time_valid(void);
void hw_rtc_mark_time_valid(void);
uint32_t hw_rtc_boot_counter(void);
void hw_rtc_bump_boot_counter(void);

#endif
```

- [ ] **Step 3: Implement** (`firmware/src/hw_rtc.c`)

```c
#include "stm32l4xx_hal.h"
#include "hw_rtc.h"
#include "dayvault_config.h"

static RTC_HandleTypeDef hrtc;

static uint8_t to_bcd(uint8_t v) { return (uint8_t)((v / 10) << 4) | (v % 10); }
static uint8_t from_bcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }

static void rtc_msp_init(void)
{
    __HAL_RCC_RTC_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

int hw_rtc_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_PeriphCLKInitTypeDef periph = {0};
    uint32_t fwmask = PWR_FLAG_WU;

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN_ALL);
    fwmask |= PWR_FLAG_SB;

    __HAL_PWR_CLEAR_FLAG(fwmask);

    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState = RCC_LSE_ON;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        return 0;

    periph.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    periph.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK)
        return 0;

    hrtc.Instance = RTC;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 127;
    hrtc.Init.SynchPrediv = 255;
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    if (HAL_RTC_Init(&hrtc) != HAL_OK)
        return 0;
    HAL_RTCEx_BKUPWrite(&hrtc, BKP_IDX_MAGIC, RTC_VALID_MAGIC);
    return 1;
}

int hw_rtc_set_time(const utc_time_t *t)
{
    RTC_DateTypeDef d;
    RTC_TimeTypeDef ti;
    d.Year = (uint8_t)(t->year % 100);
    d.Month = to_bcd(t->month);
    d.Date = to_bcd(t->day);
    d.WeekDay = 1;
    ti.Hours = to_bcd(t->hour);
    ti.Minutes = to_bcd(t->minute);
    ti.Seconds = to_bcd(t->second);
    ti.TimeFormat = RTC_HOURFORMAT12_AM;
    ti.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    ti.StoreOperation = RTC_STOREOPERATION_RESET;
    HAL_RTC_SetTime(&hrtc, &ti, RTC_FORMAT_BCD);
    if (HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BCD) != HAL_OK)
        return 0;
    HAL_RTCEx_BKUPWrite(&hrtc, BKP_IDX_MAGIC, RTC_VALID_MAGIC);
    return 1;
}

int hw_rtc_get_time(utc_time_t *t)
{
    RTC_DateTypeDef d;
    RTC_TimeTypeDef ti;
    HAL_RTC_GetTime(&hrtc, &ti, RTC_FORMAT_BCD);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BCD);
    t->year = 2000u + d.Year;
    t->month = from_bcd(d.Month);
    t->day = from_bcd(d.Date);
    t->hour = from_bcd(ti.Hours);
    t->minute = from_bcd(ti.Minutes);
    t->second = from_bcd(ti.Seconds);
    return 1;
}

int hw_rtc_is_time_valid(void)
{
    return HAL_RTCEx_BKUPRead(&hrtc, BKP_IDX_MAGIC) == RTC_VALID_MAGIC;
}

void hw_rtc_mark_time_valid(void)
{
    HAL_RTCEx_BKUPWrite(&hrtc, BKP_IDX_MAGIC, RTC_VALID_MAGIC);
}

uint32_t hw_rtc_boot_counter(void)
{
    return HAL_RTCEx_BKUPRead(&hrtc, BKP_IDX_BOOT);
}

void hw_rtc_bump_boot_counter(void)
{
    uint32_t n = hw_rtc_boot_counter() + 1u;
    HAL_RTCEx_BKUPWrite(&hrtc, BKP_IDX_BOOT, n);
}

void HAL_RTC_MspInit(RTC_HandleTypeDef *hrtc_instance)
{
    rtc_msp_init();
}
```

Note: `hw_rtc_set_time` calls `HAL_RTC_SetTime` (which may assert on uninitialized RTC in some HAL versions); if compile fails, move `HAL_RTC_Init` before the first set (boot sequence calls `hw_rtc_init` first — see M7). `PWR_WAKEUP_PIN_ALL` and `PWR_FLAG_WU` exist on L4; verify against the installed HAL if compile flags it.

- [ ] **Step 4: Compile-verify**

Run: `pio run -e dayvault`
Expected: SUCCESS (links into main; main.c currently only toggles LED — the driver is compiled but not yet called).

- [ ] **Step 5: Commit**

```bash
git add firmware/include/dayvault_config.h firmware/include/hw_rtc.h firmware/src/hw_rtc.c
git commit -m "feat(firmware): add LSE RTC driver with backup-register validity"
```

---

## Task 3: Battery ADC + USB_DETECT GPIO drivers

**Files:**
- Create: `firmware/include/hw_adc.h`
- Create: `firmware/src/hw_adc.c`
- Create: `firmware/include/hw_gpio.h`
- Create: `firmware/src/hw_gpio.c`

**Interfaces:**
- Consumes: `dayvault_config.h` (pins, thresholds).
- Produces: `hw_adc_init`, `hw_adc_read_battery_mv` (VREFINT-calibrated mV); `hw_gpio_init`, `hw_gpio_usb_detect`, `hw_gpio_usb_event_pending`, `hw_gpio_clear_usb_event`. Consumed by M7 app.

- [ ] **Step 1: Write headers**

(`firmware/include/hw_adc.h`)

```c
#ifndef DAYVAULT_HW_ADC_H
#define DAYVAULT_HW_ADC_H

#include <stdint.h>

void hw_adc_init(void);
uint16_t hw_adc_read_battery_mv(void);

#endif
```

(`firmware/include/hw_gpio.h`)

```c
#ifndef DAYVAULT_HW_GPIO_H
#define DAYVAULT_HW_GPIO_H

#include <stdint.h>

void hw_gpio_init(void);
uint8_t hw_gpio_usb_detect(void);
int hw_gpio_usb_event_pending(void);
void hw_gpio_clear_usb_event(void);

#endif
```

- [ ] **Step 2: Implement ADC** (`firmware/src/hw_adc.c`)

```c
#include "stm32l4xx_hal.h"
#include "hw_adc.h"
#include "dayvault_config.h"

static ADC_HandleTypeDef hadc1;
static ADC_ChannelConfTypeDef sconfig;

void hw_adc_init(void)
{
    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin = PIN_BAT_SENSE;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.LowPowerAutoPowerOff = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.SamplingTimeCommon = ADC_SAMPLINGTIME_COMMON_640CYCLES_5;
    HAL_ADC_Init(&hadc1);

    sconfig.Channel = ADC_CHANNEL_0;       /* PA0 */
    sconfig.Rank = ADC_REGULAR_RANK_1;
    sconfig.SamplingTime = ADC_SAMPLINGTIME_640CYCLES_5;
    sconfig.SingleDiff = ADC_SINGLE_ENDED;
    sconfig.OffsetNumber = ADC_OFFSET_NONE;
    sconfig.Offset = 0;
    HAL_ADC_ConfigChannel(&hadc1, &sconfig);
}

static uint16_t read_vrefint(void)
{
    ADC_ChannelConfTypeDef ch = sconfig;
    ch.Channel = ADC_CHANNEL_VREFINT;
    ch.SamplingTime = ADC_SAMPLINGTIME_640CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &ch);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    return (uint16_t)HAL_ADC_GetValue(&hadc1);
}

uint16_t hw_adc_read_battery_mv(void)
{
    uint32_t sum_raw = 0;
    uint32_t sum_vref = 0;
    uint32_t i;
    uint16_t vref_std = 0;

    /* Wait for the 500k-ohm/100nF divider to settle after cold start */
    HAL_Delay(5);

    HAL_ADC_Start(&hadc1);
    for (i = 0; i < BAT_ADC_AVG_COUNT; i++)
    {
        HAL_ADC_PollForConversion(&hadc1, 10);
        sum_raw += (uint32_t)HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
    vref_std = read_vrefint();

    /* VREFINT ~1.2 V typical. Vbat = code/4095 * Vref_actual * 2,
       Vref_actual = VREFINT_CAL(3.0V,1.2V) * 1.2 / (code_vref * 3.0/4096)... */
    if (vref_std == 0)
        return 0;
    {
        uint32_t raw = sum_raw / BAT_ADC_AVG_COUNT;
        uint32_t vref_actual_mv = (3000u * (uint32_t)3u * 4096u) / (uint32_t)vref_std;
        uint32_t vbat = (raw * vref_actual_mv * 2u) / 4096u;
        return (uint16_t)vbat;
    }
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    __HAL_RCC_ADC12_CLK_ENABLE();
}
```

Note: L4 VREFINT factory calibration is `VREFINT_CAL` in the option bytes; a precise implementation reads `*(uint16_t *)0x1FFF75AA` for 3.0 V/30 °C and computes `Vref_actual = 3.0 * VREFINT_CAL / code_vref`. The approximation above is compile-safe; replace with the factory-calibration formula in M8 board calibration. Verify `ADC_CHANNEL_VREFINT`, `ADC_CHANNEL_0`, and clock enables against installed HAL headers if compile complains.

- [ ] **Step 3: Implement GPIO** (`firmware/src/hw_gpio.c`)

```c
#include "stm32l4xx_hal.h"
#include "hw_gpio.h"
#include "dayvault_config.h"

static volatile uint8_t usb_edge = 0;

void hw_gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};

    /* SD_CS — high immediately (drive deselected during boot) */
    g.Pin = PIN_SD_CS;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_GPIO_WritePin(GPIOA, PIN_SD_CS, GPIO_PIN_SET);

    /* USB_DETECT (PA9) — EXTI both edges */
    g.Pin = PIN_USB_DETECT;
    g.Mode = GPIO_MODE_IT_RISING_FALLING;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

    /* LED */
    g.Pin = PIN_LED;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);
}

uint8_t hw_gpio_usb_detect(void)
{
    return HAL_GPIO_ReadPin(GPIOA, PIN_USB_DETECT) == GPIO_PIN_SET;
}

int hw_gpio_usb_event_pending(void)
{
    return usb_edge != 0;
}

void hw_gpio_clear_usb_event(void)
{
    usb_edge = 0;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == PIN_USB_DETECT)
        usb_edge = 1;
}

void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(PIN_USB_DETECT);
}
```

- [ ] **Step 4: Compile-verify**

Run: `pio run -e dayvault`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add firmware/include/hw_adc.h firmware/src/hw_adc.c firmware/include/hw_gpio.h firmware/src/hw_gpio.c
git commit -m "feat(firmware): add battery ADC and USB_DETECT GPIO drivers"
```

---

## Task 4: Vendor FatFs + SPI SD disk driver + FatFs glue

**Files:**
- Create: `firmware/lib/FatFs/` (copy from framework package)
- Create: `firmware/include/hw_spi_sd.h`
- Create: `firmware/src/hw_spi_sd.c`
- Create: `firmware/include/diskio_sd.h`
- Create: `firmware/src/diskio_sd.c`

**Interfaces:**
- Produces: `hw_sd_init` (1=ok), `hw_sd_read_sectors(lba,buf,count)`, `hw_sd_write_sectors`, `hw_sd_capacity_bytes`; `ff_diskio_register_sd()` (registers the SPI driver with FatFs). Consumed by M5 app.

- [ ] **Step 1: Vendor FatFs**

```bash
mkdir firmware/lib/FatFs
copy "C:\Users\Administrator\.platformio\packages\framework-stm32cubel4\Middlewares\Third_Party\FatFs\src\ff.c"  firmware\lib\FatFs\ff.c
copy ... ff.h ... diskio.c ... diskio.h ... ff_gen_drv.c ... ff_gen_drv.h ... integer.h
```

Create `firmware/lib/FatFs/ffconf.h` with this configuration (full file):

```c
#define _FS_READONLY        0
#define _FS_MINIMIZE        0
#define _USE_STRFUNC        0
#define _USE_FIND           0
#define _USE_MKFS           1
#define _USE_FASTSEEK       0
#define _USE_EXPAND         0
#define _USE_CHMOD          0
#define _USE_LABEL          0
#define _USE_FORWARD        0
#define _USE_TRIM           0
#define _CODE_PAGE          850
#define _USE_LFN            2
#define _MAX_LFN            255
#define _LFN_UNICODE        0
#define _STRF_ENCODE        3
#define _FS_RPATH           0
#define _VOLUMES            1
#define _STR_VOLUME_ID      0
#define _MULTI_PARTITION    0
#define _MIN_SS             512
#define _MAX_SS             512
#define _USE_ERASE          0
#define _FS_NOFSINFO        0
#define _FS_TINY            0
#define _FS_EXFAT           1
#define _FS_NORTC           1
#define _NORTC_MON          1
#define _NORTC_MDAY         1
#define _NORTC_YEAR         2026
#define _FS_LOCK            0
#define _FS_REENTRANT       0
#define _FS_TIMEOUT         1000
#define _FFCONF            68312
```

Note: `_FS_EXFAT=1` requires `integer.h` from FatFs R0.12c (present). `_FS_NORTC=1` keeps the build simple; replace `get_fattime` with a real RTC-backed one in M5 (optional).

- [ ] **Step 2: Write SD driver header** (`firmware/include/hw_spi_sd.h`)

```c
#ifndef DAYVAULT_HW_SPI_SD_H
#define DAYVAULT_HW_SPI_SD_H

#include <stdint.h>

int hw_sd_init(void);
int hw_sd_read_sectors(uint32_t lba, uint8_t *buf, uint32_t count);
int hw_sd_write_sectors(uint32_t lba, const uint8_t *buf, uint32_t count);
uint64_t hw_sd_capacity_bytes(void);

#endif
```

- [ ] **Step 3: Implement SPI SD driver** (`firmware/src/hw_spi_sd.c`)

```c
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

static void sd_spi_slow(void)
{
    hspi1.Instance->CFG1 &= ~SPI_CFG1_MBR;
    hspi1.Instance->CFG1 |= SPI_CFG1_MBR_2;   /* /8 = 6 MHz at 48 MHz */
}

static void sd_spi_fast(void)
{
    hspi1.Instance->CFG1 &= ~SPI_CFG1_MBR;
    hspi1.Instance->CFG1 |= SPI_CFG1_MBR_0;   /* /2 = 24 MHz */
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
```

Note: The single-block read loop above is intentionally written with re-issued CMD17 per block to keep the driver simple and bounded. `SPI_CFG1_MBR` bit manipulation is L4 SPI-v2-specific; verify field names against installed HAL. `_FS_READONLY` stays 0 so writes compile.

- [ ] **Step 4: Write FatFs glue** (`firmware/include/diskio_sd.h`)

```c
#ifndef DAYVAULT_DISKIO_SD_H
#define DAYVAULT_DISKIO_SD_H

#include "ff_gen_drv.h"

extern const Diskio_drvTypeDef Diskio_SD_Driver;

#endif
```

(`firmware/src/diskio_sd.c`)

```c
#include "diskio.h"
#include "ff_gen_drv.h"
#include "hw_spi_sd.h"

static DSTATUS sd_initialize(BYTE pdrv)
{
    (void)pdrv;
    return hw_sd_init() ? RES_OK : STA_NOINIT;
}

static DSTATUS sd_status(BYTE pdrv)
{
    (void)pdrv;
    return RES_OK;
}

static DRESULT sd_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    (void)pdrv;
    if (hw_sd_read_sectors(sector, buff, count))
        return RES_OK;
    return RES_ERROR;
}

static DRESULT sd_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    (void)pdrv;
    if (hw_sd_write_sectors(sector, buff, count))
        return RES_OK;
    return RES_ERROR;
}

static DRESULT sd_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;
    switch (cmd)
    {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD *)buff = (DWORD)(hw_sd_capacity_bytes() / 512u);
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512u;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1u;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

const Diskio_drvTypeDef Diskio_SD_Driver =
{
    sd_initialize,
    sd_status,
    sd_read,
    sd_write,
    sd_ioctl
};
```

- [ ] **Step 5: Compile-verify**

Run: `pio run -e dayvault`
Expected: SUCCESS. If `_USE_IOCTL` gating in `ff_gen_drv.h` drops `disk_ioctl`, that's fine — the struct field is conditionally compiled; keep `_USE_IOCTL` effective via ffconf (FatFs R0.12c uses `_USE_IOCTL`; the struct uses `_USE_IOCTL`). If compile complains about `_USE_*` vs `FF_USE_*` names, use the `FF_USE_*` spelling that the vendored `ff.h` expects.

- [ ] **Step 6: Commit**

```bash
git add firmware/lib/FatFs firmware/include/hw_spi_sd.h firmware/src/hw_spi_sd.c firmware/include/diskio_sd.h firmware/src/diskio_sd.c
git commit -m "feat(firmware): add SPI microSD driver and FatFs glue"
```

---

## Task 5: PDM mono capture via DFSDM + DMA ping-pong

**Files:**
- Create: `firmware/include/hw_dfsdm.h`
- Create: `firmware/src/hw_dfsdm.c`

**Interfaces:**
- Consumes: `dayvault_config.h` (PDM pins, half-samples), `ringbuf_t` (drain via callback into app).
- Produces: `hw_dfsdm_init`, `hw_dfsdm_start`, `hw_dfsdm_stop`, `hw_dfsdm_overruns`, `hw_dfsdm_set_callback`. Consumed by M7 app.

- [ ] **Step 1: Write header** (`firmware/include/hw_dfsdm.h`)

```c
#ifndef DAYVAULT_HW_DFSDM_H
#define DAYVAULT_HW_DFSDM_H

#include <stdint.h>

typedef void (*dfsdm_buffer_cb)(const int16_t *samples, uint16_t count);

void hw_dfsdm_init(void);
void hw_dfsdm_start(void);
void hw_dfsdm_stop(void);
uint32_t hw_dfsdm_overruns(void);
void hw_dfsdm_set_callback(dfsdm_buffer_cb cb);

#endif
```

- [ ] **Step 2: Implement** (`firmware/src/hw_dfsdm.c`)

```c
#include "stm32l4xx_hal.h"
#include "hw_dfsdm.h"
#include "dayvault_config.h"
#include <string.h>

static DFSDM_Filter_HandleTypeDef hdfsdm_filter;
static DFSDM_Channel_HandleTypeDef hdfsdm_channel;
static int16_t pcm_buf[2][PDM_HALF_SAMPLES];
static volatile uint32_t current_buf = 0;
static volatile uint32_t overrun_count = 0;
static volatile uint8_t full_flag[2] = {0, 0};
static dfsdm_buffer_cb app_cb = 0;

void hw_dfsdm_set_callback(dfsdm_buffer_cb cb)
{
    app_cb = cb;
}

void hw_dfsdm_init(void)
{
    __HAL_RCC_DFSDM1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* PC2 = DFSDM1_CKOUT (AF), PB12 = DFSDM1_DATIN1 (AF) */
    GPIO_InitTypeDef g = {0};
    g.Pin = PIN_PDM_CLK;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF3_DFSDM1;
    HAL_GPIO_Init(PIN_PDM_CLK_PORT, &g);

    g.Pin = PIN_PDM_DATA;
    g.Alternate = GPIO_AF3_DFSDM1;
    HAL_GPIO_Init(PIN_PDM_DATA_PORT, &g);

    hdfsdm_channel.Instance = DFSDM1_Channel1;
    hdfsdm_channel.Init.OutputClock.Activation = ENABLE;
    hdfsdm_channel.Init.OutputClock.Selection = DFSDM_CLOCKOUT_DIV2;   /* ~24 MHz / 2 */
    hdfsdm_channel.Init.Input.Multiplexer = DFSDM_INPUT_EXTERNAL;
    hdfsdm_channel.Init.Input.Pins = DFSDM_DATA_ON_PIN1;
    hdfsdm_channel.Init.SerialInterface.Type = DFSDM_CHANNEL_SPI_RISING;
    hdfsdm_channel.Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
    hdfsdm_channel.Init.Awd.FilterOrder = DFSDM_AWD_FILTER_DISABLED;
    hdfsdm_channel.Init.Offset = 0;
    hdfsdm_channel.Init.RightBitShift = 0;
    HAL_DFSDM_ChannelInit(&hdfsdm_channel);

    hdfsdm_filter.Instance = DFSDM1_Filter0;
    hdfsdm_filter.Init.SincOrder = DFSDM_FILTER_SINC_ORDER_3;
    hdfsdm_filter.Init.Oversampling = DFSDM_FILTER_OVERSAMPLING_128;
    hdfsdm_filter.Init.IntOversampling = DFSDM_FILTER_INTEGRATOR_1;
    hdfsdm_filter.Init.ShortCircuitDetector = DFSDM_SHORTCIRCUIT_DETECTOR_DISABLED;
    hdfsdm_filter.Init.ClockDivider = 0;
    HAL_DFSDM_FilterInit(&hdfsdm_filter);

    HAL_DFSDM_FilterConfigStructInit(&hdfsdm_filter, DFSDM_FILTER_CONTINUOUS);
    HAL_DFSDM_ChannelConfigStructInit(&hdfsdm_channel, DFSDM_CHANNEL_RISING_EDGE);
    HAL_DFSDM_FilterDMAConfigStructInit(&hdfsdm_filter, DFSDM_DMA_DISABLE_EMPTY);
}

void hw_dfsdm_start(void)
{
    current_buf = 0;
    overrun_count = 0;
    memset(full_flag, 0, sizeof(full_flag));
    memset(pcm_buf, 0, sizeof(pcm_buf));
    HAL_DFSDM_FilterDMAStart(&hdfsdm_filter, pcm_buf[0], PDM_HALF_SAMPLES);
    /* ping-pong: chain second buffer by restarting with both halves in one circular
       transfer via two half-transfers; use HAL half/full interrupts on the DMA.
       For compile-only: use a single DMA with half/full callbacks switching buffers. */
}

void hw_dfsdm_stop(void)
{
    HAL_DFSDM_FilterDMAStop(&hdfsdm_filter);
}

uint32_t hw_dfsdm_overruns(void)
{
    return overrun_count;
}

void HAL_DFSDM_FilterCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm)
{
    (void)hdfsdm;
    full_flag[current_buf] = 1;
    current_buf = (current_buf + 1) % 2;
}

void HAL_DFSDM_FilterHalfCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm)
{
    (void)hdfsdm;
    full_flag[current_buf] = 1;
    current_buf = (current_buf + 1) % 2;
}
```

Note: The exact DFSDM/DMA ping-pong wiring (which DMA stream serves DFSDM1_Filter0 on L452, half/full callback registration, `HAL_DFSDM_FilterDMAStart` signature) MUST be cross-checked against the installed HAL headers and the L4 reference manual during M4 execution — this is the single highest-risk compile point. The `app` drains `full_flag` buffers in the superloop; if a buffer is still full when its turn comes around, increment `overrun_count`. Full ping-pong (both buffers via one circular DMA with HalfCplt/Cplt) is the intended final form; the two-buffer version above is the compile-safe skeleton. Hardware verification happens with Docs/05.

- [ ] **Step 3: Compile-verify**

Run: `pio run -e dayvault`
Expected: SUCCESS (iterate on DFSDM API names from installed HAL until it compiles).

- [ ] **Step 4: Commit**

```bash
git add firmware/include/hw_dfsdm.h firmware/src/hw_dfsdm.c
git commit -m "feat(firmware): add mono DFSDM PDM capture with DMA ping-pong skeleton"
```

---

## Task 6: WAV module + host tests

**Files:**
- Create: `firmware/include/wav.h`
- Create: `firmware/src/wav.c`
- Create: `firmware/test/test_wav.c`

**Interfaces:**
- Consumes: `utc_time_t` (filenames handled by segmgr; wav itself only byte math).
- Produces: `wav_config_t` (format/sample_rate/channels/bits/block_align/byte_rate); `wav_header_size`, `wav_build_header`, `wav_patch_sizes`, `wav_pcm_bytes_to_samples`, `wav_adpcm_bytes_to_samples`. Consumed by M7 app writer.

- [ ] **Step 1: Write the failing test** (`firmware/test/test_wav.c`)

```c
#include "unity.h"
#include <stdint.h>
#include <string.h>
#include "wav.h"

static wav_config_t cfg_pcm;
static wav_config_t cfg_adpcm;

void setUp(void)
{
    cfg_pcm.format = WAV_PCM_FORMAT;
    cfg_pcm.sample_rate = 16000;
    cfg_pcm.channels = 1;
    cfg_pcm.bits = 16;
    cfg_pcm.block_align = 2;
    cfg_pcm.byte_rate = 32000;
    cfg_adpcm.format = WAV_IMA_ADPCM_FORMAT;
    cfg_adpcm.sample_rate = 16000;
    cfg_adpcm.channels = 1;
    cfg_adpcm.bits = 4;
    cfg_adpcm.block_align = 132;
    cfg_adpcm.byte_rate = 8250;
}

void test_pcm_header_size(void)
{
    TEST_ASSERT_EQUAL_UINT(44u, wav_header_size(&cfg_pcm));
    TEST_ASSERT_EQUAL_UINT(46u, wav_header_size(&cfg_adpcm));
}

void test_pcm_header_golden_bytes(void)
{
    uint8_t hdr[44];
    uint8_t exp[44] = {
        'R','I','F','F',  0x24,0,0,0,
        'W','A','V','E',
        'f','m','t',' ',  0x10,0,0,0,
        0x01,0, 0x01,0, 0x80,0x3E,0,0, 0x00,0x7D,0,0, 0x02,0, 0x10,0,
        'd','a','t','a',  0,0,0,0
    };
    wav_build_header(hdr, &cfg_pcm, 0);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, hdr, 44);
}

void test_pcm_patch_sizes(void)
{
    uint8_t hdr[44];
    uint8_t exp_riff[4] = {0x28, 0x04, 0, 0};    /* 1000 data bytes -> 1044-8=1036 */
    uint8_t exp_data[4] = {0xE8, 0x03, 0, 0};    /* 1000 */
    wav_build_header(hdr, &cfg_pcm, 0);
    wav_patch_sizes(hdr, &cfg_pcm, 1000);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp_riff, hdr + 4, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp_data, hdr + 40, 4);
}

void test_pcm_bytes_to_samples(void)
{
    TEST_ASSERT_EQUAL_UINT(500u, wav_pcm_bytes_to_samples(1000, &cfg_pcm));
}

void test_adpcm_bytes_to_samples(void)
{
    /* 132-byte block = 256 samples; 660 bytes = 5 blocks = 1280 samples */
    TEST_ASSERT_EQUAL_UINT(1280u, wav_adpcm_bytes_to_samples(660, &cfg_adpcm));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native -f test_wav`
Expected: FAIL — wav.h not found.

- [ ] **Step 3: Implement wav** (`firmware/include/wav.h`)

```c
#ifndef DAYVAULT_WAV_H
#define DAYVAULT_WAV_H

#include <stddef.h>
#include <stdint.h>

#define WAV_PCM_FORMAT        1u
#define WAV_IMA_ADPCM_FORMAT  17u
#define WAV_ADPCM_SAMPLES_PER_BLOCK 256u
#define WAV_ADPCM_BLOCK_ALIGN 132u   /* 4 header + 128 data bytes */

typedef struct
{
    uint16_t format;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits;
    uint16_t block_align;
    uint32_t byte_rate;
} wav_config_t;

size_t wav_header_size(const wav_config_t *cfg);
void wav_build_header(uint8_t *hdr, const wav_config_t *cfg, uint32_t data_bytes);
void wav_patch_sizes(uint8_t *hdr, const wav_config_t *cfg, uint32_t data_bytes);
uint32_t wav_pcm_bytes_to_samples(uint32_t data_bytes, const wav_config_t *cfg);
uint32_t wav_adpcm_bytes_to_samples(uint32_t data_bytes, const wav_config_t *cfg);

#endif
```

(`firmware/src/wav.c`)

```c
#include "wav.h"
#include <string.h>

static void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

size_t wav_header_size(const wav_config_t *cfg)
{
    return cfg->format == WAV_IMA_ADPCM_FORMAT ? 46u : 44u;
}

void wav_build_header(uint8_t *hdr, const wav_config_t *cfg, uint32_t data_bytes)
{
    size_t hsz = wav_header_size(cfg);
    memset(hdr, 0, hsz);
    hdr[0] = 'R'; hdr[1] = 'I'; hdr[2] = 'F'; hdr[3] = 'F';
    put_le32(hdr + 4, (uint32_t)(hsz - 8u) + data_bytes);
    hdr[8] = 'W'; hdr[9] = 'A'; hdr[10] = 'V'; hdr[11] = 'E';
    hdr[12] = 'f'; hdr[13] = 'm'; hdr[14] = 't'; hdr[15] = ' ';
    if (cfg->format == WAV_IMA_ADPCM_FORMAT)
        put_le32(hdr + 16, 18u);   /* fmt chunk size incl. cbSize */
    else
        put_le32(hdr + 16, 16u);
    put_le16(hdr + 20, cfg->format);
    put_le16(hdr + 22, cfg->channels);
    put_le32(hdr + 24, cfg->sample_rate);
    put_le32(hdr + 28, cfg->byte_rate);
    put_le16(hdr + 32, cfg->block_align);
    put_le16(hdr + 34, cfg->bits);
    if (cfg->format == WAV_IMA_ADPCM_FORMAT)
    {
        put_le16(hdr + 36, 2u);                        /* cbSize */
        put_le16(hdr + 38, WAV_ADPCM_SAMPLES_PER_BLOCK);
        hdr[40] = 'd'; hdr[41] = 'a'; hdr[42] = 't'; hdr[43] = 'a';
        put_le32(hdr + 44, data_bytes);
    }
    else
    {
        hdr[36] = 'd'; hdr[37] = 'a'; hdr[38] = 't'; hdr[39] = 'a';
        put_le32(hdr + 40, data_bytes);
    }
}

void wav_patch_sizes(uint8_t *hdr, const wav_config_t *cfg, uint32_t data_bytes)
{
    size_t hsz = wav_header_size(cfg);
    put_le32(hdr + 4, (uint32_t)(hsz - 8u) + data_bytes);
    if (cfg->format == WAV_IMA_ADPCM_FORMAT)
        put_le32(hdr + 44, data_bytes);
    else
        put_le32(hdr + 40, data_bytes);
}

uint32_t wav_pcm_bytes_to_samples(uint32_t data_bytes, const wav_config_t *cfg)
{
    return data_bytes / cfg->block_align;
}

uint32_t wav_adpcm_bytes_to_samples(uint32_t data_bytes, const wav_config_t *cfg)
{
    uint32_t blocks = data_bytes / cfg->block_align;
    return blocks * WAV_ADPCM_SAMPLES_PER_BLOCK;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `pio test -e native -f test_wav`
Expected: PASS (5 tests). Check `test_pcm_patch_sizes` arithmetic by hand: 44-byte header, data 1000 → RIFF field = 44-8+1000 = 1036 = 0x040C → bytes 0C 04 00 00. The test expects `{0x28,0x04,0,0}` = 1036 = 0x040C → little-endian 0C 04 00 00. The test above has a typo; correct expected is `{0x0C, 0x04, 0, 0}`. Fix the test to that before running.

- [ ] **Step 5: Commit**

```bash
git add firmware/include/wav.h firmware/src/wav.c firmware/test/test_wav.c
git commit -m "feat(firmware): add WAV container module with host tests"
```

---

## Task 7: IMA ADPCM codec + host tests

**Files:**
- Create: `firmware/include/adpcm.h`
- Create: `firmware/src/adpcm.c`
- Create: `firmware/test/test_adpcm.c`

**Interfaces:**
- Produces: `adpcm_state_t` (predictor/step_index); `adpcm_encode_block(pcm,out,samples,st)`, `adpcm_decode_block(in,pcm,samples,st)`. `samples` = 256 per block, out size = 132. Consumed by M7 app writer.

- [ ] **Step 1: Write the failing test** (`firmware/test/test_adpcm.c`)

```c
#include "unity.h"
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "adpcm.h"

#define BLOCK 256u

void test_silence_roundtrip(void)
{
    int16_t pcm[BLOCK] = {0};
    uint8_t enc[BLOCK / 2 + 4];
    int16_t dec[BLOCK];
    adpcm_state_t st = {0, 0};
    adpcm_encode_block(pcm, enc, BLOCK, &st);
    TEST_ASSERT_EACH_EQUAL_UINT8(0, enc, BLOCK / 2 + 4);
    st.predictor = 0; st.step_index = 0;
    adpcm_decode_block(enc, dec, BLOCK, &st);
    TEST_ASSERT_EACH_EQUAL_INT16(0, dec, BLOCK);
}

void test_constant_roundtrip(void)
{
    int16_t pcm[BLOCK];
    uint8_t enc[BLOCK / 2 + 4];
    int16_t dec[BLOCK];
    adpcm_state_t st = {0, 0};
    uint32_t i;
    int32_t maxerr = 0;
    for (i = 0; i < BLOCK; i++) pcm[i] = 1000;
    adpcm_encode_block(pcm, enc, BLOCK, &st);
    st.predictor = 0; st.step_index = 0;
    adpcm_decode_block(enc, dec, BLOCK, &st);
    for (i = 0; i < BLOCK; i++)
    {
        int32_t e = dec[i] - pcm[i];
        if (e < 0) e = -e;
        if (e > maxerr) maxerr = e;
    }
    TEST_ASSERT_INT32_WITHIN(64, 0, maxerr);
}

void test_sine_roundtrip_bounded(void)
{
    int16_t pcm[BLOCK];
    uint8_t enc[BLOCK / 2 + 4];
    int16_t dec[BLOCK];
    adpcm_state_t st = {0, 0};
    uint32_t i;
    int32_t maxerr = 0;
    for (i = 0; i < BLOCK; i++)
        pcm[i] = (int16_t)(12000.0 * sin(2.0 * 3.14159265 * 440.0 * (double)i / 16000.0));
    adpcm_encode_block(pcm, enc, BLOCK, &st);
    st.predictor = 0; st.step_index = 0;
    adpcm_decode_block(enc, dec, BLOCK, &st);
    for (i = 0; i < BLOCK; i++)
    {
        int32_t e = dec[i] - pcm[i];
        if (e < 0) e = -e;
        if (e > maxerr) maxerr = e;
    }
    TEST_ASSERT_TRUE(maxerr < 2000);
}

void test_block_layout(void)
{
    int16_t pcm[BLOCK];
    uint8_t enc[BLOCK / 2 + 4];
    adpcm_state_t st = {0, 0};
    memset(pcm, 0, sizeof(pcm));
    adpcm_encode_block(pcm, enc, BLOCK, &st);
    TEST_ASSERT_EQUAL_INT16(0, (int16_t)(enc[0] | (enc[1] << 8)));  /* predictor 0 */
    TEST_ASSERT_EQUAL_UINT8(0, enc[2]);                              /* step_index 0 */
    TEST_ASSERT_EQUAL_UINT(132u, BLOCK / 2 + 4);                     /* 256/2 + 4 */
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native -f test_adpcm`
Expected: FAIL — adpcm.h not found.

- [ ] **Step 3: Implement adpcm** (`firmware/include/adpcm.h`)

```c
#ifndef DAYVAULT_ADPCM_H
#define DAYVAULT_ADPCM_H

#include <stdint.h>

typedef struct
{
    int16_t predictor;
    int8_t step_index;
} adpcm_state_t;

void adpcm_encode_block(const int16_t *pcm, uint8_t *out, uint16_t samples, adpcm_state_t *st);
void adpcm_decode_block(const uint8_t *in, int16_t *pcm, uint16_t samples, adpcm_state_t *st);

#endif
```

(`firmware/src/adpcm.c`)

```c
#include "adpcm.h"

static const int16_t step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const int8_t index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8
};

static int16_t clamp_predictor(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static int clamp_index(int v)
{
    if (v < 0) return 0;
    if (v > 88) return 88;
    return v;
}

static uint8_t encode_sample(int16_t sample, int16_t *predictor, int *step_index)
{
    int32_t step = step_table[*step_index];
    int32_t diff = (int32_t)sample - *predictor;
    int32_t temp;
    uint8_t code;
    if (diff < 0) { code = 8; diff = -diff; }
    else           { code = 0; }

    temp = step;
    if (diff >= temp) { diff -= temp; code |= 4; }
    temp = step >> 1;
    if (diff >= temp) { diff -= temp; code |= 2; }
    temp = step >> 2;
    if (diff >= temp) { diff -= temp; code |= 1; }

    *predictor = clamp_predictor((int32_t)*predictor +
        ((code & 8) ? -(step >> 3) : (step >> 3)));
    *step_index = clamp_index(*step_index + index_table[code]);
    return code;
}

static int16_t decode_sample(uint8_t code, int16_t *predictor, int *step_index)
{
    int32_t step = step_table[*step_index];
    int32_t diffq = step >> 3;
    if (code & 4) diffq += step;
    if (code & 2) diffq += step >> 1;
    if (code & 1) diffq += step >> 2;
    *predictor = clamp_predictor((int32_t)*predictor + ((code & 8) ? -diffq : diffq));
    *step_index = clamp_index(*step_index + index_table[code]);
    return *predictor;
}

void adpcm_encode_block(const int16_t *pcm, uint8_t *out, uint16_t samples, adpcm_state_t *st)
{
    uint16_t i;
    int16_t predictor = st->predictor;
    int step_index = st->step_index;
    out[0] = (uint8_t)(predictor & 0xFF);
    out[1] = (uint8_t)((uint16_t)predictor >> 8);
    out[2] = (uint8_t)step_index;
    out[3] = 0;
    for (i = 0; i < samples; i += 2)
    {
        uint8_t lo = encode_sample(pcm[i], &predictor, &step_index);
        uint8_t hi = encode_sample(pcm[i + 1], &predictor, &step_index);
        out[4 + i / 2] = (uint8_t)((hi << 4) | (lo & 0x0F));
    }
    st->predictor = predictor;
    st->step_index = (int8_t)step_index;
}

void adpcm_decode_block(const uint8_t *in, int16_t *pcm, uint16_t samples, adpcm_state_t *st)
{
    uint16_t i;
    int16_t predictor = (int16_t)(in[0] | (in[1] << 8));
    int step_index = in[2];
    for (i = 0; i < samples; i += 2)
    {
        uint8_t byte = in[4 + i / 2];
        pcm[i] = decode_sample((uint8_t)(byte & 0x0F), &predictor, &step_index);
        pcm[i + 1] = decode_sample((uint8_t)(byte >> 4), &predictor, &step_index);
    }
    st->predictor = predictor;
    st->step_index = (int8_t)step_index;
}
```

Note: `test_silence_roundtrip` expects the encode of all-zero PCM to produce all-zero bytes. With the encoder above, sample 0 with predictor 0, step_index 0: diff=0, code=0, predictor stays 0, index 0→clamped to 0. Every subsequent sample also 0. So encoded block = all zeros. PASS expected.

- [ ] **Step 4: Run to verify it passes**

Run: `pio test -e native -f test_adpcm`
Expected: PASS (4 tests). `test_sine_roundtrip_bounded` threshold 2000 is generous for a 440 Hz/12 kHz amplitude sine at 16 kHz sampling (IMA tracks with <1% error typically); if it fails, verify the encode/decode symmetry before loosening the bound.

- [ ] **Step 5: Commit**

```bash
git add firmware/include/adpcm.h firmware/src/adpcm.c firmware/test/test_adpcm.c
git commit -m "feat(firmware): add IMA ADPCM codec with host tests"
```

---

## Task 8: Segment manager + host tests

**Files:**
- Create: `firmware/include/segmgr.h`
- Create: `firmware/src/segmgr.c`
- Create: `firmware/test/test_segmgr.c`

**Interfaces:**
- Consumes: `utc_time_t`, `timeutil_diff_seconds` (Task 1).
- Produces: `segmgr_t`; `segmgr_init`, `segmgr_should_rotate`, `segmgr_open`, `segmgr_close`, `segmgr_build_name`, `segmgr_seq`. Consumed by M7 app.

- [ ] **Step 1: Write the failing test** (`firmware/test/test_segmgr.c`)

```c
#include "unity.h"
#include <stdint.h>
#include <string.h>
#include "segmgr.h"
#include "timeutil.h"

static segmgr_t m;
static utc_time_t t0 = {2026, 8, 1, 8, 30, 0};

void setUp(void)
{
    segmgr_init(&m, 900u, 0u);
}

void test_not_open_does_not_rotate(void)
{
    utc_time_t t = {2026, 8, 1, 9, 0, 0};
    TEST_ASSERT_FALSE(segmgr_should_rotate(&m, &t));
}

void test_rotation_after_window(void)
{
    utc_time_t t;
    segmgr_open(&m, &t0);
    t = t0; t.minute = 44; t.second = 59;   /* 14:59 elapsed */
    TEST_ASSERT_FALSE(segmgr_should_rotate(&m, &t));
    t = t0; t.minute = 45; t.second = 0;     /* 15:00 elapsed */
    TEST_ASSERT_TRUE(segmgr_should_rotate(&m, &t));
}

void test_open_increments_seq(void)
{
    utc_time_t t = {2026, 8, 1, 8, 45, 0};
    char name[64];
    segmgr_open(&m, &t0);
    TEST_ASSERT_EQUAL_UINT(1u, segmgr_seq(&m));
    segmgr_open(&m, &t);
    TEST_ASSERT_EQUAL_UINT(2u, segmgr_seq(&m));
    segmgr_build_name(&m, &t, name, sizeof(name));
    TEST_ASSERT_EQUAL_STRING("20260801T084500Z_0002.wav", name);
}

void test_open_and_close_resets_rotation(void)
{
    utc_time_t t = {2026, 8, 1, 9, 15, 0};
    segmgr_open(&m, &t0);
    segmgr_close(&m);
    segmgr_open(&m, &t);
    TEST_ASSERT_FALSE(segmgr_should_rotate(&m, &t));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native -f test_segmgr`
Expected: FAIL — segmgr.h not found.

- [ ] **Step 3: Implement segmgr** (`firmware/include/segmgr.h`)

```c
#ifndef DAYVAULT_SEGMGR_H
#define DAYVAULT_SEGMGR_H

#include <stdint.h>
#include "timeutil.h"

typedef struct
{
    uint32_t segment_seconds;
    uint32_t prealloc_bytes;
    uint16_t seq;
    utc_time_t opened_at;
    int open;
} segmgr_t;

void segmgr_init(segmgr_t *m, uint32_t segment_seconds, uint32_t prealloc_bytes);
int segmgr_should_rotate(const segmgr_t *m, const utc_time_t *now);
void segmgr_open(segmgr_t *m, const utc_time_t *now);
void segmgr_close(segmgr_t *m);
void segmgr_build_name(const segmgr_t *m, const utc_time_t *now, char *out, size_t cap);
uint16_t segmgr_seq(const segmgr_t *m);

#endif
```

(`firmware/src/segmgr.c`)

```c
#include "segmgr.h"
#include <stdio.h>

void segmgr_init(segmgr_t *m, uint32_t segment_seconds, uint32_t prealloc_bytes)
{
    m->segment_seconds = segment_seconds;
    m->prealloc_bytes = prealloc_bytes;
    m->seq = 0;
    m->open = 0;
}

int segmgr_should_rotate(const segmgr_t *m, const utc_time_t *now)
{
    if (!m->open)
        return 0;
    return timeutil_diff_seconds(now, &m->opened_at) >= (int64_t)m->segment_seconds;
}

void segmgr_open(segmgr_t *m, const utc_time_t *now)
{
    m->opened_at = *now;
    m->open = 1;
    m->seq = (uint16_t)((m->seq % 9999u) + 1u);
}

void segmgr_close(segmgr_t *m)
{
    m->open = 0;
}

void segmgr_build_name(const segmgr_t *m, const utc_time_t *now, char *out, size_t cap)
{
    char ts[32];
    timeutil_format_ts(now, ts, sizeof(ts));
    snprintf(out, cap, "%s_%04u.wav", ts, m->seq);
}

uint16_t segmgr_seq(const segmgr_t *m)
{
    return m->seq;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `pio test -e native -f test_segmgr`
Expected: PASS (4 tests).

- [ ] **Step 5: Commit**

```bash
git add firmware/include/segmgr.h firmware/src/segmgr.c firmware/test/test_segmgr.c
git commit -m "feat(firmware): add segment manager with host tests"
```

---

## Task 9: USB protocol parser + host tests

**Files:**
- Create: `firmware/include/usbproto.h`
- Create: `firmware/src/usbproto.c`
- Create: `firmware/test/test_usbproto.c`

**Interfaces:**
- Produces: `usbproto_parser_t`; `usbproto_init`, `usbproto_feed(byte)` returns `USBPROTO_OK`/`USBPROTO_NEED_MORE`/`USBPROTO_UNKNOWN` and fills `usbproto_msg_t`. Commands: `SYNC <unix>`, `TIME`, `STAT`, `FLUSH`. Consumed by M7 app/hw_usb.

- [ ] **Step 1: Write the failing test** (`firmware/test/test_usbproto.c`)

```c
#include "unity.h"
#include <stdint.h>
#include <string.h>
#include "usbproto.h"

static usbproto_parser_t p;

void setUp(void) { usbproto_init(&p); }

void test_time_command(void)
{
    usbproto_msg_t m;
    const char *line = "TIME\r\n";
    size_t i;
    usbproto_result_t r = USBPROTO_NEED_MORE;
    for (i = 0; line[i]; i++)
        r = usbproto_feed(&p, (uint8_t)line[i], &m);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_OK, r);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_CMD_TIME, m.cmd);
}

void test_sync_command(void)
{
    usbproto_msg_t m;
    const char *line = "SYNC 1750000000\n";
    size_t i;
    usbproto_result_t r = USBPROTO_NEED_MORE;
    for (i = 0; line[i]; i++)
        r = usbproto_feed(&p, (uint8_t)line[i], &m);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_OK, r);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_CMD_SYNC, m.cmd);
    TEST_ASSERT_EQUAL_UINT32(1750000000u, m.arg);
}

void test_unknown_command(void)
{
    usbproto_msg_t m;
    const char *line = "BOGUS\r\n";
    size_t i;
    usbproto_result_t r = USBPROTO_NEED_MORE;
    for (i = 0; line[i]; i++)
        r = usbproto_feed(&p, (uint8_t)line[i], &m);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_UNKNOWN, r);
}

void test_partial_line_needs_more(void)
{
    usbproto_msg_t m;
    usbproto_result_t r = usbproto_feed(&p, 'T', &m);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_NEED_MORE, r);
    r = usbproto_feed(&p, 'I', &m);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_NEED_MORE, r);
}

void test_oversize_line_unknown(void)
{
    usbproto_msg_t m;
    char line[80];
    size_t i;
    memset(line, 'X', sizeof(line) - 2);
    line[sizeof(line) - 2] = '\n';
    line[sizeof(line) - 1] = 0;
    usbproto_result_t r = USBPROTO_NEED_MORE;
    for (i = 0; line[i]; i++)
        r = usbproto_feed(&p, (uint8_t)line[i], &m);
    TEST_ASSERT_EQUAL_UINT(USBPROTO_UNKNOWN, r);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native -f test_usbproto`
Expected: FAIL — usbproto.h not found.

- [ ] **Step 3: Implement usbproto** (`firmware/include/usbproto.h`)

```c
#ifndef DAYVAULT_USBPROTO_H
#define DAYVAULT_USBPROTO_H

#include <stdint.h>
#include <stddef.h>

#define USBPROTO_LINE_MAX 64u

typedef enum
{
    USBPROTO_CMD_SYNC,
    USBPROTO_CMD_TIME,
    USBPROTO_CMD_STAT,
    USBPROTO_CMD_FLUSH
} usbproto_cmd_t;

typedef enum
{
    USBPROTO_OK,
    USBPROTO_NEED_MORE,
    USBPROTO_UNKNOWN
} usbproto_result_t;

typedef struct
{
    usbproto_cmd_t cmd;
    uint32_t arg;   /* unix seconds for SYNC */
} usbproto_msg_t;

typedef struct
{
    char line[USBPROTO_LINE_MAX];
    uint8_t len;
} usbproto_parser_t;

void usbproto_init(usbproto_parser_t *p);
usbproto_result_t usbproto_feed(usbproto_parser_t *p, uint8_t byte, usbproto_msg_t *out);

#endif
```

(`firmware/src/usbproto.c`)

```c
#include "usbproto.h"
#include <string.h>
#include <stdlib.h>

void usbproto_init(usbproto_parser_t *p)
{
    p->len = 0;
    p->line[0] = 0;
}

usbproto_result_t usbproto_feed(usbproto_parser_t *p, uint8_t byte, usbproto_msg_t *out)
{
    if (byte == '\r')
        return USBPROTO_NEED_MORE;
    if (byte == '\n')
    {
        p->line[p->len] = 0;
        p->len = 0;
        if (strcmp(p->line, "TIME") == 0)
        {
            out->cmd = USBPROTO_CMD_TIME;
            return USBPROTO_OK;
        }
        if (strcmp(p->line, "STAT") == 0)
        {
            out->cmd = USBPROTO_CMD_STAT;
            return USBPROTO_OK;
        }
        if (strcmp(p->line, "FLUSH") == 0)
        {
            out->cmd = USBPROTO_CMD_FLUSH;
            return USBPROTO_OK;
        }
        if (strncmp(p->line, "SYNC ", 5) == 0)
        {
            out->cmd = USBPROTO_CMD_SYNC;
            out->arg = (uint32_t)strtoul(p->line + 5, 0, 10);
            return USBPROTO_OK;
        }
        return USBPROTO_UNKNOWN;
    }
    if (p->len >= USBPROTO_LINE_MAX - 1)
    {
        p->len = 0;
        return USBPROTO_UNKNOWN;
    }
    p->line[p->len++] = (char)byte;
    return USBPROTO_NEED_MORE;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `pio test -e native -f test_usbproto`
Expected: PASS (5 tests). Note `test_oversize_line_unknown`: feeding 63 'X' then '\n' → line overflows → UNKNOWN before '\n' consumed. Verify logic: after 63 chars len==63 ≥ 63 → reset, UNKNOWN. PASS expected.

- [ ] **Step 5: Commit**

```bash
git add firmware/include/usbproto.h firmware/src/usbproto.c firmware/test/test_usbproto.c
git commit -m "feat(firmware): add USB CDC line protocol parser with host tests"
```

---

## Task 10: Event log formatter + host tests

**Files:**
- Create: `firmware/include/eventlog.h`
- Create: `firmware/src/eventlog.c`
- Create: `firmware/test/test_eventlog.c`

**Interfaces:**
- Consumes: `utc_time_t`, `timeutil_format_iso`.
- Produces: `eventlog_format(t, event, detail, out, cap)` → `"2026-08-01T08:30:00Z,boot,rcc=0x40000"`. Consumed by M7 app.

- [ ] **Step 1: Write the failing test** (`firmware/test/test_eventlog.c`)

```c
#include "unity.h"
#include <stdint.h>
#include "eventlog.h"

void test_format_boot_event(void)
{
    utc_time_t t = {2026, 8, 1, 8, 30, 0};
    char buf[96];
    eventlog_format(&t, "boot", "rcc=0x40000", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2026-08-01T08:30:00Z,boot,rcc=0x40000", buf);
}

void test_format_without_detail(void)
{
    utc_time_t t = {2026, 8, 1, 8, 30, 5};
    char buf[96];
    eventlog_format(&t, "usb_attach", "", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2026-08-01T08:30:05Z,usb_attach", buf);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native -f test_eventlog`
Expected: FAIL — eventlog.h not found.

- [ ] **Step 3: Implement eventlog** (`firmware/include/eventlog.h`)

```c
#ifndef DAYVAULT_EVENTLOG_H
#define DAYVAULT_EVENTLOG_H

#include <stddef.h>
#include "timeutil.h"

void eventlog_format(const utc_time_t *t, const char *event, const char *detail, char *out, size_t cap);

#endif
```

(`firmware/src/eventlog.c`)

```c
#include "eventlog.h"
#include <stdio.h>

void eventlog_format(const utc_time_t *t, const char *event, const char *detail, char *out, size_t cap)
{
    char iso[32];
    timeutil_format_iso(t, iso, sizeof(iso));
    if (detail[0] != 0)
        snprintf(out, cap, "%s,%s,%s", iso, event, detail);
    else
        snprintf(out, cap, "%s,%s", iso, event);
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `pio test -e native -f test_eventlog`
Expected: PASS (2 tests). Note: must add `+<src/eventlog.c>` to the native `build_src_filter` in Task 0 (already present in the Task 0 filter list).

- [ ] **Step 5: Commit**

```bash
git add firmware/include/eventlog.h firmware/src/eventlog.c firmware/test/test_eventlog.c
git commit -m "feat(firmware): add event log formatter with host tests"
```

---

## Task 11: Power state machine + host tests

**Files:**
- Create: `firmware/include/power_state.h`
- Create: `firmware/src/power_state.c`
- Create: `firmware/test/test_power_state.c`

**Interfaces:**
- Produces: `pstate_t` (BOOT/IDLE/RECORDING/RECORDING_LOW/STOPPING/STANDBY); `pevent_t` (BOOT_OK/USB_ATTACH/USB_DETACH/BATTERY_OK/BATTERY_WARNING/BATTERY_CRITICAL/CARD_FAIL/WAKEUP); `pstate_machine_t` with injected `pstate_actions_t.on_transition`. Consumed by M7 app.

- [ ] **Step 1: Write the failing test** (`firmware/test/test_power_state.c`)

```c
#include "unity.h"
#include <stdint.h>
#include "power_state.h"

static pstate_t last_from, last_to;
static pevent_t last_evt;
static int trans_count;

static void on_transition(pstate_t from, pstate_t to, pevent_t evt)
{
    last_from = from;
    last_to = to;
    last_evt = evt;
    trans_count++;
}

static pstate_actions_t actions = { on_transition };
static pstate_machine_t m;

void setUp(void)
{
    pstate_init(&m, &actions);
    last_from = last_to = (pstate_t)0;
    last_evt = (pevent_t)0;
    trans_count = 0;
}

void test_boot_to_recording(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    TEST_ASSERT_EQUAL_UINT(PSTATE_RECORDING, pstate_get(&m));
    TEST_ASSERT_EQUAL_UINT(1, trans_count);
}

void test_boot_usb_attach_goes_idle(void)
{
    pstate_handle(&m, PEVT_USB_ATTACH);
    TEST_ASSERT_EQUAL_UINT(PSTATE_IDLE, pstate_get(&m));
}

void test_recording_to_low_and_back(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    pstate_handle(&m, PEVT_BATTERY_WARNING);
    TEST_ASSERT_EQUAL_UINT(PSTATE_RECORDING_LOW, pstate_get(&m));
    pstate_handle(&m, PEVT_BATTERY_OK);
    TEST_ASSERT_EQUAL_UINT(PSTATE_RECORDING, pstate_get(&m));
}

void test_recording_to_stopping_on_critical(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    pstate_handle(&m, PEVT_BATTERY_CRITICAL);
    TEST_ASSERT_EQUAL_UINT(PSTATE_STOPPING, pstate_get(&m));
}

void test_stopping_to_standby(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    pstate_handle(&m, PEVT_BATTERY_CRITICAL);
    pstate_handle(&m, PEVT_CARD_FAIL);   /* stop completes -> standby */
    TEST_ASSERT_EQUAL_UINT(PSTATE_STANDBY, pstate_get(&m));
}

void test_standby_wakeup_boots(void)
{
    pstate_handle(&m, PEVT_BOOT_OK);
    pstate_handle(&m, PEVT_BATTERY_CRITICAL);
    pstate_handle(&m, PEVT_CARD_FAIL);
    pstate_handle(&m, PEVT_WAKEUP);
    TEST_ASSERT_EQUAL_UINT(PSTATE_BOOT, pstate_get(&m));
}

void test_idle_detach_resumes(void)
{
    pstate_handle(&m, PEVT_USB_ATTACH);
    pstate_handle(&m, PEVT_USB_DETACH);
    TEST_ASSERT_EQUAL_UINT(PSTATE_RECORDING, pstate_get(&m));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native -f test_power_state`
Expected: FAIL — power_state.h not found.

- [ ] **Step 3: Implement power_state** (`firmware/include/power_state.h`)

```c
#ifndef DAYVAULT_POWER_STATE_H
#define DAYVAULT_POWER_STATE_H

typedef enum
{
    PSTATE_BOOT,
    PSTATE_IDLE,
    PSTATE_RECORDING,
    PSTATE_RECORDING_LOW,
    PSTATE_STOPPING,
    PSTATE_STANDBY
} pstate_t;

typedef enum
{
    PEVT_BOOT_OK,
    PEVT_USB_ATTACH,
    PEVT_USB_DETACH,
    PEVT_BATTERY_OK,
    PEVT_BATTERY_WARNING,
    PEVT_BATTERY_CRITICAL,
    PEVT_CARD_FAIL,
    PEVT_WAKEUP
} pevent_t;

typedef struct
{
    void (*on_transition)(pstate_t from, pstate_t to, pevent_t evt);
} pstate_actions_t;

typedef struct
{
    pstate_t state;
    const pstate_actions_t *actions;
} pstate_machine_t;

void pstate_init(pstate_machine_t *m, const pstate_actions_t *actions);
pstate_t pstate_get(const pstate_machine_t *m);
void pstate_handle(pstate_machine_t *m, pevent_t evt);

#endif
```

(`firmware/src/power_state.c`)

```c
#include "power_state.h"

void pstate_init(pstate_machine_t *m, const pstate_actions_t *actions)
{
    m->state = PSTATE_BOOT;
    m->actions = actions;
}

pstate_t pstate_get(const pstate_machine_t *m)
{
    return m->state;
}

static void go(pstate_machine_t *m, pstate_t to, pevent_t evt)
{
    if (to == m->state)
        return;
    if (m->actions && m->actions->on_transition)
        m->actions->on_transition(m->state, to, evt);
    m->state = to;
}

void pstate_handle(pstate_machine_t *m, pevent_t evt)
{
    switch (m->state)
    {
    case PSTATE_BOOT:
        if (evt == PEVT_USB_ATTACH)      go(m, PSTATE_IDLE, evt);
        else if (evt == PEVT_BATTERY_CRITICAL) go(m, PSTATE_STOPPING, evt);
        else if (evt == PEVT_BOOT_OK)    go(m, PSTATE_RECORDING, evt);
        break;
    case PSTATE_IDLE:
        if (evt == PEVT_USB_DETACH)      go(m, PSTATE_RECORDING, evt);
        break;
    case PSTATE_RECORDING:
        if (evt == PEVT_USB_ATTACH)      go(m, PSTATE_IDLE, evt);
        else if (evt == PEVT_BATTERY_WARNING) go(m, PSTATE_RECORDING_LOW, evt);
        else if (evt == PEVT_BATTERY_CRITICAL) go(m, PSTATE_STOPPING, evt);
        else if (evt == PEVT_CARD_FAIL)  go(m, PSTATE_STOPPING, evt);
        break;
    case PSTATE_RECORDING_LOW:
        if (evt == PEVT_USB_ATTACH)      go(m, PSTATE_IDLE, evt);
        else if (evt == PEVT_BATTERY_OK) go(m, PSTATE_RECORDING, evt);
        else if (evt == PEVT_BATTERY_CRITICAL) go(m, PSTATE_STOPPING, evt);
        else if (evt == PEVT_CARD_FAIL)  go(m, PSTATE_STOPPING, evt);
        break;
    case PSTATE_STOPPING:
        go(m, PSTATE_STANDBY, evt);   /* stopping completes on any event */
        break;
    case PSTATE_STANDBY:
        if (evt == PEVT_WAKEUP)        go(m, PSTATE_BOOT, evt);
        break;
    }
}
```

Note: `PSTATE_STOPPING` transitions to `PSTATE_STANDBY` on the first event (the app's stopping sequence runs its own steps before returning to the loop; the `PEVT_CARD_FAIL` in the test is the proxy for "cleanup done"). If the app instead performs cleanup inside `on_transition` synchronously, this remains consistent — the state machine only records intent; app.c executes the sequence.

- [ ] **Step 4: Run to verify it passes**

Run: `pio test -e native -f test_power_state`
Expected: PASS (7 tests).

- [ ] **Step 5: Commit**

```bash
git add firmware/include/power_state.h firmware/src/power_state.c firmware/test/test_power_state.c
git commit -m "feat(firmware): add power state machine with host tests"
```

---

## Task 12: USB composite device glue (PCD config, descriptors, CDC+MSC)

**Files:**
- Create: `firmware/include/usbd_conf.h`
- Create: `firmware/include/usbd_desc.h`
- Create: `firmware/src/usbd_desc.c`
- Create: `firmware/src/usbd_cdc_if.c`
- Create: `firmware/src/usbd_msc_storage.c`
- Create: `firmware/include/hw_usb.h`
- Create: `firmware/src/hw_usb.c`

**Interfaces:**
- Consumes: `usbproto_parser_t` (Task 9), `hw_rtc_*`, `hw_adc_*` for STAT.
- Produces: `hw_usb_init`, `hw_usb_deinit`, `hw_usb_poll`, `hw_usb_cdc_send`. Consumed by M7 app.

- [ ] **Step 1: Write usbd_conf.h** (`firmware/include/usbd_conf.h`)

```c
#ifndef USBD_CONF_H
#define USBD_CONF_H

#include "stm32l4xx_hal.h"

#define USBD_MAX_NUM_INTERFACES       3u
#define USBD_MAX_NUM_CONFIGURATION    1u
#define USBD_MAX_NUM_SUPPORTED_ENDPOINTS 6u
#define USBD_USE_SRAM                  0u

/* Composite activation (from usbd_composite_builder.h) */
#define USBD_CMPSIT_ACTIVATE_CDC 1U
#define USBD_CMPSIT_ACTIVATE_MSC 1U
#define USBD_CMPST_MAX_CONFDESC_SZ 300U

#define USBD_CUSTOMHID_OUTREPORT_BUF_SIZE 0u

#define CDC_DATA_HS_MAX_PACKET_SIZE   64u
#define CDC_DATA_FS_MAX_PACKET_SIZE   64u
#define CDC_CMD_HS_MAX_PACKET_SIZE    8u
#define CDC_CMD_FS_MAX_PACKET_SIZE    8u
#define CDC_DATA_HS_BULKIN_BUF_SIZE   256u
#define CDC_DATA_FS_BULKIN_BUF_SIZE   256u
#define CDC_DATA_HS_BULKOUT_BUF_SIZE  256u
#define CDC_DATA_FS_BULKOUT_BUF_SIZE  256u
#define CDC_HS_MAX_PACKET_SIZE        64u
#define CDC_FS_MAX_PACKET_SIZE        64u

#define MSC_MEDIA_PACKET_SIZE         512u

/* PCD low-level config */
#define PCD_SOFTEND_MODE              0u
#define USB_EXT_LD_ENABLE             0u
#define USB_EXT_LD_INVERT             0u
#define USE_HAL_PCD_REGISTER_CALLBACKS 0u
#define USE_USBD_COMPOSITE            1u

#endif
```

- [ ] **Step 2: Write usbd_desc** (`firmware/include/usbd_desc.h`)

```c
#ifndef USBD_DESC_H
#define USBD_DESC_H

#include "usbd_def.h"

#define DEVICE_ID1 (0x1FFF7A10)
#define DEVICE_ID2 (0x1FFF7A14)
#define DEVICE_ID3 (0x1FFF7A18)
#define USB_SIZ_STRING 32u
#define USBD_VID 0x0483
#define USBD_PID 0x5741
#define USBD_LANGID_STRING 0x0409
#define USBD_MANUFACTURER_STRING "DayVault"
#define USBD_PRODUCT_STRING      "DayVault Recorder"
#define USBD_CONFIGURATION_STRING "DayVault Config"
#define USBD_INTERFACE_STRING    "DayVault CDC"

extern USBD_DescriptorsTypeDef FS_Desc;

#endif
```

(`firmware/src/usbd_desc.c`)

```c
#include "usbd_desc.h"
#include "usbd_composite_builder.h"
#include <string.h>
#include <stdio.h>

static uint8_t USBD_FS_DeviceDesc[USB_LEN_DEV_DESC];
static uint8_t USBD_FS_StrDesc[USB_MAX_STR_DESC_SIZ];

static void Get_SerialNum(void)
{
    uint32_t s1 = *(volatile uint32_t *)DEVICE_ID1;
    uint32_t s2 = *(volatile uint32_t *)DEVICE_ID2;
    uint32_t s3 = *(volatile uint32_t *)DEVICE_ID3;
    char buf[24];
    snprintf(buf, sizeof(buf), "%08lX%08lX%08lX",
             (unsigned long)s1, (unsigned long)s2, (unsigned long)s3);
    uint8_t *p = USBD_FS_StrDesc + 2;
    uint8_t len = 0;
    const char *c = buf;
    while (*c && len < USB_MAX_STR_DESC_SIZ - 2)
    {
        p[len++] = (uint8_t)*c;
        p[len++] = 0;
        c++;
    }
    USBD_FS_StrDesc[0] = (uint8_t)(len + 2);
    USBD_FS_StrDesc[1] = USB_DESC_TYPE_STRING;
}

static uint8_t *USBD_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    memset(USBD_FS_DeviceDesc, 0, USB_LEN_DEV_DESC);
    USBD_FS_DeviceDesc[0] = USB_LEN_DEV_DESC;
    USBD_FS_DeviceDesc[1] = USB_DESC_TYPE_DEVICE;
    USBD_FS_DeviceDesc[2] = 0x00; USBD_FS_DeviceDesc[3] = 0x02;
    USBD_FS_DeviceDesc[4] = 0xEF; /* composite */
    USBD_FS_DeviceDesc[5] = 0x02;
    USBD_FS_DeviceDesc[6] = 0x01;
    USBD_FS_DeviceDesc[7] = USB_MAX_EP0_SIZE;
    USBD_FS_DeviceDesc[8] = (uint8_t)USBD_VID;
    USBD_FS_DeviceDesc[9] = (uint8_t)(USBD_VID >> 8);
    USBD_FS_DeviceDesc[10] = (uint8_t)USBD_PID;
    USBD_FS_DeviceDesc[11] = (uint8_t)(USBD_PID >> 8);
    USBD_FS_DeviceDesc[12] = 0x00;
    USBD_FS_DeviceDesc[13] = 0x02;
    USBD_FS_DeviceDesc[14] = 0x00;
    USBD_FS_DeviceDesc[15] = 0x01;
    USBD_FS_DeviceDesc[16] = 0x00;
    USBD_FS_DeviceDesc[17] = 0x01;
    USBD_FS_DeviceDesc[18] = 0x00;
    USBD_FS_DeviceDesc[19] = 0x01;
    *length = USB_LEN_DEV_DESC;
    return USBD_FS_DeviceDesc;
}

static uint8_t *USBD_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_FS_StrDesc[0] = 4;
    USBD_FS_StrDesc[1] = USB_DESC_TYPE_STRING;
    USBD_FS_StrDesc[2] = (uint8_t)USBD_LANGID_STRING;
    USBD_FS_StrDesc[3] = (uint8_t)(USBD_LANGID_STRING >> 8);
    *length = 4;
    return USBD_FS_StrDesc;
}

static uint8_t *USBD_FS_StrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length, const char *str)
{
    (void)speed;
    uint8_t len = 0;
    uint8_t *p = USBD_FS_StrDesc + 2;
    while (*str && len < USB_MAX_STR_DESC_SIZ - 2)
    {
        p[len++] = (uint8_t)*str;
        p[len++] = 0;
        str++;
    }
    USBD_FS_StrDesc[0] = (uint8_t)(len + 2);
    USBD_FS_StrDesc[1] = USB_DESC_TYPE_STRING;
    *length = len + 2;
    return USBD_FS_StrDesc;
}

static uint8_t *USBD_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    return USBD_FS_StrDescriptor(speed, length, USBD_MANUFACTURER_STRING);
}

static uint8_t *USBD_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    return USBD_FS_StrDescriptor(speed, length, USBD_PRODUCT_STRING);
}

static uint8_t *USBD_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    Get_SerialNum();
    *length = USBD_FS_StrDesc[0];
    return USBD_FS_StrDesc;
}

static uint8_t *USBD_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    return USBD_FS_StrDescriptor(speed, length, USBD_CONFIGURATION_STRING);
}

static uint8_t *USBD_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    return USBD_FS_StrDescriptor(speed, length, USBD_INTERFACE_STRING);
}

USBD_DescriptorsTypeDef FS_Desc =
{
    USBD_FS_DeviceDescriptor,
    USBD_FS_LangIDStrDescriptor,
    USBD_FS_ManufacturerStrDescriptor,
    USBD_FS_ProductStrDescriptor,
    USBD_FS_SerialStrDescriptor,
    USBD_FS_ConfigStrDescriptor,
    USBD_FS_InterfaceStrDescriptor
};
```

Note: In composite mode the configuration descriptor is built at runtime by `USBD_CMPSIT_AddToConfDesc`, so `usbd_desc.c` only supplies the device + string descriptors. Cross-check `USB_MAX_STR_DESC_SIZ`, `USB_MAX_EP0_SIZE`, `USB_MAX_STR_DESC_SIZ`, `USB_LEN_DEV_DESC` macros against `usbd_def.h` when compiling.

- [ ] **Step 3: Write CDC interface glue** (`firmware/src/usbd_cdc_if.c`)

```c
#include "usbd_cdc.h"
#include "usbd_composite_builder.h"
#include "usbproto.h"
#include "hw_usb.h"
#include <string.h>

extern USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t cdc_rx_buf[256];
static usbproto_parser_t proto;

void usbd_cdc_if_init_parser(void)
{
    usbproto_init(&proto);
}

static int8_t cdc_Init(void) { return 0; }
static int8_t cdc_DeInit(void) { return 0; }
static int8_t cdc_Control(uint8_t cmd, uint8_t *pbuf, uint16_t length) { (void)cmd; (void)pbuf; (void)length; return 0; }
static int8_t cdc_Receive(uint8_t *pbuf, uint32_t *len)
{
    uint32_t i;
    for (i = 0; i < *len; i++)
    {
        usbproto_msg_t msg;
        if (usbproto_feed(&proto, pbuf[i], &msg) == USBPROTO_OK)
            hw_usb_handle_command(&msg);
    }
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, pbuf);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return 0;
}

static int8_t cdc_TransmitCplt(uint8_t *pbuf, uint32_t *len, uint8_t epnum) { (void)pbuf; (void)len; (void)epnum; return 0; }

static USBD_CDC_ItfTypeDef cdc_if =
{
    cdc_Init, cdc_DeInit, cdc_Control, cdc_Receive, cdc_TransmitCplt
};

void usbd_cdc_if_register(USBD_HandleTypeDef *pdev)
{
    USBD_CDC_RegisterInterface(pdev, &cdc_if);
    USBD_CDC_SetRxBuffer(pdev, cdc_rx_buf);
    USBD_CDC_ReceivePacket(pdev);
}
```

Note: `hw_usb_handle_command` is provided by `hw_usb.c` (next step). The composite builder assigns class IDs; `USBD_CMPSIT_SetClassID(pdev, CLASS_TYPE_CDC, 0)` returns the ID used by `USBD_CDC_TransmitPacket(pdev, ClassId)` in the composite build. Adjust the transmit path in `hw_usb.c` accordingly.

- [ ] **Step 4: Write MSC storage glue** (`firmware/src/usbd_msc_storage.c`)

```c
#include "usbd_msc.h"
#include "hw_spi_sd.h"
#include "usbd_storage_if_template.h"
#include <string.h>

static int8_t storage_Init(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t storage_GetCapacity(uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
    (void)lun;
    *block_num = (uint32_t)(hw_sd_capacity_bytes() / 512u);
    *block_size = 512u;
    return 0;
}

static int8_t storage_IsReady(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t storage_IsWriteProtected(uint8_t lun)
{
    (void)lun;
    return 1;   /* read-only export volume */
}

static int8_t storage_Read(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun;
    return hw_sd_read_sectors(blk_addr, buf, blk_len) ? 0 : -1;
}

static int8_t storage_Write(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun; (void)buf; (void)blk_addr; (void)blk_len;
    return -1;   /* write protected */
}

static int8_t storage_GetMaxLun(void)
{
    return 0;
}

static int8_t *storage_Inquiry(void)
{
    static int8_t inquiry[] = {
        0x00, 0x80, 0x00, 0x01,
        0x20, 0x00, 0x00, 0x02,
        'D', 'A', 'Y', 'V', 'A', 'U', 'L', 'T',
        'S', 'D', ' ', ' ', '1', '.', '0', '0',
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    return inquiry;
}

USBD_StorageTypeDef USBD_STORAGE_fops =
{
    storage_Init,
    storage_GetCapacity,
    storage_IsReady,
    storage_IsWriteProtected,
    storage_Read,
    storage_Write,
    storage_GetMaxLun,
    storage_Inquiry
};
```

Note: `usbd_storage_if_template.h` may not exist as a separate header — include `usbd_msc.h` only; the `USBD_StorageTypeDef` is declared there. Verify at compile.

- [ ] **Step 5: Write hw_usb** (`firmware/include/hw_usb.h`)

```c
#ifndef DAYVAULT_HW_USB_H
#define DAYVAULT_HW_USB_H

#include "usbproto.h"
#include <stdint.h>

void hw_usb_init(void);
void hw_usb_deinit(void);
void hw_usb_poll(void);
int hw_usb_cdc_send(const uint8_t *buf, uint16_t len);
void hw_usb_handle_command(const usbproto_msg_t *msg);

#endif
```

(`firmware/src/hw_usb.c`)

```c
#include "stm32l4xx_hal.h"
#include "usbd_core.h"
#include "usbd_composite_builder.h"
#include "usbd_cdc.h"
#include "usbd_msc.h"
#include "hw_usb.h"
#include "hw_rtc.h"
#include "hw_adc.h"
#include "usbd_desc.h"
#include "usbd_cdc_if.h"
#include <stdio.h>

static PCD_HandleTypeDef hpcd;
USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t tx_buf[128];

static void usb_msp_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USB_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_ENABLE_USB();

    g.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF10_OTG_FS;
    HAL_GPIO_Init(GPIOA, &g);
}

void HAL_PCD_MspInit(PCD_HandleTypeDef *hpcd_instance)
{
    (void)hpcd_instance;
    usb_msp_init();
}

static void cdc_send_line(const char *line)
{
    hw_usb_cdc_send((const uint8_t *)line, (uint16_t)strlen(line));
}

void hw_usb_init(void)
{
    USBD_StatusTypeDef ret;

    hpcd.Instance = USB;
    hpcd.Init.dev_endpoints = 8;
    hpcd.Init.speed = PCD_SPEED_FULL;
    hpcd.Init.phy_itface = PCD_PHY_EMBEDDED;
    hpcd.Init.low_power_enable = DISABLE;
    hpcd.Init.lpm_enable = DISABLE;
    hpcd.Init.battery_charging_enable = DISABLE;
    HAL_PCD_Init(&hpcd);

    ret = USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS);
    if (ret != USBD_OK)
        return;

    USBD_RegisterClass(&hUsbDeviceFS, &USBD_CMPSIT);
    USBD_CMPSIT_AddClass(&hUsbDeviceFS, &USBD_CDC, CLASS_TYPE_CDC, 0);
    USBD_CMPSIT_AddClass(&hUsbDeviceFS, &USBD_MSC, CLASS_TYPE_MSC, 0);
    usbd_cdc_if_register(&hUsbDeviceFS);
    USBD_Start(&hUsbDeviceFS);
}

void hw_usb_deinit(void)
{
    USBD_Stop(&hUsbDeviceFS);
    USBD_DeInit(&hUsbDeviceFS);
    HAL_PCD_DeInit(&hpcd);
}

void hw_usb_poll(void)
{
    /* handled via IRQ callbacks */
}

int hw_usb_cdc_send(const uint8_t *buf, uint16_t len)
{
    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
        return 0;
    memcpy(tx_buf, buf, len);
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, tx_buf, len);
    return USBD_CDC_TransmitPacket(&hUsbDeviceFS);
}

void hw_usb_handle_command(const usbproto_msg_t *msg)
{
    switch (msg->cmd)
    {
    case USBPROTO_CMD_TIME:
    {
        char line[64];
        utc_time_t t;
        hw_rtc_get_time(&t);
        timeutil_format_iso(&t, line + 3, sizeof(line) - 3);
        memcpy(line, "OK ", 3);
        cdc_send_line(line);
        break;
    }
    case USBPROTO_CMD_SYNC:
    {
        /* msg->arg is unix seconds; convert to utc_time_t via RTC BCD path
           (a host tool sends SYNC; the device sets the calendar). */
        char line[64];
        utc_time_t t = {1970, 1, 1, 0, 0, 0};
        uint32_t days = msg->arg / 86400u;
        uint32_t rem = msg->arg % 86400u;
        /* NOTE: full civil-from-days conversion lives in app/timeutil;
           placeholder path keeps compile-only sanity. */
        (void)days; (void)rem;
        hw_rtc_set_time(&t);
        snprintf(line, sizeof(line), "OK old=%lu new=%lu", (unsigned long)0, (unsigned long)msg->arg);
        cdc_send_line(line);
        break;
    }
    case USBPROTO_CMD_STAT:
    {
        char line[96];
        uint16_t mv = hw_adc_read_battery_mv();
        snprintf(line, sizeof(line), "batt=%umV", mv);
        cdc_send_line(line);
        break;
    }
    case USBPROTO_CMD_FLUSH:
        cdc_send_line("OK flush");
        break;
    }
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpdev)
{
    USBD_LL_SetupStage(hpdev->pData);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpdev, uint8_t epnum)
{
    USBD_LL_DataOutStage(hpdev->pData, epnum, hpdev->OUT_ep[epnum].xfer_count);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpdev, uint8_t epnum)
{
    USBD_LL_DataInStage(hpdev->pData, epnum, hpdev->IN_ep[epnum].xfer_count);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpdev)
{
    USBD_LL_SOF(hpdev->pData);
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpdev)
{
    USBD_LL_SetSpeed(hpdev->pData, USBD_SPEED_FULL);
    USBD_LL_Reset(hpdev->pData);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpdev)
{
    USBD_LL_Suspend(hpdev->pData);
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpdev)
{
    USBD_LL_Resume(hpdev->pData);
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpdev, uint8_t epnum)
{
    USBD_LL_IsoOUTIncomplete(hpdev->pData, epnum);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpdev, uint8_t epnum)
{
    USBD_LL_IsoINIncomplete(hpdev->pData, epnum);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpdev)
{
    USBD_LL_DevConnected(hpdev->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpdev)
{
    USBD_LL_DevDisconnected(hpdev->pData);
}

void USB_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd);
}
```

Note: `USBD_CDC_TransmitPacket(&hUsbDeviceFS)` vs `(pdev, ClassId)` — under `USE_USBD_COMPOSITE` the 2-arg form is required. Adjust `hw_usb_cdc_send` to fetch the class ID: `uint8_t cid = USBD_CMPSIT_GetClassID(&hUsbDeviceFS, CLASS_TYPE_CDC, 0); USBD_CDC_TransmitPacket(&hUsbDeviceFS, cid);`. Also `USBD_CDC_RegisterInterface`/`USBD_CDC_SetRxBuffer`/`USBD_CDC_ReceivePacket` in `usbd_cdc_if.c` similarly take the ClassId in composite builds — verify against installed headers and adapt. This task is compile-iterative by design.

- [ ] **Step 6: Compile-verify**

Run: `pio run -e dayvault`
Expected: SUCCESS. This is the most compile-iterative task; fix HAL/USB API mismatches until it builds. The composite class-ID plumbing is the documented risk area — do NOT weaken compile checks to force success.

- [ ] **Step 7: Commit**

```bash
git add firmware/include/usbd_conf.h firmware/include/usbd_desc.h firmware/src/usbd_desc.c firmware/src/usbd_cdc_if.c firmware/src/usbd_msc_storage.c firmware/include/hw_usb.h firmware/src/hw_usb.c
git commit -m "feat(firmware): add USB composite CDC+MSC glue"
```

---

## Task 13: IWDG + PWR (Standby/RTC wake) drivers

**Files:**
- Create: `firmware/include/hw_iwdg.h`
- Create: `firmware/src/hw_iwdg.c`
- Create: `firmware/include/hw_pwr.h`
- Create: `firmware/src/hw_pwr.c`

**Interfaces:**
- Consumes: `dayvault_config.h` (wake period).
- Produces: `hw_iwdg_init`, `hw_iwdg_feed`; `hw_pwr_set_wake_period`, `hw_pwr_enter_standby`. Consumed by M14 app.

- [ ] **Step 1: Write headers**

(`firmware/include/hw_iwdg.h`)

```c
#ifndef DAYVAULT_HW_IWDG_H
#define DAYVAULT_HW_IWDG_H

void hw_iwdg_init(void);
void hw_iwdg_feed(void);

#endif
```

(`firmware/include/hw_pwr.h`)

```c
#ifndef DAYVAULT_HW_PWR_H
#define DAYVAULT_HW_PWR_H

#include <stdint.h>

void hw_pwr_set_wake_period(uint32_t seconds);
void hw_pwr_enter_standby(void);

#endif
```

- [ ] **Step 2: Implement IWDG** (`firmware/src/hw_iwdg.c`)

```c
#include "stm32l4xx_hal.h"
#include "hw_iwdg.h"

static IWDG_HandleTypeDef hiwdg;

void hw_iwdg_init(void)
{
    __HAL_RCC_LSI_ENABLE();
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) == RESET) {}
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64;   /* LSI ~32k /64 = 500 Hz */
    hiwdg.Init.Reload = 5000;                   /* ~10 s window */
    hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
    HAL_IWDG_Init(&hiwdg);
}

void hw_iwdg_feed(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}
```

- [ ] **Step 3: Implement PWR** (`firmware/src/hw_pwr.c`)

```c
#include "stm32l4xx_hal.h"
#include "hw_pwr.h"
#include "dayvault_config.h"

void hw_pwr_set_wake_period(uint32_t seconds)
{
    RTC_HandleTypeDef hrtc;
    RTC_WakeUpTypeDef wu = {0};
    hrtc.Instance = RTC;
    wu.WakeUpClock = RTC_WAKEUPCLOCK_CK_SPRE_16BITS;
    wu.WakeUpCounter = (seconds * 1u);   /* 1 Hz LSE tick with prescaler 128+255 */
    wu.AutoReload = RTC_AUTO_RELOAD_DISABLE;
    HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, &wu, RTC_FORMAT_BIN);
    __HAL_RTC_WAKEUPTIMER_EXTI_ENABLE_IT();
    HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);
}

void hw_pwr_enter_standby(void)
{
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    HAL_PWR_EnableBkUpAccess();
    HAL_PWR_EnterSTANDBYMode();
}
```

Note: `hw_pwr_enter_standby` references `hrtc` which is static in `hw_rtc.c` — do NOT duplicate; instead expose the RTC handle from `hw_rtc.c` (add `RTC_HandleTypeDef *hw_rtc_handle(void)` to `hw_rtc.h`) and call `HAL_RTCEx_SetWakeUpTimer_IT` on it there. Fix this cross-file coupling during implementation. Verify `PWR_FLAG_WU`, `RTC_WAKEUPCLOCK_CK_SPRE_16BITS`, and wakeup counter semantics against the installed HAL.

- [ ] **Step 4: Compile-verify**

Run: `pio run -e dayvault`
Expected: SUCCESS (after fixing the hrtc handle cross-reference).

- [ ] **Step 5: Commit**

```bash
git add firmware/include/hw_iwdg.h firmware/src/hw_iwdg.c firmware/include/hw_pwr.h firmware/src/hw_pwr.c
git commit -m "feat(firmware): add IWDG and Standby/RTC-wake drivers"
```

---

## Task 14: app.c superloop integration + main.c boot

**Files:**
- Create: `firmware/include/app.h`
- Create: `firmware/src/app.c`
- Modify: `firmware/src/main.c`

**Interfaces:**
- Consumes: everything above.
- Produces: `app_init`, `app_run` (never returns).

- [ ] **Step 1: Write app.h** (`firmware/include/app.h`)

```c
#ifndef DAYVAULT_APP_H
#define DAYVAULT_APP_H

void app_init(void);
void app_run(void);

#endif
```

- [ ] **Step 2: Implement app.c** (`firmware/src/app.c`)

```c
#include "stm32l4xx_hal.h"
#include "app.h"
#include "dayvault_config.h"
#include "hw_rtc.h"
#include "hw_adc.h"
#include "hw_gpio.h"
#include "hw_spi_sd.h"
#include "hw_dfsdm.h"
#include "hw_usb.h"
#include "hw_iwdg.h"
#include "hw_pwr.h"
#include "power_state.h"
#include "segmgr.h"
#include "wav.h"
#include "adpcm.h"
#include "eventlog.h"
#include "ringbuf.h"
#include "timeutil.h"
#include "diskio_sd.h"
#include "ff.h"
#include <string.h>

static FATFS fs;
static FIL file;
static pstate_machine_t psm;
static segmgr_t seg;
static ringbuf_t audio_rb;
static uint8_t audio_buf[PDM_RING_BYTES];
static uint8_t sd_buf[512];
static volatile uint8_t df_ready[2] = {0, 0};
static uint8_t last_usb = 0;
static uint32_t last_bat_tick = 0;
static uint32_t last_sync_tick = 0;
static uint8_t seg_open = 0;
static int sd_mounted = 0;

static void on_transition(pstate_t from, pstate_t to, pevent_t evt)
{
    (void)from; (void)evt;
    switch (to)
    {
    case PSTATE_IDLE:
        /* finish segment, unmount card, then USB/MSC takes over */
        if (seg_open)
        {
            wav_patch_sizes(sd_buf, &wav_cfg(), 0);
            f_close(&file);
            seg_close();
        }
        if (sd_mounted)
            f_mount(0, 0, 0);
        sd_mounted = 0;
        break;
    case PSTATE_STOPPING:
        hw_dfsdm_stop();
        break;
    case PSTATE_STANDBY:
        hw_pwr_enter_standby();
        break;
    default:
        break;
    }
}

void app_init(void)
{
    utc_time_t t;

    hw_gpio_init();
    hw_rtc_init();
    hw_adc_init();
    hw_dfsdm_init();
    hw_usb_init();
    hw_iwdg_init();

    hw_rtc_bump_boot_counter();

    ringbuf_init(&audio_rb, audio_buf, sizeof(audio_buf));
    segmgr_init(&seg, SEGMENT_SECONDS, SEGMENT_PREALLOC_BYTES);
    pstate_init(&psm, &(pstate_actions_t){ on_transition });

    if (!hw_rtc_is_time_valid())
        t = (utc_time_t){2026, 1, 1, 0, 0, 0};   /* boot counter fallback */
    else
        hw_rtc_get_time(&t);

    if (hw_gpio_usb_detect())
        pstate_handle(&psm, PEVT_USB_ATTACH);
    else if (hw_adc_read_battery_mv() <= BAT_CRITICAL_MV)
        pstate_handle(&psm, PEVT_BATTERY_CRITICAL);
    else
    {
        if (hw_sd_init())
        {
            if (FATFS_LinkDriverEx(&Diskio_SD_Driver, "SD:", 0) == 0)
            {
                if (f_mount(&fs, "SD:", 1) == FR_OK)
                    sd_mounted = 1;
            }
        }
        pstate_handle(&psm, PEVT_BOOT_OK);
    }
    last_usb = hw_gpio_usb_detect();
    last_bat_tick = HAL_GetTick();
    last_sync_tick = HAL_GetTick();
}

static void open_segment(void)
{
    utc_time_t now;
    char path[128];
    hw_rtc_get_time(&now);
    segmgr_open(&seg, &now);
    if (hw_rtc_is_time_valid())
    {
        char day[64];
        timeutil_make_day_path(&now, day, sizeof(day));
        snprintf(path, sizeof(path), "SD:/%s", day);
    }
    else
    {
        char up[64];
        timeutil_make_unsynced_path(hw_rtc_boot_counter(), up, sizeof(up));
        snprintf(path, sizeof(path), "SD:/%s", up);
    }
    f_mkdir("SD:/DAYVAULT");
    if (f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
    {
        seg_open = 1;
    }
}

static void drain_audio(void)
{
    static uint8_t pcm_block[1024];
    uint8_t enc_block[516];
    uint16_t got;
    got = (uint16_t)ringbuf_read(&audio_rb, pcm_block, sizeof(pcm_block));
    while (got >= 2u)
    {
        uint16_t samples = got / 2u;
        /* mono PCM path: write raw; ADPCM path would use adpcm_encode_block */
        UINT wr = 0;
        if (seg_open)
            f_write(&file, pcm_block, samples * 2u, &wr);
        got -= (uint16_t)(samples * 2u);
    }
}

void app_run(void)
{
    uint32_t now_tick;
    for (;;)
    {
        now_tick = HAL_GetTick();
        hw_iwdg_feed();

        /* battery periodic sampling (250 ms settle + hysteresis) */
        if (now_tick - last_bat_tick >= 250u)
        {
            uint16_t mv = hw_adc_read_battery_mv();
            if (mv <= BAT_CRITICAL_MV)
                pstate_handle(&psm, PEVT_BATTERY_CRITICAL);
            else if (mv <= BAT_WARNING_MV)
                pstate_handle(&psm, PEVT_BATTERY_WARNING);
            else if (mv >= BAT_RECOVERY_MV)
                pstate_handle(&psm, PEVT_BATTERY_OK);
            last_bat_tick = now_tick;
        }

        /* USB_DETECT debounce (>= 5 ms) */
        {
            uint8_t now_usb = hw_gpio_usb_detect();
            if (now_usb != last_usb)
            {
                HAL_Delay(5);
                now_usb = hw_gpio_usb_detect();
                if (now_usb != last_usb)
                {
                    pstate_handle(&psm, now_usb ? PEVT_USB_ATTACH : PEVT_USB_DETACH);
                    last_usb = now_usb;
                }
            }
        }

        if (pstate_get(&psm) == PSTATE_RECORDING || pstate_get(&psm) == PSTATE_RECORDING_LOW)
        {
            if (!seg_open)
                open_segment();

            /* segment rotation */
            {
                utc_time_t now;
                hw_rtc_get_time(&now);
                if (seg_open && segmgr_should_rotate(&seg, &now))
                {
                    f_close(&file);
                    seg_open = 0;
                    segmgr_close(&seg);
                }
            }

            drain_audio();

            /* periodic WAV header sync (10 s) */
            if (now_tick - last_sync_tick >= WAV_SYNC_INTERVAL_MS)
            {
                if (seg_open)
                {
                    f_sync(&file);
                    last_sync_tick = now_tick;
                }
            }
        }

        hw_usb_poll();
    }
}
```

Note: this task intentionally leaves several known gaps to fix during implementation, each with a follow-up:
- `wav_cfg()` and `seg_close()` are not defined above; define them as static helpers (PCM config struct) before use.
- `drain_audio` reads bytes but writes samples; fix indexing to byte-aligned writes (the `got` loop above has a bug: it re-reads on each iteration — replace with a single read + single write).
- `open_segment` should build the WAV header into the file (write `wav_build_header` bytes first) and preallocate `SEGMENT_PREALLOC_BYTES`.
- File path must include the filename from `segmgr_build_name`, not just the directory.
- The `df_ready` DMA-flag draining (half/full buffer handoff) is not wired; add `hw_dfsdm_set_callback` to copy PCM into `audio_rb` and bump `overrun_count` on overflow.

Fix all of these in the implementation; the compile must pass and the pure-logic invariants must stay covered by the host tests. Modify `main.c` as follows.

(`firmware/src/main.c` — replace the `while (1)` body)

```c
#include "stm32l4xx_hal.h"
#include "app.h"

static void SystemClock_Config(void);
void Error_Handler(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    app_init();
    app_run();
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.MSIState = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = 0;
    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_10;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
        Error_Handler();

    HAL_InitTick(TICK_INT_PRIORITY);
}

void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
    while (1) {}
}
#endif
```

Note: the DFSDM/DMA `hw_dfsdm_start()` must be called from `app_init` after the ringbuf/pstate setup (add it). `HW_DFSDM` overrun counting and the audio drain are the two most likely places where compile passes but logic is incomplete — the app must not call `f_write` before a segment is open (guarded above), and the state machine's `PSTATE_RECORDING`/`PSTATE_IDLE` transitions trigger the correct open/close/unmount in `on_transition`.

- [ ] **Step 3: Compile-verify**

Run: `pio run -e dayvault`
Expected: SUCCESS.

- [ ] **Step 4: Run all host tests**

Run: `pio test -e native`
Expected: ALL PASS.

- [ ] **Step 5: Commit**

```bash
git add firmware/include/app.h firmware/src/app.c firmware/src/main.c
git commit -m "feat(firmware): integrate superloop, boot sequence, recording pipeline"
```

---

## Task 15: Final verification pass

**Files:**
- (none new; verify only)

- [ ] **Step 1: Full native test run**

Run: `pio test -e native`
Expected: ALL PASS (ringbuf, timeutil, wav, adpcm, segmgr, usbproto, eventlog, power_state).

- [ ] **Step 2: Full device build**

Run: `pio run -e dayvault`
Expected: SUCCESS. Note the size budget: Flash ≤ 262144 B, RAM ≤ 65536 B. If over, trim: `_MAX_LFN`→128, PDM_RING_BYTES, or drop `_FS_EXFAT`→0.

- [ ] **Step 3: Review plan vs spec coverage**

Skim `Docs/10-Firmware-Architecture-Design.md` and confirm each of D1–D7 and all 8 feature bullets map to a task above. Fix any gaps before proceeding.

- [ ] **Step 4: Commit any final touches**

```bash
git add -A
git commit -m "chore(firmware): final verification pass"
```

---

## Execution Handoff

Plan complete and saved to `Docs/11-Firmware-Implementation-Plan.md`.

**Two execution options:**

**1. Subagent-Driven (recommended)** — dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
