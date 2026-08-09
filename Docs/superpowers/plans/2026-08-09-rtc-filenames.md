# RTC Timestamped Recording Filenames Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Name recordings by local start time + duration (e.g. `REC-20260809-1110_5m32s.WAV`), crash-safe against power loss, with PC-synced timezone and FatFs long-filename support.

**Architecture:** Extend `DeviceTime` with a backup-domain UTC-offset (`BKP1R`) + `time_set` flag and local-time rendering. Rework `rec_start`/`rec_stop` in `main.cpp` to create timestamp names (with `_n` collision retry and sequence-name fallback when time is unset) and rename on stop to include duration. Add periodic WAV-header + `f_sync` checkpointing (~1 s) so a power loss leaves a playable, correctly-sized file, and finalize the recording cleanly before low-battery STOP. Verify FatFs LFN long names first and trim `_MAX_LFN` for stack headroom.

**Tech Stack:** STM32L452RC, STM32duino Arduino core, FatFs (`lib/FatFs`, `_USE_LFN 2`), PDM/DFSDM audio, PlatformIO (`pio run -e dayvault`).

## Global Constraints

- **NEVER touch FLASH option bytes or `dfu-util` writes to `0x1FFF7800`** (brick risk, AGENTS.md HARD RULE). Enter DFU only via the firmware `DFU` serial command or BOOT+RST.
- USB VID must stay ST `0x0483`.
- The RTC calendar stores **UTC**; the UTC offset is applied **only at render time** (filenames, `INFO`). `dt_get_unix` stays true UTC.
- Recording must never be blocked or lose data because of naming: long-name failure falls back to sequence names; `f_rename` failure keeps the start-time name.
- FatFs `ffconf.h` `_USE_LFN 2` is unchanged; `_MAX_LFN` reduced to 40; long filenames must actually work.
- Default UTC offset = `+480` minutes (UTC+8, current dev PC); default `time_set` = 0.
- Every command/behavior change is documented in `Docs/Serial-Command-Reference.md`.
- Audio pipeline (PDM/DFSDM, `pdm_*`, `wav_build_header`) and low-battery sleep logic (hysteresis 3.0/3.3 V, 4 s RTC wake, IWDG kicks) are untouched except where the plan says.

---

## File Structure

- `firmware/lib/FatFs/ffconf.h` — `_MAX_LFN` 255 → 40 (stack headroom).
- `firmware/src/Fs.cpp` — unchanged (no `ff_memalloc` needed; FatFs R0.12c uses stack buffers for `_USE_LFN 2`).
- `firmware/src/DeviceTime.h` — declare `dt_time_is_set`, `dt_get_tz`, `dt_set_tz`, `dt_format_local`, `dt_format_stem`.
- `firmware/src/DeviceTime.cpp` — implement the above + `time_set` set in `dt_set_unix`, `BKP1R` default init, `dt_format_local`/`dt_format_stem` rendering.
- `firmware/src/main.cpp` — timestamp filenames in `rec_start`/`rec_stop`, collision retry, sequence fallback, rename-with-duration, checkpointing, sleep-entry finalize, `LTEST` command, `SETTZ` command, `SETTIME` offset arg, `INFO`/`TIME`/`CHECK`/`REC` message updates.
- `Docs/Serial-Command-Reference.md` — document all command and behavior changes.

Build: `pio run -e dayvault` (run in `firmware/`). Flash: serial `DFU` command, or BOOT+RST + `dfu-util -a 0 -s 0x08000000:leave -D .pio/build/dayvault/firmware.bin`. Serial check: COM9 @115200.

---

### Task 1: FatFs LFN verify + stack headroom + `LTEST` command

**Files:**
- Modify: `firmware/lib/FatFs/ffconf.h` (reduce `_MAX_LFN` 255 → 40)
- Modify: `firmware/src/main.cpp` (add `LTEST` handler + `lfn_bringup_test()`)
- Verify: SD card long-name behavior

**Interfaces:**
- Consumes: existing `fs_mount_result()`, `f_open`, `f_write`, `f_close`, `f_opendir`, `f_readdir`, `f_rename`, `f_unlink` (all FatFs, already used).
- Produces: serial command `LTEST` — proves long-name create / readdir / rename / delete work.

