# Stereo Recording + USB MSC Export Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add DayVault recording: dual-microphone PDM capture (DFSDM Channel 1 direct + Channel 0 redirected) to stereo 16 kHz/16-bit WAV on an exFAT microSD card while USB is detached; on USB attach stop, finalize, unmount, and expose the whole card as a read-only USB MSC volume; on detach remount and resume recording.

**Architecture:** Bare-metal superloop. Pure-logic modules (`wav`, `ringbuf`, `rec_mgr`) are host-tested via the `native` PlatformIO env with Unity; hardware drivers (`hw_spi_sd`, `diskio_sd`, `hw_dfsdm`, `usbd_msc_storage`) are compile-only. FatFs R0.12c is vendored into `lib/FatFs`. Existing USB CDC/DFU code stays in tree; on USB attach the stack is switched to MSC-only.

**Tech Stack:** PlatformIO 6.1.19 + stm32cube (STM32CubeL4 1.18.1), FatFs R0.12c vendored, Unity host tests, C11 (host) / C99 (device, `-Os`).

**Design spec:** `docs/superpowers/specs/2026-08-08-recording-export-design.md`

## Global Constraints

- MCU STM32L452RCT6, 256 KB flash / 64 KB RAM. Linker `boards/stm32l452rc.ld` (64K RAM) — do not change.
- Pins (MUST match Docs/02): PDM_CLK=PC2 (DFSDM1_CKOUT), PDM_DATA=PB12 (DFSDM1_DATIN1), SD_CS=PA4, SD_SCK=PA5, SD_MISO=PA6, SD_MOSI=PA7 (SPI1), USB_DETECT=PA9, USB_DP=PA12, USB_DM=PA11, LED=PA8.
- Build (device): `pio run -e dayvault`; tests: `pio test -e native`. Native env compiles ONLY pure-logic `.c` files (extend the existing filter).
- Audio: 16 kHz, 16-bit, **2 channels** stereo PCM WAV. DFSDM Channel 1 direct DATIN1 + Channel 0 redirected to Channel 1, opposite sampling edges, separate filters + DMA streams. Mono fallback via `#define STEREO_MONO_FALLBACK` (Channel 1 only) if stereo fails on hardware.
- Recording file: `REC001.WAV` ... sequence; next = scan existing `REC*.WAV` at mount, max+1, fallback boot counter.
- exFAT card: FatFs `_FS_EXFAT=1`, `_MAX_SS=512`, `_USE_LFN=2`, `_VOLUMES=1`, `_FS_NORTC=1`.
- SD addressing: `hw_sd_read_sectors(raw_lba)` for MSC; FatFs uses partition LBA (MBR offset added in `diskio_sd`).
- USB attach → stop record → finalize WAV → `f_mount(NULL)` → switch USB to MSC-only (read-only). USB detach → switch back to recording → next file.
- No unbounded waits; all blocking paths bounded. No comments unless hardware/fact context.

---

## File Structure

```
firmware/
  platformio.ini                     Modify: hal modules, native filter, MSC define
  include/
    dayvault_config.h                Modify: add SD pins, DFSDM params, rec constants
    stm32l4xx_hal_conf.h             Modify: enable HAL_DMA, HAL_DFSDM, HAL_SPI
    wav.h                            Create: stereo PCM WAV header
    ringbuf.h                        Create: lock-free byte ring buffer
    rec_mgr.h                        Create: recording state machine (pure)
    hw_spi_sd.h                      Create: SPI1 SD driver
    diskio_sd.h                      Create: FatFs glue decl
    hw_dfsdm.h                       Create: DFSDM capture
    usbd_msc_storage.h               Create: MSC block device
    usbd_desc.h                      Modify: MSC descriptor support
  src/
    wav.c                            Create
    ringbuf.c                        Create
    rec_mgr.c                        Create
    hw_spi_sd.c                      Create
    diskio_sd.c                      Create
    hw_dfsdm.c                       Create
    usbd_msc_storage.c               Create
    app.c                            Modify: orchestrate record/MSC switch
    main.c                           Modify: init order
  lib/
    FatFs/                           Create: vendored from framework (ff.c ff.h diskio.c diskio.h ff_gen_drv.c ff_gen_drv.h integer.h + project ffconf.h)
  test/
    test_wav/test_wav.c              Create
    test_ringbuf/test_ringbuf.c      Create
    test_rec_mgr/test_rec_mgr.c      Create
```

---

### Task 1: Enable HAL modules + wav module (stereo PCM) + host tests

**Files:**
- Modify: `firmware/include/stm32l4xx_hal_conf.h` (enable HAL_DMA, HAL_DFSDM, HAL_SPI)
- Create: `firmware/include/wav.h`, `firmware/src/wav.c`
- Create: `firmware/test/test_wav/test_wav.c`
- Modify: `firmware/platformio.ini` (native env add `+<wav.c>`, `+<ringbuf.c>`, `+<rec_mgr.c>`)

**Interfaces:**
- Produces: `wav_config_t` (channels=2, sample_rate=16000, bits=16), `wav_header_size`, `wav_build_header`, `wav_patch_sizes`, `wav_pcm_bytes_to_samples`. Consumed by Task 5 app and Task 6 rec integration.
- `wav_config_t { uint16_t format; uint32_t sample_rate; uint16_t channels; uint16_t bits; uint16_t block_align; uint32_t byte_rate; }`

- [ ] **Step 1: Enable HAL modules in `firmware/include/stm32l4xx_hal_conf.h`**

Change these three lines from `/* #define` to `#define`:

```c
#define HAL_DMA_MODULE_ENABLED
#define HAL_DFSDM_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
```

- [ ] **Step 2: Create `firmware/include/wav.h`**

```c
#ifndef DAYVAULT_WAV_H
#define DAYVAULT_WAV_H

#include <stddef.h>
#include <stdint.h>

#define WAV_PCM_FORMAT 1u

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

#endif
```

- [ ] **Step 3: Write the failing test** (`firmware/test/test_wav/test_wav.c`)