Context: FatFs is R0.12c. `_USE_LFN 2` = LFN with a **dynamic working buffer on the stack** (`WCHAR lbuf[_MAX_LFN+1]` local array + exFAT `dirbuf`); `ff_memalloc` applies only to `_USE_LFN 3`, so **no allocation wrappers are needed**. The long-name mechanism already exists; this task verifies it and trims the oversized stack buffer.

- [ ] **Step 1: Reduce `_MAX_LFN` to 40**

In `firmware/lib/FatFs/ffconf.h`, change line 15 from:

```c
#define _MAX_LFN 255
```

to:

```c
#define _MAX_LFN 40
```

Rationale: with `_FS_EXFAT 1`, `_USE_LFN 2` puts `lbuf[256]` (512 B) + `dirbuf[608 B]` on the stack per LFN directory/rename call. `_MAX_LFN 40` → `lbuf[41]` (82 B) + `dirbuf[160 B]` ≈ 242 B, plenty for our longest generated name (~31 chars: `REC-20260809-1110_2_1h23m45s.WAV`). `ffconf.h` requires `_MAX_LFN >= 12`.

- [ ] **Step 2: Rebuild and confirm no `ff_memalloc` is involved**

```powershell
pio run -e dayvault
```

Expected: build succeeds. `_MAX_LFN 40` compiles (`ffconf.h` validates `12..255`). No `ff_memalloc`/`ff_memfree` symbols are expected in `libFatFs.a` (they belong to `_USE_LFN 3`) — that is normal.

- [ ] **Step 3: Add the `LTEST` diagnostic command**

In `firmware/src/main.cpp`, add this static function (place it near `check_wav_file`):

```cpp
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
```

Add the command handler in the serial dispatch chain (next to `LIST`, around `main.cpp:525`):

```cpp
} else if (strncmp(line, "LTEST", 5) == 0) {
    lfn_bringup_test();
}
```

- [ ] **Step 4: Flash and verify LFN works on the SD card**

Flash (serial `DFU` command or BOOT+RST). Then send `LTEST` over COM9 @115200 and confirm:

```
LTEST mount=0 create_fr=0 write_fr=0 readdir_found=1 rename_fr=0 unlink_fr=0
```

`create_fr=0`, `readdir_found=1`, `rename_fr=0`, `unlink_fr=0` prove long-name create / readdir / rename / delete all work with the stack-buffer LFN config. Run `LIST` to confirm the test file is gone.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/FatFs/ffconf.h firmware/src/main.cpp
git commit -m "feat(fs): verify FatFs LFN long names, trim _MAX_LFN 255->40 for stack headroom, add LTEST diagnostic"
```

---

### Task 2: DeviceTime timezone offset + `time_set` + local-time rendering

**Files:**
- Modify: `firmware/src/DeviceTime.h`
- Modify: `firmware/src/DeviceTime.cpp`
- Modify: `firmware/src/main.cpp` (`SETTIME`, new `SETTZ`, `INFO`, `TIME`)

**Interfaces:**
- Consumes: existing `dt_init`, `dt_set_unix`, `dt_get_unix`, `dt_format`; `BKP0R` magic pattern; write-protection dance.
- Produces:
  - `bool dt_time_is_set(void)` — true after the first `SETTIME`.
  - `int32_t dt_get_tz(void)` — UTC offset in minutes (default +480).
  - `void dt_set_tz(int32_t minutes)` — store offset (keeps `time_set` flag).
  - `void dt_format_local(char* buf, size_t len)` — `"YYYY-MM-DD HH:MM:SS"` in local time.
  - `void dt_format_stem(char* buf, size_t len)` — `"YYYYMMDD-HHMM"` local, minute precision, for filenames (used by Task 3).
  - `dt_set_unix(uint32_t)` now also sets `time_set`.

- [ ] **Step 1: Extend the header**

In `firmware/src/DeviceTime.h`, replace the whole file content with:

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>
void dt_init(void);
void dt_set_unix(uint32_t unix);
uint32_t dt_get_unix(void);
void dt_format(char* buf, size_t len);
void dt_format_local(char* buf, size_t len);   /* "YYYY-MM-DD HH:MM:SS" in local time */
void dt_format_stem(char* buf, size_t len);    /* "YYYYMMDD-HHMM" local, for filenames */
bool dt_time_is_set(void);
int32_t dt_get_tz(void);
void dt_set_tz(int32_t minutes);
void dt_set_wake(uint16_t seconds);            /* schedule RTC wake-up timer, periodic */
void dt_wake_off(void);                        /* deactivate RTC wake-up timer */
```

- [ ] **Step 2: Add backup-domain storage + default init**

In `firmware/src/DeviceTime.cpp`, add defines after line 7 (`DT_RTC_SYNCH_PREDIV`):

```cpp
#define DT_BKP_TZ         RTC->BKP1R
#define DT_TZ_SET_BIT     0x80000000u
#define DT_DEFAULT_TZ     480                  /* minutes, UTC+8 */
```

In `dt_init`, extend the fresh-board block so `BKP1R` is also initialized (default offset, time NOT set). Replace:

```cpp
    if (RTC->BKP0R != DT_MAGIC) {
        __HAL_RTC_WRITEPROTECTION_DISABLE(&s_hrtc);
        RTC->BKP0R = DT_MAGIC;
        __HAL_RTC_WRITEPROTECTION_ENABLE(&s_hrtc);
        rtc_store(days_from_civil(2026, 1, 1) * 86400u);   /* 2026-01-01 00:00:00 UTC */
    }
```

with:

```cpp
    if (RTC->BKP0R != DT_MAGIC) {
        __HAL_RTC_WRITEPROTECTION_DISABLE(&s_hrtc);
        RTC->BKP0R = DT_MAGIC;
        RTC->BKP1R = (uint32_t)DT_DEFAULT_TZ;              /* offset only, time_set=0 */
        __HAL_RTC_WRITEPROTECTION_ENABLE(&s_hrtc);
        rtc_store(days_from_civil(2026, 1, 1) * 86400u);   /* 2026-01-01 00:00:00 UTC */
    }
```

- [ ] **Step 3: Implement the accessors + `time_set` in `dt_set_unix`**

Add after `dt_set_unix` (which becomes):

```cpp
void dt_set_unix(uint32_t unix)
{
    rtc_store(unix);
    __HAL_RTC_WRITEPROTECTION_DISABLE(&s_hrtc);
    RTC->BKP1R |= DT_TZ_SET_BIT;
    __HAL_RTC_WRITEPROTECTION_ENABLE(&s_hrtc);
}

bool dt_time_is_set(void)
{
    return (RTC->BKP1R & DT_TZ_SET_BIT) != 0;
}

int32_t dt_get_tz(void)
{
    /* bits [30:0] hold the offset (two's complement, sign-extended from bit 30) */
    uint32_t v = RTC->BKP1R & 0x7FFFFFFFu;
    return (int32_t)((int32_t)(v << 1) >> 1);
}

void dt_set_tz(int32_t minutes)
{
    uint32_t set = RTC->BKP1R & DT_TZ_SET_BIT;
    __HAL_RTC_WRITEPROTECTION_DISABLE(&s_hrtc);
    RTC->BKP1R = set | ((uint32_t)minutes & 0x7FFFFFFFu);
    __HAL_RTC_WRITEPROTECTION_ENABLE(&s_hrtc);
}
```

- [ ] **Step 4: Implement local-time rendering**

Add after `dt_format`:

```cpp
static void civil_from_local(int64_t local, int* y, unsigned* m, unsigned* d, uint32_t* sod)
{
    uint32_t days = (uint32_t)(local / 86400);
    civil_from_days(days, y, m, d);
    *sod = (uint32_t)(local % 86400);
}

void dt_format_local(char* buf, size_t len)
{
    if (buf == NULL) return;
    if (len < 20u) { if (len > 0u) buf[0] = 0; return; }
    int64_t local = (int64_t)dt_get_unix() + (int64_t)dt_get_tz() * 60;
    int y; unsigned m, d; uint32_t sod;
    civil_from_local(local, &y, &m, &d, &sod);
    snprintf(buf, len, "%04d-%02u-%02u %02u:%02u:%02u",
             y, m, d, sod / 3600u, (sod / 60u) % 60u, sod % 60u);
}

void dt_format_stem(char* buf, size_t len)
{
    if (buf == NULL) return;
    if (len < 16u) { if (len > 0u) buf[0] = 0; return; }
    int64_t local = (int64_t)dt_get_unix() + (int64_t)dt_get_tz() * 60;
    int y; unsigned m, d; uint32_t sod;
    civil_from_local(local, &y, &m, &d, &sod);
    snprintf(buf, len, "%04u%02u%02u-%02u%02u",
             (unsigned)y, m, d, sod / 3600u, (sod / 60u) % 60u);
}
```