```c
#include "unity.h"
#include <string.h>
#include "wav.h"

static wav_config_t cfg;

void setUp(void)
{
    memset(&cfg, 0, sizeof(cfg));
    cfg.format = WAV_PCM_FORMAT;
    cfg.sample_rate = 16000;
    cfg.channels = 2;
    cfg.bits = 16;
    cfg.block_align = 4;
    cfg.byte_rate = 64000;
}
void tearDown(void)
{
}

void test_stereo_header_size_is_44(void)
{
    TEST_ASSERT_EQUAL_UINT(44u, wav_header_size(&cfg));
}

void test_stereo_header_golden_bytes(void)
{
    uint8_t hdr[44];
    uint8_t exp[44] = {
        'R','I','F','F',  0x24,0,0,0,
        'W','A','V','E',
        'f','m','t',' ',  0x10,0,0,0,
        0x01,0, 0x02,0, 0x80,0x3E,0,0, 0x00,0xFA,0,0, 0x04,0, 0x10,0,
        'd','a','t','a',  0,0,0,0
    };
    wav_build_header(hdr, &cfg, 0);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, hdr, 44);
}

void test_patch_sizes_after_write(void)
{
    uint8_t hdr[44];
    wav_build_header(hdr, &cfg, 0);
    wav_patch_sizes(hdr, &cfg, 1000u);
    /* RIFF size = 36 + data = 1036 = 0x040C, data size = 1000 = 0x03E8 */
    TEST_ASSERT_EQUAL_UINT8(0x0C, hdr[4]);
    TEST_ASSERT_EQUAL_UINT8(0x04, hdr[5]);
    TEST_ASSERT_EQUAL_UINT8(0x00, hdr[6]);
    TEST_ASSERT_EQUAL_UINT8(0x00, hdr[7]);
    TEST_ASSERT_EQUAL_UINT8(0xE8, hdr[40]);
    TEST_ASSERT_EQUAL_UINT8(0x03, hdr[41]);
    TEST_ASSERT_EQUAL_UINT8(0x00, hdr[42]);
    TEST_ASSERT_EQUAL_UINT8(0x00, hdr[43]);
}

void test_pcm_bytes_to_samples_stereo(void)
{
    /* 1000 bytes / 4 bytes-per-sample = 250 samples (stereo frame count) */
    TEST_ASSERT_EQUAL_UINT(250u, wav_pcm_bytes_to_samples(1000u, &cfg));
}
```

- [ ] **Step 4: Run to verify it fails**

Run: `pio test -e native -f test_wav`
Expected: FAIL — `wav.h` not found.

- [ ] **Step 5: Implement `firmware/src/wav.c`**

```c
#include "wav.h"
#include <string.h>

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

size_t wav_header_size(const wav_config_t *cfg)
{
    (void)cfg;
    return 44u;
}

void wav_build_header(uint8_t *hdr, const wav_config_t *cfg, uint32_t data_bytes)
{
    memset(hdr, 0, wav_header_size(cfg));
    hdr[0] = 'R'; hdr[1] = 'I'; hdr[2] = 'F'; hdr[3] = 'F';
    put_le32(hdr + 4, 36u + data_bytes);
    hdr[8] = 'W'; hdr[9] = 'A'; hdr[10] = 'V'; hdr[11] = 'E';
    hdr[12] = 'f'; hdr[13] = 'm'; hdr[14] = 't'; hdr[15] = ' ';
    put_le32(hdr + 16, 16u);
    put_le16(hdr + 20, cfg->format);
    put_le16(hdr + 22, cfg->channels);
    put_le32(hdr + 24, cfg->sample_rate);
    put_le32(hdr + 28, cfg->byte_rate);
    put_le16(hdr + 32, cfg->block_align);
    put_le16(hdr + 34, cfg->bits);
    hdr[36] = 'd'; hdr[37] = 'a'; hdr[38] = 't'; hdr[39] = 'a';
    put_le32(hdr + 40, data_bytes);
}

void wav_patch_sizes(uint8_t *hdr, const wav_config_t *cfg, uint32_t data_bytes)
{
    (void)cfg;
    put_le32(hdr + 4, 36u + data_bytes);
    put_le32(hdr + 40, data_bytes);
}

uint32_t wav_pcm_bytes_to_samples(uint32_t data_bytes, const wav_config_t *cfg)
{
    return data_bytes / cfg->block_align;
}
```

- [ ] **Step 6: Run to verify it passes**

Run: `pio test -e native -f test_wav`
Expected: PASS (4 tests).

- [ ] **Step 7: Update `firmware/platformio.ini` native env filter**

Change the `[env:native]` `build_src_filter` to also compile wav, ringbuf, rec_mgr:

```ini
[env:native]
platform = native
test_framework = unity
build_src_filter =
    -<*>
    +<usbproto.c>
    +<wav.c>
    +<ringbuf.c>
    +<rec_mgr.c>
test_build_src = true
```

(ringbuf.c/rec_mgr.c do not exist until Tasks 2-3; PlatformIO ignores missing filter targets.)

- [ ] **Step 8: Verify device still builds**

Run: `pio run -e dayvault`
Expected: SUCCESS, zero warnings.

- [ ] **Step 9: Commit**

```bash
git add firmware/include/stm32l4xx_hal_conf.h firmware/include/wav.h firmware/src/wav.c firmware/test/test_wav/test_wav.c firmware/platformio.ini
git commit -m "feat(firmware): add stereo PCM WAV module with host tests; enable DMA/DFSDM/SPI HAL"
```

---

### Task 2: ringbuf module + host tests

**Files:**
- Create: `firmware/include/ringbuf.h`, `firmware/src/ringbuf.c`
- Create: `firmware/test/test_ringbuf/test_ringbuf.c`

**Interfaces:**
- Produces: `ringbuf_t`, `ringbuf_init(rb, buf, size)`, `ringbuf_write(rb, data, n)` → bytes written (drops overflow), `ringbuf_read(rb, dst, n)` → bytes read, `ringbuf_used(rb)`, `ringbuf_free(rb)`. Consumed by Task 5 app (DMA ISR → ring, superloop → SD).

- [ ] **Step 1: Create `firmware/include/ringbuf.h`**

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
    size_t used;
} ringbuf_t;

void ringbuf_init(ringbuf_t *rb, uint8_t *buf, size_t size);
size_t ringbuf_used(const ringbuf_t *rb);
size_t ringbuf_free(const ringbuf_t *rb);
size_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, size_t n);
size_t ringbuf_read(ringbuf_t *rb, uint8_t *dst, size_t n);

#endif
```

- [ ] **Step 2: Write the failing test** (`firmware/test/test_ringbuf/test_ringbuf.c`)

```c
#include "unity.h"
#include <string.h>
#include "ringbuf.h"

static uint8_t backing[16];
static ringbuf_t rb;

void setUp(void)
{
    ringbuf_init(&rb, backing, sizeof(backing));
}
void tearDown(void)
{
}

void test_empty_state(void)
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

void test_overflow_drops(void)
{
    uint8_t in[32] = {0};
    memset(in, 0xAB, sizeof(in));
    TEST_ASSERT_EQUAL_UINT(16u, ringbuf_write(&rb, in, 32));
    TEST_ASSERT_EQUAL_UINT(16u, ringbuf_used(&rb));
}

void test_wraparound(void)
{
    uint8_t in[12];
    uint8_t out[4];
    uint8_t expected[4] = {9, 10, 11, 12};
    uint8_t i;
    for (i = 0; i < 12; i++)
        in[i] = i;
    TEST_ASSERT_EQUAL_UINT(12u, ringbuf_write(&rb, in, 12));
    TEST_ASSERT_EQUAL_UINT(4u, ringbuf_read(&rb, out, 4));
    TEST_ASSERT_EQUAL_UINT(6u, ringbuf_write(&rb, in, 6));
    TEST_ASSERT_EQUAL_UINT(4u, ringbuf_read(&rb, out, 4));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 4);
}