`civil_from_days` is already a static helper in this file (line 21). `int64_t` requires `<stdint.h>` (included).

- [ ] **Step 5: Wire the commands in `main.cpp`**

Replace the `SETTIME` handler (`main.cpp:705-710`) with:

```cpp
} else if (strncmp(line, "SETTIME ", 8) == 0) {
    char* sp = strchr(line + 8, ' ');
    uint32_t unix;
    int32_t tz = INT32_MIN;
    if (sp != NULL) { *sp = 0; tz = (int32_t)strtol(sp + 1, NULL, 10); }
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
}
```

Update the `TIME` diagnostic (`main.cpp:711-719`) to also print the backup register — append before the final `Serial.println();`:

```cpp
                        Serial.print(" BKP1="); Serial.print(RTC->BKP1R, HEX);
```

Update `INFO` (`main.cpp:720-735`): change `dt_format` to `dt_format_local` and add the new fields. Replace:

```cpp
                        char tb[32];
                        dt_format(tb, sizeof(tb));
                        Serial.print(" time="); Serial.print(tb);
                        Serial.print(" bat="); Serial.print(bat_millivolts()); Serial.print("mV");
                        Serial.print(" pct="); Serial.print(bat_percent());
                        Serial.println();
```

with:

```cpp
                        char tb[32];
                        dt_format_local(tb, sizeof(tb));
                        Serial.print(" time="); Serial.print(tb);
                        Serial.print(" bat="); Serial.print(bat_millivolts()); Serial.print("mV");
                        Serial.print(" pct="); Serial.print(bat_percent());
                        Serial.print(" tz="); Serial.print(dt_get_tz());
                        Serial.print(" time_set="); Serial.print(dt_time_is_set() ? 1 : 0);
                        Serial.println();
```

- [ ] **Step 6: Build, flash, verify**

```powershell
pio run -e dayvault
```

Flash, then over COM9:

```
SETTIME 1786273834 480        -> TIME set to <local time 2026-08-09 ...>
INFO                          -> ... time=<local> tz=480 time_set=1 ...
SETTZ -120                    -> TZ set to -120
SETTIME 1786273834            -> (no offset arg) offset kept at -120
SETTZ 480                     -> restore
TIME                          -> ... BKP1=... shows bit31 set
```

Verify: `time=` equals local wall-clock (UTC+8), `tz=480 time_set=1`, omitting the offset keeps the previous offset, and `BKP1` bit 31 is set.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/DeviceTime.h firmware/src/DeviceTime.cpp firmware/src/main.cpp
git commit -m "feat(rtc): backup-domain timezone offset + time_set, local-time formatting, SETTZ and SETTIME tz arg"
```

---

### Task 3: Timestamped filenames + duration rename + fallback + CHECK

**Files:**
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Consumes: `dt_time_is_set()`, `dt_format_stem()` (Task 2); `fs_next_sequence()`, `wav_build_header`, `rec_cfg`, `REC_DIR_STR`/`REC_EXT_STR`.
- Produces: `rec_start` creates `0:/REC-YYYYMMDD-HHMM[_n].WAV` (or sequence name); `rec_stop` renames to include `_MmSs`/`_HhMmSs`; `CHECK` finds the newest recording by name; `REC started` message includes the filename.

- [ ] **Step 1: Add helpers + state**

Near the rec_* statics (`main.cpp:326-335`), add:

```cpp
static char rec_name[40];          /* current file path (set in rec_start) */
static uint8_t rec_name_kind = 0;  /* 0 = seq fallback, 1 = timestamp */
```

Add these static functions above `rec_start`:

```cpp
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
```

- [ ] **Step 2: Rework `rec_start`**

Replace the body of `rec_start` (`main.cpp:346-367`) so the filename comes first and the header is synced:

```cpp
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
    rec_active = true;
}
```

The `char name[24]` local is gone; `rec_name` (40 bytes) holds the path. `FA_CREATE_NEW` fails with `FR_EXIST` when the minute name is taken, so the `_1`..`_9` suffix retries handle the same-minute collision; any other failure also falls through to the sequence path (never blocks recording).

- [ ] **Step 3: Rework `rec_stop` to rename with duration**

In `rec_stop` (`main.cpp:371-401`), after `wav_build_header`/`f_lseek`/`f_write`/`f_sync` and **before** `f_close`, insert the rename block:

```cpp
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
```

(`f_rename` failure keeps the start-time name — no data loss. The existing `fs_unmount(); rec_active = false;` and the AUTO-stop message stay unchanged.)

- [ ] **Step 4: Update the `REC` command message**

Replace `main.cpp:588-590`:

```cpp
} else if (strncmp(line, "REC", 3) == 0 && line[3] != ' ') {
    rec_start();
    Serial.print("REC started seq="); Serial.println(rec_seq);
}
```

with:

```cpp
} else if (strncmp(line, "REC", 3) == 0 && line[3] != ' ') {
    rec_start();
    Serial.print("REC started seq="); Serial.print(rec_seq);
    Serial.print(" name="); Serial.println(rec_name);
}
```

- [ ] **Step 5: Rewrite `CHECK` to find the newest recording by name**

Replace the body of `check_wav_file` (`main.cpp:118-...`) with a version that scans for the lexicographically-largest (per `rec_name_cmp`) `REC*.WAV` and then runs the existing header checks on it:

```cpp
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
            if (best[0] == 0 || rec_name_cmp(fno.fname, best) > 0) strncpy(best, fno.fname, sizeof(best) - 1);
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
```

(This preserves the diagnostic spirit of the old `CHECK` — new functions may also be called after power-loss drills in Task 4.)

- [ ] **Step 6: Build, flash, verify**

```powershell
pio run -e dayvault
```

Flash. Ensure time is set (`SETTIME <unix> 480`). Then:

1. `REC` → reply includes `name=0:/REC-20260809-XXXX.WAV`.
2. `STOP` → `LIST` shows `REC-20260809-XXXX_MmSs.WAV` (renamed, duration present).
3. Start a second recording in the same minute → `STOP` → `LIST` shows `_1`-suffixed start name → duration-renamed.
4. `CHECK` → `CHECK file=0:/REC-...` newest (timestamp preferred over old `REC###`).

- [ ] **Step 7: Commit**

```bash
git add firmware/src/main.cpp
git commit -m "feat(rec): timestamped recording filenames with duration rename, collision suffix, seq fallback; CHECK newest-by-name"
```

---

### Task 4: Power-loss / crash-safe checkpointing + clean sleep finalize

**Files:**
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Consumes: `rec_active`, `rec_data_bytes`, `rec_cfg`, `rec_name`, `wav_build_header`, `rec_flush_chunk` (flush path updates `rec_data_bytes`).
- Produces: `rec_checkpoint()` — periodic header + `f_sync`; `rec_start` header already `f_sync`d (Task 3); `low_battery_enter_stop()` finalizes an active recording before STOP.

- [ ] **Step 1: Add checkpoint state + function**

Near the rec_* statics, add:

```cpp
#define REC_SYNC_INTERVAL_MS 1000u
static uint32_t rec_last_sync_ms = 0;
```

Add after `rec_poll_samples`:

```cpp
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
```

In `rec_start`, right after `rec_start_ms = millis();`, add:

```cpp
    rec_last_sync_ms = millis();
```

- [ ] **Step 2: Call the checkpoint from `loop()`**

At the very end of `loop()` (`main.cpp:829`), replace:

```cpp
    if (rec_active) rec_poll_samples();
```

with:

```cpp
    if (rec_active) {
        rec_poll_samples();
        if ((millis() - rec_last_sync_ms) >= REC_SYNC_INTERVAL_MS) rec_checkpoint();
    }
```

(`rec_data_bytes` grows in `rec_flush_chunk`; the checkpoint rewrites the header with the flushed byte count and `f_sync`s so the FAT directory size matches. A power loss then leaves a playable file with a valid header and correct size, losing at most ~1 s of audio plus the pending <=64-byte chunk.)