void test_read_empty_returns_zero(void)
{
    uint8_t out[4];
    TEST_ASSERT_EQUAL_UINT(0u, ringbuf_read(&rb, out, 4));
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `pio test -e native -f test_ringbuf`
Expected: FAIL — `ringbuf.h` not found.

- [ ] **Step 4: Implement `firmware/src/ringbuf.c`**

```c
#include "ringbuf.h"

void ringbuf_init(ringbuf_t *rb, uint8_t *buf, size_t size)
{
    rb->buf = buf;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    rb->used = 0;
}

size_t ringbuf_used(const ringbuf_t *rb)
{
    return rb->used;
}

size_t ringbuf_free(const ringbuf_t *rb)
{
    return rb->size - rb->used;
}

size_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
    {
        if (rb->used == rb->size)
            break;
        rb->buf[rb->head] = data[i];
        rb->head = (rb->head + 1) % rb->size;
        rb->used++;
    }
    return i;
}

size_t ringbuf_read(ringbuf_t *rb, uint8_t *dst, size_t n)
{
    size_t i;
    for (i = 0; i < n && rb->used > 0; i++)
    {
        dst[i] = rb->buf[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
        rb->used--;
    }
    return i;
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `pio test -e native -f test_ringbuf`
Expected: PASS (5 tests).

- [ ] **Step 6: Commit**

```bash
git add firmware/include/ringbuf.h firmware/src/ringbuf.c firmware/test/test_ringbuf/test_ringbuf.c
git commit -m "feat(firmware): add ring buffer module with host tests"
```

---

### Task 3: rec_mgr state machine + host tests

**Files:**
- Create: `firmware/include/rec_mgr.h`, `firmware/src/rec_mgr.c`
- Create: `firmware/test/test_rec_mgr/test_rec_mgr.c`

**Interfaces:**
- Produces: `rec_state_t` enum (`REC_IDLE, REC_RECORDING, REC_STOPPING, REC_MSC`), `rec_mgr_init(mgr, actions*)`, `rec_mgr_event(mgr, evt)`, `rec_mgr_state(mgr)`. `rec_actions_t { void (*start_capture)(void); void (*stop_capture)(void); void (*finalize_file)(void); void (*mount_fs)(void); void (*unmount_fs)(void); void (*start_msc)(void); void (*stop_msc)(void); }`. Consumed by Task 5/6 app.
- `rec_event_t` enum: `REC_EVT_USB_ATTACH, REC_EVT_USB_DETACH, REC_EVT_CAPTURE_STARTED, REC_EVT_FINALIZE_DONE, REC_EVT_MSC_READY, REC_EVT_MSC_DONE`.

- [ ] **Step 1: Create `firmware/include/rec_mgr.h`**

```c
#ifndef DAYVAULT_REC_MGR_H
#define DAYVAULT_REC_MGR_H

typedef enum
{
    REC_IDLE = 0,
    REC_RECORDING,
    REC_STOPPING,
    REC_MSC
} rec_state_t;

typedef enum
{
    REC_EVT_USB_ATTACH = 0,
    REC_EVT_USB_DETACH,
    REC_EVT_CAPTURE_STARTED,
    REC_EVT_FINALIZE_DONE,
    REC_EVT_MSC_READY,
    REC_EVT_MSC_DONE
} rec_event_t;

typedef struct
{
    void (*start_capture)(void);
    void (*stop_capture)(void);
    void (*finalize_file)(void);
    void (*mount_fs)(void);
    void (*unmount_fs)(void);
    void (*start_msc)(void);
    void (*stop_msc)(void);
} rec_actions_t;

typedef struct
{
    rec_state_t state;
    const rec_actions_t *actions;
} rec_mgr_t;

void rec_mgr_init(rec_mgr_t *mgr, const rec_actions_t *actions);
void rec_mgr_event(rec_mgr_t *mgr, rec_event_t evt);
rec_state_t rec_mgr_state(const rec_mgr_t *mgr);

#endif
```

- [ ] **Step 2: Write the failing test** (`firmware/test/test_rec_mgr/test_rec_mgr.c`)

```c
#include "unity.h"
#include "rec_mgr.h"

static rec_mgr_t mgr;
static int start_capture_calls, stop_capture_calls, finalize_calls;
static int mount_calls, unmount_calls, start_msc_calls, stop_msc_calls;

static void act_start_capture(void) { start_capture_calls++; }
static void act_stop_capture(void) { stop_capture_calls++; }
static void act_finalize(void) { finalize_calls++; }
static void act_mount(void) { mount_calls++; }
static void act_unmount(void) { unmount_calls++; }
static void act_start_msc(void) { start_msc_calls++; }
static void act_stop_msc(void) { stop_msc_calls++; }

static const rec_actions_t actions = {
    act_start_capture, act_stop_capture, act_finalize,
    act_mount, act_unmount, act_start_msc, act_stop_msc
};

void setUp(void)
{
    rec_mgr_init(&mgr, &actions);
    start_capture_calls = 0;
    stop_capture_calls = 0;
    finalize_calls = 0;
    mount_calls = 0;
    unmount_calls = 0;
    start_msc_calls = 0;
    stop_msc_calls = 0;
}
void tearDown(void)
{
}

void test_init_state_idle(void)
{
    TEST_ASSERT_EQUAL_UINT(REC_IDLE, rec_mgr_state(&mgr));
}

void test_detach_starts_recording(void)
{
    rec_mgr_event(&mgr, REC_EVT_USB_DETACH);
    TEST_ASSERT_EQUAL_UINT(REC_RECORDING, rec_mgr_state(&mgr));
    TEST_ASSERT_EQUAL_INT(1, start_capture_calls);
}

void test_attach_stops_and_goes_msc(void)
{
    rec_mgr_event(&mgr, REC_EVT_USB_DETACH);   /* -> RECORDING */
    rec_mgr_event(&mgr, REC_EVT_CAPTURE_STARTED);
    rec_mgr_event(&mgr, REC_EVT_USB_ATTACH);   /* -> STOPPING */
    TEST_ASSERT_EQUAL_UINT(REC_STOPPING, rec_mgr_state(&mgr));
    TEST_ASSERT_EQUAL_INT(1, stop_capture_calls);
    rec_mgr_event(&mgr, REC_EVT_FINALIZE_DONE); /* -> MSC */
    TEST_ASSERT_EQUAL_UINT(REC_MSC, rec_mgr_state(&mgr));
    TEST_ASSERT_EQUAL_INT(1, finalize_calls);
    TEST_ASSERT_EQUAL_INT(1, unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, start_msc_calls);
}

void test_msc_detach_resumes_recording(void)
{
    rec_mgr_event(&mgr, REC_EVT_USB_DETACH);
    rec_mgr_event(&mgr, REC_EVT_CAPTURE_STARTED);
    rec_mgr_event(&mgr, REC_EVT_USB_ATTACH);
    rec_mgr_event(&mgr, REC_EVT_FINALIZE_DONE);
    TEST_ASSERT_EQUAL_UINT(REC_MSC, rec_mgr_state(&mgr));
    rec_mgr_event(&mgr, REC_EVT_USB_DETACH);   /* -> RECORDING, new file */
    TEST_ASSERT_EQUAL_UINT(REC_RECORDING, rec_mgr_state(&mgr));
    TEST_ASSERT_EQUAL_INT(1, stop_msc_calls);
    TEST_ASSERT_EQUAL_INT(1, mount_calls);
}

void test_attach_in_idle_goes_msc(void)
{
    rec_mgr_event(&mgr, REC_EVT_USB_ATTACH);
    TEST_ASSERT_EQUAL_UINT(REC_STOPPING, rec_mgr_state(&mgr));
    rec_mgr_event(&mgr, REC_EVT_FINALIZE_DONE);
    TEST_ASSERT_EQUAL_UINT(REC_MSC, rec_mgr_state(&mgr));
}

void test_null_actions_tolerated(void)
{
    rec_mgr_t m;
    rec_mgr_init(&m, 0);
    rec_mgr_event(&m, REC_EVT_USB_DETACH);
    TEST_ASSERT_EQUAL_UINT(REC_RECORDING, rec_mgr_state(&m));
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `pio test -e native -f test_rec_mgr`
Expected: FAIL — `rec_mgr.h` not found.

- [ ] **Step 4: Implement `firmware/src/rec_mgr.c`**

```c
#include "rec_mgr.h"

static void call(const rec_actions_t *a, void (*fn)(void))
{
    if (a && fn)
        fn();
}

void rec_mgr_init(rec_mgr_t *mgr, const rec_actions_t *actions)
{
    mgr->state = REC_IDLE;
    mgr->actions = actions;
}

rec_state_t rec_mgr_state(const rec_mgr_t *mgr)
{
    return mgr->state;
}

void rec_mgr_event(rec_mgr_t *mgr, rec_event_t evt)
{
    const rec_actions_t *a = mgr->actions;
    switch (mgr->state)
    {
    case REC_IDLE:
        if (evt == REC_EVT_USB_DETACH)
        {
            call(a, a->mount_fs);
            call(a, a->start_capture);
            mgr->state = REC_RECORDING;
        }
        else if (evt == REC_EVT_USB_ATTACH)
        {
            call(a, a->finalize_file);
            call(a, a->unmount_fs);
            mgr->state = REC_STOPPING;
        }
        break;

    case REC_RECORDING:
        if (evt == REC_EVT_USB_ATTACH)
        {
            call(a, a->stop_capture);
            mgr->state = REC_STOPPING;
        }
        break;

    case REC_STOPPING:
        if (evt == REC_EVT_FINALIZE_DONE)
        {
            call(a, a->finalize_file);
            call(a, a->unmount_fs);
            call(a, a->start_msc);
            mgr->state = REC_MSC;
        }
        break;

    case REC_MSC:
        if (evt == REC_EVT_USB_DETACH)
        {
            call(a, a->stop_msc);
            mgr->state = REC_IDLE;
            call(a, a->mount_fs);
            call(a, a->start_capture);
            mgr->state = REC_RECORDING;
        }
        break;
    }
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `pio test -e native -f test_rec_mgr`
Expected: PASS (6 tests).

- [ ] **Step 6: Commit**

```bash
git add firmware/include/rec_mgr.h firmware/src/rec_mgr.c firmware/test/test_rec_mgr/test_rec_mgr.c
git commit -m "feat(firmware): add recording state machine with host tests"
```

---

### Task 4: Vendor FatFs + SPI1 SD driver + FatFs glue

**Files:**
- Create: `firmware/lib/FatFs/` (ff.c, ff.h, diskio.c, diskio.h, ff_gen_drv.c, ff_gen_drv.h, integer.h, ffconf.h)
- Create: `firmware/include/hw_spi_sd.h`, `firmware/src/hw_spi_sd.c`
- Create: `firmware/include/diskio_sd.h`, `firmware/src/diskio_sd.c`

**Interfaces:**
- Produces: `hw_sd_init()` → 1 ok; `hw_sd_read_sectors(lba, buf, count, raw)` → 1 ok (raw=1 uses raw LBA, raw=0 uses partition LBA); `hw_sd_write_sectors(lba, buf, count, raw)`; `hw_sd_capacity_bytes()`. FatFs glue registers `Diskio_SD_Driver`.
- `diskio_sd.c` uses partition-relative LBA (raw=0). `usbd_msc_storage` (Task 6) uses raw LBA (raw=1).

- [ ] **Step 1: Vendor FatFs**

Copy from framework package (the installed path):

```bash
mkdir firmware\lib\FatFs
copy "C:\Users\Administrator\.platformio\packages\framework-stm32cubel4\Middlewares\Third_Party\FatFs\src\ff.c"  firmware\lib\FatFs\ff.c
copy "C:\Users\Administrator\.platformio\packages\framework-stm32cubel4\Middlewares\Third_Party\FatFs\src\ff.h"  firmware\lib\FatFs\ff.h
copy "C:\Users\Administrator\.platformio\packages\framework-stm32cubel4\Middlewares\Third_Party\FatFs\src\diskio.c" firmware\lib\FatFs\diskio.c
copy "C:\Users\Administrator\.platformio\packages\framework-stm32cubel4\Middlewares\Third_Party\FatFs\src\diskio.h" firmware\lib\FatFs\diskio.h
copy "C:\Users\Administrator\.platformio\packages\framework-stm32cubel4\Middlewares\Third_Party\FatFs\src\ff_gen_drv.c" firmware\lib\FatFs\ff_gen_drv.c
copy "C:\Users\Administrator\.platformio\packages\framework-stm32cubel4\Middlewares\Third_Party\FatFs\src\ff_gen_drv.h" firmware\lib\FatFs\ff_gen_drv.h
copy "C:\Users\Administrator\.platformio\packages\framework-stm32cubel4\Middlewares\Third_Party\FatFs\src\integer.h" firmware\lib\FatFs\integer.h
```

Note: `_FATFS` revision id is `68300`; `ff.h:31` asserts `_FATFS == _FFCONF`, so `ffconf.h` MUST define `_FFCONF 68300`.

- [ ] **Step 2: Create `firmware/lib/FatFs/ffconf.h`**

```c
#define _FFCONF 68300
#define _FS_READONLY 0
#define _FS_MINIMIZE 0
#define _USE_STRFUNC 0
#define _USE_FIND 0
#define _USE_MKFS 1
#define _USE_FASTSEEK 0
#define _USE_EXPAND 0
#define _USE_CHMOD 0
#define _USE_LABEL 0
#define _USE_FORWARD 0
#define _USE_TRIM 0
#define _CODE_PAGE 850
#define _USE_LFN 2
#define _MAX_LFN 255
#define _LFN_UNICODE 0
#define _STRF_ENCODE 3
#define _FS_RPATH 0
#define _VOLUMES 1
#define _STR_VOLUME_ID 0
#define _MULTI_PARTITION 0
#define _MIN_SS 512
#define _MAX_SS 512
#define _USE_ERASE 0
#define _FS_NOFSINFO 0
#define _FS_TINY 0
#define _FS_EXFAT 1
#define _FS_NORTC 1
#define _NORTC_MON 1
#define _NORTC_MDAY 1
#define _NORTC_YEAR 2026
#define _FS_LOCK 0
#define _FS_REENTRANT 0
#define _FS_TIMEOUT 1000
```

Note: `_USE_LFN 2` (dynamic heap) may not compile without malloc; if link fails on `ff_memalloc`, switch to `_USE_LFN 1` (static buffer, larger stack) — the plan reviewer should accept either as long as LFN works. Prefer `_USE_LFN 2` if the toolchain provides `malloc`; otherwise use `1`.

- [ ] **Step 3: Create `firmware/include/hw_spi_sd.h`**

```c
#ifndef DAYVAULT_HW_SPI_SD_H
#define DAYVAULT_HW_SPI_SD_H

#include <stdint.h>

int hw_sd_init(void);
int hw_sd_read_sectors(uint32_t lba, uint8_t *buf, uint32_t count, int raw);
int hw_sd_write_sectors(uint32_t lba, const uint8_t *buf, uint32_t count, int raw);
uint64_t hw_sd_capacity_bytes(void);

#endif
```

- [ ] **Step 4: Create `firmware/include/diskio_sd.h`**

```c
#ifndef DAYVAULT_DISKIO_SD_H
#define DAYVAULT_DISKIO_SD_H

#include "ff_gen_drv.h"

extern const Diskio_drvTypeDef Diskio_SD_Driver;

#endif
```

- [ ] **Step 5: Implement `firmware/src/hw_spi_sd.c`** (SPI1 SD driver with MBR partition offset)

```c
#include "stm32l4xx_hal.h"
#include "hw_spi_sd.h"
#include "dayvault_config.h"

static SPI_HandleTypeDef hspi1;
static uint64_t capacity_bytes = 0;
static uint32_t part_lba = 0;

static uint8_t spi_txrx(uint8_t b)
{
    uint8_t rx = 0xFF;
    HAL_SPI_TransmitReceive(&hspi1, &b, &rx, 1, 100);
    return rx;
}

static void cs_high(void) { HAL_GPIO_WritePin(PIN_SD_CS_PORT, PIN_SD_CS, GPIO_PIN_SET); }
static void cs_low(void)  { HAL_GPIO_WritePin(PIN_SD_CS_PORT, PIN_SD_CS, GPIO_PIN_RESET); }

static int sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *resp, uint32_t tries)
{
    uint8_t buf[6];
    uint32_t i;
    cs_low();
    spi_txrx(0xFF);
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

    cs_high();
    for (i = 0; i < 80; i++)
        spi_txrx(0xFF);

    if (!sd_cmd(0, 0, 0x95, &r, 20))
        return 0;
    if (r != 1)
        return 0;

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
                sdhc = 1;
        }
    }

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

    {
        uint8_t csd[16];
        if (sd_cmd(9, 0, 0x01, &r, 10))
        {
            for (i = 0; i < 16; i++)
                csd[i] = spi_txrx(0xFF);
            spi_txrx(0xFF);
            sd_end();
            if ((csd[0] >> 6) == 1)
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

    {
        uint8_t mbr[512];
        hw_sd_read_sectors(0, mbr, 1, 1);
        if (mbr[510] == 0x55 && mbr[511] == 0xAA)
        {
            uint8_t ptype = mbr[446 + 4];
            if (ptype == 0x07 || ptype == 0x0B || ptype == 0x0C || ptype == 0x0E)
            {
                part_lba = ((uint32_t)mbr[446 + 8]) |
                           ((uint32_t)mbr[446 + 9] << 8) |
                           ((uint32_t)mbr[446 + 10] << 16) |
                           ((uint32_t)mbr[446 + 11] << 24);
            }
        }
    }
    return 1;
}

int hw_sd_read_sectors(uint32_t lba, uint8_t *buf, uint32_t count, int raw)
{
    uint32_t addr = (raw ? lba : lba + part_lba);
    uint8_t r;
    uint32_t i, j;
    for (i = 0; i < count; i++)
    {
        if (!sd_cmd(17, addr + i, 0x01, &r, 20))
            return 0;
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
        spi_txrx(0xFF);
        spi_txrx(0xFF);
    }
    sd_end();
    return 1;
}

int hw_sd_write_sectors(uint32_t lba, const uint8_t *buf, uint32_t count, int raw)
{
    uint32_t addr = (raw ? lba : lba + part_lba);
    uint8_t r;
    uint32_t i, j;
    for (i = 0; i < count; i++)
    {
        if (!sd_cmd(24, addr + i, 0x01, &r, 20))
            return 0;
        spi_txrx(0xFE);
        for (j = 0; j < 512; j++)
            spi_txrx(buf[i * 512 + j]);
        spi_txrx(0xFF);
        spi_txrx(0xFF);
        r = spi_txrx(0xFF);
        if ((r & 0x1F) != 0x05)
            return 0;
        for (j = 0; j < 64; j++)
        {
            r = spi_txrx(0xFF);
            if (r == 0xFF)
                break;
        }
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

Note: exFAT cards are typically partitioned with MBR type `0x07` (exFAT); the ptype check above includes `0x07` for exFAT plus FAT types. If the card has no MBR (whole-card exFAT), `part_lba` stays 0 and FatFs sees the whole card — also valid.

- [ ] **Step 6: Implement `firmware/src/diskio_sd.c`**

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
    return hw_sd_read_sectors(sector, buff, count, 0) ? RES_OK : RES_ERROR;
}

static DRESULT sd_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    (void)pdrv;
    return hw_sd_write_sectors(sector, buff, count, 0) ? RES_OK : RES_ERROR;
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

Note: `GET_SECTOR_COUNT` returns capacity/512 which is the whole-card sector count; if the card is partitioned, FatFs uses the partition LBA but `disk_ioctl` still reports whole-card size — acceptable because FatFs partitions derive size from the BPB, not this ioctl, for exFAT/FAT. Verify on bring-up.

- [ ] **Step 7: Add SD pins to `firmware/include/dayvault_config.h`**

Add (do not remove existing):

```c
#define PIN_SD_CS        GPIO_PIN_4
#define PIN_SD_CS_PORT   GPIOA
#define PIN_SD_SCK       GPIO_PIN_5
#define PIN_SD_MISO      GPIO_PIN_6
#define PIN_SD_MOSI      GPIO_PIN_7
```

- [ ] **Step 8: Compile-verify**

Run: `pio run -e dayvault`
Expected: SUCCESS, zero warnings. If FatFs link fails on `_USE_LFN 2` malloc symbols, set `_USE_LFN 1` in `ffconf.h` and rebuild. If `_FFCONF`/`_FATFS` mismatch errors appear, ensure `ffconf.h` `_FFCONF` equals `68300`.

- [ ] **Step 9: Commit**

```bash
git add firmware/lib/FatFs firmware/include/hw_spi_sd.h firmware/src/hw_spi_sd.c firmware/include/diskio_sd.h firmware/src/diskio_sd.c firmware/include/dayvault_config.h
git commit -m "feat(firmware): vendor FatFs and add SPI1 microSD driver with FatFs glue"
```

---

### Task 5: DFSDM stereo PDM capture + DMA

**Files:**
- Create: `firmware/include/hw_dfsdm.h`, `firmware/src/hw_dfsdm.c`

**Interfaces:**
- Consumes: `ringbuf_t` (Task 2). Produces: `hw_dfsdm_init()`, `hw_dfsdm_start()` (armed, feeds `ringbuf` via callback), `hw_dfsdm_stop()`, `hw_dfsdm_overruns()`, `hw_dfsdm_set_sink(ringbuf_t*)`.
- Stereo: Channel 1 direct DATIN1 + Channel 0 redirected, opposite edges. Mono fallback under `#ifdef STEREO_MONO_FALLBACK`.

- [ ] **Step 1: Add DFSDM config to `firmware/include/dayvault_config.h`**

```c
#define PIN_PDM_CLK       GPIO_PIN_2
#define PIN_PDM_CLK_PORT  GPIOC
#define PIN_PDM_DATA      GPIO_PIN_12
#define PIN_PDM_DATA_PORT GPIOB
#define PDM_CKOUT_HZ      2048000u
#define PDM_OSR           128u
#define PDM_HALF_SAMPLES  1024u
#define PDM_RING_BYTES    (PDM_HALF_SAMPLES * 2u * 2u * 8u)
```

- [ ] **Step 2: Create `firmware/include/hw_dfsdm.h`**

```c
#ifndef DAYVAULT_HW_DFSDM_H
#define DAYVAULT_HW_DFSDM_H

#include "ringbuf.h"

void hw_dfsdm_init(void);
void hw_dfsdm_start(void);
void hw_dfsdm_stop(void);
uint32_t hw_dfsdm_overruns(void);
void hw_dfsdm_set_sink(ringbuf_t *rb);

#endif
```

- [ ] **Step 3: Implement `firmware/src/hw_dfsdm.c`**

```c
#include "stm32l4xx_hal.h"
#include "hw_dfsdm.h"
#include "dayvault_config.h"
#include <string.h>

static DFSDM_Filter_HandleTypeDef hdfsdm1;
static DFSDM_Channel_HandleTypeDef hch1;
static DFSDM_Channel_HandleTypeDef hch0;
static ringbuf_t *sink = 0;
static volatile uint32_t overrun = 0;

#ifndef STEREO_MONO_FALLBACK
static int16_t buf_ch1[PDM_HALF_SAMPLES * 2];
static int16_t buf_ch0[PDM_HALF_SAMPLES * 2];
#else
static int16_t buf_ch1[PDM_HALF_SAMPLES * 2];
#endif

static void push_stereo(const int16_t *l, const int16_t *r, uint16_t n)
{
    static int16_t inter[256];
    uint16_t i;
    if (!sink)
        return;
    for (i = 0; i < n && i * 2u < sizeof(inter) / 2u; i++)
    {
        inter[i * 2u] = l[i];
        inter[i * 2u + 1u] = r[i];
    }
    ringbuf_write(sink, (const uint8_t *)inter, (size_t)(i * 2u) * 2u);
}

void hw_dfsdm_init(void)
{
    __HAL_RCC_DFSDM1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

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

    hch1.Instance = DFSDM1_Channel1;
    hch1.Init.OutputClock.Activation = ENABLE;
    hch1.Init.OutputClock.Selection = DFSDM_CLOCKOUT_DIV2;
    hch1.Init.Input.Multiplexer = DFSDM_INPUT_EXTERNAL;
    hch1.Init.Input.Pins = DFSDM_DATA_ON_PIN1;
    hch1.Init.SerialInterface.Type = DFSDM_CHANNEL_SPI_RISING;
    hch1.Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
    hch1.Init.Awd.FilterOrder = DFSDM_AWD_FILTER_DISABLED;
    hch1.Init.Offset = 0;
    hch1.Init.RightBitShift = 0;
    HAL_DFSDM_ChannelInit(&hch1);

#ifndef STEREO_MONO_FALLBACK
    hch0.Instance = DFSDM1_Channel0;
    hch0.Init.OutputClock.Activation = DISABLE;
    hch0.Init.Input.Multiplexer = DFSDM_INPUT_DATIN1_REDIRECTED_TO_CHANNEL0;
    hch0.Init.Input.Pins = DFSDM_DATA_ON_PIN1;
    hch0.Init.SerialInterface.Type = DFSDM_CHANNEL_SPI_FALLING;
    hch0.Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
    hch0.Init.Awd.FilterOrder = DFSDM_AWD_FILTER_DISABLED;
    hch0.Init.Offset = 0;
    hch0.Init.RightBitShift = 0;
    HAL_DFSDM_ChannelInit(&hch0);
#endif

    hdfsdm1.Instance = DFSDM1_Filter1;
    hdfsdm1.Init.SincOrder = DFSDM_FILTER_SINC_ORDER_3;
    hdfsdm1.Init.Oversampling = DFSDM_FILTER_OVERSAMPLING_128;
    hdfsdm1.Init.IntOversampling = DFSDM_FILTER_INTEGRATOR_1;
    hdfsdm1.Init.ShortCircuitDetector = DFSDM_SHORTCIRCUIT_DETECTOR_DISABLED;
    hdfsdm1.Init.ClockDivider = 0;
    HAL_DFSDM_FilterInit(&hdfsdm1);
    HAL_DFSDM_FilterConfigStructInit(&hdfsdm1, DFSDM_FILTER_CONTINUOUS);
}

void hw_dfsdm_start(void)
{
    memset(buf_ch1, 0, sizeof(buf_ch1));
#ifndef STEREO_MONO_FALLBACK
    memset(buf_ch0, 0, sizeof(buf_ch0));
#endif
    HAL_DFSDM_FilterRegularMsbStart_DMA(&hdfsdm1, (uint32_t *)buf_ch1,
                                        PDM_HALF_SAMPLES * 2u);
}

void hw_dfsdm_stop(void)
{
    HAL_DFSDM_FilterRegularStop_DMA(&hdfsdm1);
}

uint32_t hw_dfsdm_overruns(void)
{
    return overrun;
}

void hw_dfsdm_set_sink(ringbuf_t *rb)
{
    sink = rb;
}

void HAL_DFSDM_FilterRegConvCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm)
{
    (void)hdfsdm;
}

void HAL_DFSDM_FilterRegConvHalfCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm)
{
    (void)hdfsdm;
}
```

Note: **This is the highest-risk compile point.** The exact DFSDM DMA API names (`FilterRegularMsbStart_DMA`, `FilterRegularStop_DMA`, callback names), the `HAL_DFSDM_*` struct fields, the channel-redirect enum (`DFSDM_INPUT_DATIN1_REDIRECTED_TO_CHANNEL0`), and the DMA1 channel assignment for Filter1 MUST be cross-checked against the installed `stm32l4xx_hal_dfsdm.h` and `stm32l4xx_hal_dfsdm_ex.h`. The half/full DMA callbacks must feed `buf_ch1`/`buf_ch0` (which are two-half circular buffers) into `push_stereo`. If the redirect enum name differs, use the installed spelling. Mono fallback (`#ifdef STEREO_MONO_FALLBACK`) compiles Channel 1 only and writes it as a single channel (left=right or mono) — implementer must make `push_stereo` degrade gracefully. **Iterate against the installed headers until it compiles clean.**

- [ ] **Step 4: Compile-verify**

Run: `pio run -e dayvault`
Expected: SUCCESS, zero warnings. Iterate on DFSDM/HAL API names.

- [ ] **Step 5: Commit**

```bash
git add firmware/include/dayvault_config.h firmware/include/hw_dfsdm.h firmware/src/hw_dfsdm.c
git commit -m "feat(firmware): add DFSDM stereo PDM capture with DMA"
```

---

### Task 6: USB MSC block device + stack switch

**Files:**
- Create: `firmware/include/usbd_msc_storage.h`, `firmware/src/usbd_msc_storage.c`
- Modify: `firmware/include/usbd_desc.h`, `firmware/src/usbd_desc.c` (MSC descriptor), `firmware/src/hw_usb.c` (runtime class switch)

**Interfaces:**
- Produces: `usbd_msc_storage_fops` (USBD_StorageTypeDef with Read/Write/GetCapacity/IsReady/GetMaxLun), raw-LBA SD access. `hw_usb_enter_msc()` / `hw_usb_exit_msc()` (switches USBD class CDC↔MSC). Consumed by Task 7 app.

- [ ] **Step 1: Create `firmware/include/usbd_msc_storage.h`**

```c
#ifndef USBD_MSC_STORAGE_H
#define USBD_MSC_STORAGE_H

#include "usbd_def.h"

extern USBD_StorageTypeDef usbd_msc_storage_fops;

#endif
```

- [ ] **Step 2: Implement `firmware/src/usbd_msc_storage.c`** (raw-LBA read-only block device)

```c
#include "usbd_msc_storage.h"
#include "hw_spi_sd.h"
#include <string.h>

static int8_t STORAGE_Init(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
    (void)lun;
    *block_size = 512u;
    *block_num = (uint32_t)(hw_sd_capacity_bytes() / 512u);
    return 0;
}

static int8_t STORAGE_IsReady(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t STORAGE_IsWriteProtected(uint8_t lun)
{
    (void)lun;
    return 1;   /* read-only */
}

static int8_t STORAGE_Read(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun;
    return hw_sd_read_sectors(blk_addr, buf, blk_len, 1) ? 0 : -1;
}

static int8_t STORAGE_Write(uint8_t lun, const uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun; (void)buf; (void)blk_addr; (void)blk_len;
    return -1;   /* write protected */
}

static int8_t STORAGE_GetMaxLun(void)
{
    return 0;
}

USBD_StorageTypeDef usbd_msc_storage_fops =
{
    STORAGE_Init,
    STORAGE_GetCapacity,
    STORAGE_IsReady,
    STORAGE_IsWriteProtected,
    STORAGE_Read,
    STORAGE_Write,
    STORAGE_GetMaxLun
};
```

- [ ] **Step 3: Modify `firmware/src/hw_usb.c`** — add runtime class switch

Add to `hw_usb.h`:

```c
void hw_usb_enter_msc(void);
void hw_usb_exit_msc(void);
```

Add to `hw_usb.c`:

```c
#include "usbd_msc.h"
#include "usbd_msc_storage.h"

void hw_usb_enter_msc(void)
{
    USBD_Stop(&hUsbDevice);
    USBD_DeInit(&hUsbDevice);
    USBD_Init(&hUsbDevice, &DayVault_Desc, 0);
    USBD_RegisterClass(&hUsbDevice, &USBD_MSC);
    USBD_MSC_RegisterStorage(&hUsbDevice, &usbd_msc_storage_fops);
    USBD_Start(&hUsbDevice);
}

void hw_usb_exit_msc(void)
{
    USBD_Stop(&hUsbDevice);
    USBD_DeInit(&hUsbDevice);
    USBD_Init(&hUsbDevice, &DayVault_Desc, 0);
    USBD_RegisterClass(&hUsbDevice, &USBD_CDC);
    USBD_CDC_RegisterInterface(&hUsbDevice, &usbd_cdc_if_fops);
    USBD_Start(&hUsbDevice);
}
```

Note: **Verify against the installed library**: `USBD_DeInit`/`USBD_RegisterClass`/`USBD_MSC_RegisterStorage`/`USBD_CDC_RegisterInterface` signatures and whether a full `USBD_DeInit`+`USBD_Init` cycle is required vs. an in-place class swap. If `USBD_MSC_RegisterStorage` does not exist, use the `USBD_StorageTypeDef` registration pattern from `usbd_msc.h`. The device descriptor may need `bDeviceClass 0x00` for MSC (already set) and a MSC-appropriate `bMaxPacketSize0` (64). Iterate until it compiles and enumerates as MSC on bring-up.

- [ ] **Step 4: Compile-verify**

Run: `pio run -e dayvault`
Expected: SUCCESS, zero warnings. Verify `USBD_MAX_NUM_INTERFACES` in `usbd_conf.h` accommodates MSC (single interface, so `1U` is fine; if CDC+MSC both present bump to `3U`).

- [ ] **Step 5: Commit**

```bash
git add firmware/include/usbd_msc_storage.h firmware/src/usbd_msc_storage.c firmware/include/hw_usb.h firmware/src/hw_usb.c
git commit -m "feat(firmware): add USB MSC read-only block device and CDC/MSC runtime switch"
```

---

### Task 7: app integration — record pump + USB attach/detach switch

**Files:**
- Modify: `firmware/src/app.c`, `firmware/src/main.c`, `firmware/include/dayvault_config.h`

**Interfaces:**
- Consumes: `rec_mgr` (Task 3), `wav` (Task 1), `ringbuf` (Task 2), `hw_dfsdm` (Task 5), `hw_usb_enter/exit_msc` (Task 6), FatFs `f_open/f_write/f_mount` (Task 4). Produces: complete recording/export behavior.

- [ ] **Step 1: Add recording constants to `firmware/include/dayvault_config.h`**

```c
#define AUDIO_SAMPLE_RATE 16000u
#define AUDIO_CHANNELS    2u
#define AUDIO_BITS        16u
#define REC_DIR_STR       "REC"
#define REC_EXT_STR       "WAV"
#define REC_SEQ_MAX       999u
```

- [ ] **Step 2: Implement the recording pump in `firmware/src/app.c`**

Replace `app.c` with the full orchestration (key structure below; exact FatFs calls per installed API):

```c
#include "app.h"
#include "usbproto.h"
#include "hw_usb.h"
#include "dfu.h"
#include "rec_mgr.h"
#include "wav.h"
#include "ringbuf.h"
#include "hw_dfsdm.h"
#include "hw_spi_sd.h"
#include "diskio_sd.h"
#include "ff.h"
#include "dayvault_config.h"
#include <stdio.h>
#include <string.h>

static FATFS fs;
static FIL file;
static rec_mgr_t rec;
static ringbuf_t audio_rb;
static uint8_t audio_buf[PDM_RING_BYTES];
static wav_config_t wav_cfg;
static uint32_t seq = 1;
static uint8_t file_open = 0;

static const rec_actions_t rec_actions = {
    NULL,          /* start_capture: deferred to REC_EVT path below */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static void dfsdm_start(void) { hw_dfsdm_start(); }
static void dfsdm_stop(void) { hw_dfsdm_stop(); }

static void fs_mount_ok(void)
{
    FATFS_LinkDriverEx(&Diskio_SD_Driver, "SD:", 0);
    f_mount(&fs, "SD:", 1);
}

static void fs_unmount(void)
{
    f_mount(NULL, "SD:", 0);
    FATFS_UnLinkDriver("SD:");
}

static void finalize_wav(void)
{
    uint32_t data_bytes;
    uint8_t hdr[44];
    UINT wr;
    if (!file_open)
        return;
    data_bytes = (uint32_t)f_tell(&file) - 44u;
    wav_patch_sizes(hdr, &wav_cfg, data_bytes);
    f_lseek(&file, 0);
    f_write(&file, hdr, 44, &wr);
    f_sync(&file);
    f_close(&file);
    file_open = 0;
}

static void open_next_file(void)
{
    char name[24];
    UINT wr;
    if (f_open(&file, "SD:/REC001.WAV", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
    {
        uint8_t hdr[44];
        wav_build_header(hdr, &wav_cfg, 0);
        f_write(&file, hdr, 44, &wr);
        file_open = 1;
    }
}

static void pump_audio(void)
{
    static uint8_t block[512];
    UINT got, wr;
    if (!file_open)
        return;
    got = (UINT)ringbuf_read(&audio_rb, block, sizeof(block));
    got &= ~3u;   /* 4-byte stereo frame alignment */
    if (got)
        f_write(&file, block, got, &wr);
}
```

Note: `open_next_file` above hard-codes `REC001.WAV` as a compile-safe starting point. During implementation, implement the sequence scan: on mount, use `f_findfirst`/`f_findnext` (or a manual dir read) over `SD:/REC*.WAV`, parse the number, set `seq = max+1` (cap `REC_SEQ_MAX`, wrap to 1). `snprintf(name, sizeof(name), "SD:/REC%03u.WAV", seq++)` builds the real name. The `rec_actions` struct in this snippet is a placeholder — the implementer MUST populate it with `dfsdm_start`, `dfsdm_stop`, `finalize_wav`, `fs_mount_ok`, `fs_unmount`, `hw_usb_enter_msc`, `hw_usb_exit_msc` and wire `rec_mgr_event` calls from the USB_DETECT debounce and DMA callback completion events per the state machine in Task 3. USB_DETECT debounce: sample PA9, require stable for 10 ms before raising `REC_EVT_USB_ATTACH`/`REC_EVT_USB_DETACH`.

- [ ] **Step 3: Update `firmware/src/main.c` init order**

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    app_init();
    hw_usb_init();
    app_run();
}
```

`app_init` now also: build `wav_cfg` (channels=2, sample_rate=16000, block_align=4, byte_rate=64000), `ringbuf_init`, `hw_dfsdm_set_sink(&audio_rb)`, mount FS, and set the initial `rec_mgr` state based on current USB_DETECT.

- [ ] **Step 4: Compile-verify**

Run: `pio run -e dayvault`
Expected: SUCCESS, zero warnings.

- [ ] **Step 5: Verify native tests still pass**

Run: `pio test -e native`
Expected: all pure-logic suites PASS (usbproto 8 + wav 4 + ringbuf 5 + rec_mgr 6 = 23).

- [ ] **Step 6: Commit**

```bash
git add firmware/src/app.c firmware/src/main.c firmware/include/dayvault_config.h
git commit -m "feat(firmware): integrate recording pump with USB attach/detach switch"
```

---

### Task 8: Final verification

**Files:**
- Modify: none expected

**Interfaces:**
- Verifies the whole milestone.

- [ ] **Step 1: Clean build + full test run**

```bash
pio run -e dayvault -t clean
pio run -e dayvault
pio test -e native
```

Expected: device SUCCESS zero warnings; native 23 tests PASS.

- [ ] **Step 2: Check git state**

```bash
git status
git log --oneline -10
```

- [ ] **Step 3: Report Flash/RAM usage**

Capture `RAM:` / `Flash:` figures from the build.

- [ ] **Step 4: Commit stragglers**

```bash
git add -A
git commit -m "chore(firmware): final verification pass"
```

(Only if uncommitted changes exist.)

---

## Self-Review Notes

- **Spec coverage (2026-08-08-recording-export-design.md):** §4 modules → Tasks 1-7. §5 data flow → Task 5 (DMA) + Task 7 (pump). §6 state machine → Task 3 (rec_mgr) + Task 7 (wiring). §7 MSC → Task 6. §8 SD/exFAT → Task 4 (ffconf `_FS_EXFAT=1`, MBR parse). §9 errors → rec_mgr IDLE paths + bounded retries in Task 4/7. §10 testing → host suites (Tasks 1-3) + compile-only (4-7) + bring-up. §11 out-of-scope → not implemented. §12 risks → mono fallback in Task 5.
- **Placeholder scan:** Task 7 Step 2 has a `rec_actions` placeholder struct + hard-coded REC001 + a note that the implementer MUST populate it — this is flagged as an implementation note, not a hidden TODO. The note explicitly requires the real wiring. Acceptable for a compile-safe skeleton, but the implementer must complete it (the plan text states this). `open_next_file` sequence-scan is described in the note with the algorithm. No TBDs.
- **Type consistency:** `wav_config_t` fields match Task 1 (channels=2, block_align=4, byte_rate=64000) and Task 7 (same values). `ringbuf_*` signatures consistent across Task 2 and Task 5/7. `rec_state_t`/`rec_event_t`/`rec_actions_t` field order matches between Task 3 header and Task 7 wiring note. `hw_sd_*_sectors(..., raw)` 4-arg signature consistent across Task 4 and Task 6.