- [ ] **Step 3: Finalize recording before low-battery STOP**

In `low_battery_enter_stop` (`main.cpp:435-444`), add the finalize at the top:

```cpp
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
```

This covers both sleep paths (first entry and periodic re-sleep), so a recording is never left half-written across STOP. `rec_stop` is defined before this function, so it is callable.

- [ ] **Step 4: Build, flash, verify**

```powershell
pio run -e dayvault
```

Flash. Verify:

1. **Checkpointing is active**: `REC`, wait ~5 s, `STOP`; the file header (`CHECK`) shows a data size matching `rec_data_bytes` from the AUTO-stop message (within one sync interval).
2. **Sleep finalize**: with USB unplugged and battery below 3.0 V, a recording stops cleanly and the device sleeps; on replug, `LIST` shows a correctly-sized, duration-renamed file. (Physical: user unplugs USB on a low battery.)
3. **Power-loss drill (physical)**: start `REC`, wait ~5 s, then **disconnect power** (battery JST or the board power switch; keep USB pulled too). Re-power, reconnect USB, `LIST`/`CHECK`: the file exists with the start-time name, a valid header, and a data size close to what ~5 s of audio implies (NOT 0/44). This proves checkpointing recovered the file.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/main.cpp
git commit -m "feat(rec): periodic WAV header+f_sync checkpointing for power-loss safety; finalize recording before low-battery STOP"
```

---

### Task 5: Documentation

**Files:**
- Modify: `Docs/Serial-Command-Reference.md`

**Interfaces:** none (pure docs).

- [ ] **Step 1: Update the command table**

In the `INFO` row, add `time_set`, `tz` to the fields and note `time=` is **local** time. Add rows for:

- `SETTZ <minutes>` — set UTC offset only (e.g. `SETTZ 480`; negative for west).
- `SETTIME <unix> [tz_minutes]` — example shows optional offset: `SETTIME 1786273834 480`.
- `LTEST` — long-filename diagnostic (create/readdir/rename/delete).

- [ ] **Step 2: Document recording naming behavior**

In the REC/STOP section, add a subsection describing:

- Names: `REC-YYYYMMDD-HHMM.WAV` (local time, minute precision); renamed on stop to `REC-..._MmSs.WAV` / `_HhMmSs.WAV`.
- Same-minute collision: `_1`..`_9` suffix (kept in the final name before the duration).
- If time is not set (`time_set=0`, fresh board or backup-domain loss): falls back to `REC%03u.WAV` sequence names until the first `SETTIME`.
- Power-loss recovery: header + FAT size are checkpointed ~1/s, so a power loss leaves a playable file with the start-time name (no duration suffix); low-battery sleep finalizes the recording cleanly first.
- Timezone: RTC stores UTC; `tz` offset (default +480 = UTC+8) is applied for filenames and `INFO`; each PC sync should send `SETTIME <unix> <pc_tz_offset>` so the offset is refreshed on every sync.

- [ ] **Step 3: Commit**

```bash
git add Docs/Serial-Command-Reference.md
git commit -m "docs: SETTZ/SETTIME tz arg, LTEST, INFO tz/time_set, timestamp+duration naming, power-loss recovery"
```

---

## Self-Review

- Spec §1 (filename scheme) → Task 3. §2 (time sync/timezone) → Task 2. §3 (LFN bring-up) → Task 1. §4 (affected code) → Tasks 2-4 + Task 5 docs. §5 (power-loss safety) → Task 4. §6/§7/§8 (error handling/testing) → Tasks 3-4 + verification steps.
- No placeholders: every code step has full code; every build/verify step has commands and expected output.
- Type consistency: `dt_time_is_set`, `dt_get_tz`, `dt_set_tz`, `dt_format_local`, `dt_format_stem` declared in Task 2 are used identically in Tasks 3/5; `rec_name`, `rec_name_kind`, `rec_last_sync_ms`, `rec_checkpoint`, `rec_duration_str`, `rec_name_cmp` are defined before use.
- The final power-loss drill requires the user to physically disconnect power — flagged in Task 4 Step 4.
