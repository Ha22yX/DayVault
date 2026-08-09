# Circular Recording (Delete-Oldest) + PDM Overflow Self-Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Automatically delete the oldest completed recordings when the SD card drops below 64 MB free (circular recording), and harden `pdm_dma_read` so long loop blocks never permanently starve or corrupt recorded audio.

**Architecture:** Add three FatFs helpers in `Fs.cpp` (`fs_free_bytes`, `fs_make_space`, `fs_delete_oldest`) with oldest-first ordering (sequence files older than timestamp files). Fix `pdm_dma_read` in `PdmCapture.cpp` with monotonic written/consumed counters that detect a DMA lap and resync to fresh data. Wire a 30 s free-space check into the recording loop, a pre-check at `rec_start`, and add `CIRC`/`DELOLDEST` commands + `INFO free=`.

**Tech Stack:** STM32L452RC, STM32duino Arduino core, FatFs (R0.12c, `_USE_LFN 2`, `_MAX_LFN 40`), PDM/DFSDM audio, PlatformIO.

## Global Constraints

- **NEVER touch FLASH option bytes or `dfu-util` writes to `0x1FFF7800`** (brick risk, AGENTS.md HARD RULE). Enter DFU only via the firmware `DFU` serial command or BOOT+RST.
- USB VID stays ST `0x0483`.
- **The active recording file must never be deleted** — every delete path skips it by basename.
- Recording must never be blocked or lose data because of circular deletion (best-effort; a failed `f_getfree`/`f_unlink` just skips the pass).
- RTC stays UTC; offset render-time only (unchanged from prior work).
- Every command/behavior change is documented in `Docs/Serial-Command-Reference.md`.

---

## File Structure

- `firmware/src/Fs.h` — declare `fs_free_bytes`, `fs_make_space`, `fs_delete_oldest`.
- `firmware/src/Fs.cpp` — implement the three helpers + oldest-first ordering (collect → sort → unlink), `#include <stdlib.h>` for `qsort`.
- `firmware/src/PdmCapture.cpp` — `pdm_dma_read` overrun detection + resync.
- `firmware/src/main.cpp` — `CIRC`/`DELOLDEST` commands, 30 s loop check, `rec_start` pre-check, `INFO free=`.
- `Docs/Serial-Command-Reference.md` — document everything.

Build: `pio run -e dayvault` (run in `firmware/`). Flash: serial `DFU` command, or BOOT+RST + `dfu-util -a 0 -s 0x08000000:leave -D .pio/build/dayvault/firmware.bin`. Serial: COM9 @115200.

---

### Task 1: Fs free-space + delete-oldest helpers, `CIRC`/`DELOLDEST` commands

**Files:**
- Modify: `firmware/src/Fs.h`, `firmware/src/Fs.cpp`, `firmware/src/main.cpp`

**Interfaces:**
- Consumes: `f_getfree`, `f_opendir`/`f_readdir`, `f_unlink`, `Config.h` `REC_DIR_STR`/`REC_EXT_STR`; serial dispatch chain in `main.cpp`.
- Produces:
  - `uint64_t fs_free_bytes(void)` — free bytes on `0:` (0 if not mountable).
  - `int fs_make_space(uint64_t want_free, const char* skip_basename)` — delete oldest until free >= `want_free`; returns count deleted.
  - `int fs_delete_oldest(const char* skip_basename)` — delete exactly the oldest; returns 0/1.
  - Commands `CIRC` and `DELOLDEST`.

- [ ] **Step 1: Extend `Fs.h`**

Add to the `extern "C"` block (after `fs_test_write`):

```c
uint64_t fs_free_bytes(void);
int  fs_make_space(uint64_t want_free, const char* skip_basename);
int  fs_delete_oldest(const char* skip_basename);
```

- [ ] **Step 2: Implement the helpers in `Fs.cpp`**

Add `#include <stdlib.h>` to the existing includes (for `qsort`), then append:

```c
typedef struct { char name[40]; uint32_t size; } rec_file_t;

static bool is_seq_name(const char* n) { return (n[3] != '-'); }   /* "REC###..." vs "REC-..." */
static uint32_t seq_num(const char* n)
{
    uint32_t v = 0;
    const char* p = n + 3;
    while (*p >= '0' && *p <= '9') { v = v * 10u + (uint32_t)(*p - '0'); p++; }
    return v;
}

static int rec_oldest_cmp(const void* a, const void* b)
{
    const rec_file_t* A = (const rec_file_t*)a;
    const rec_file_t* B = (const rec_file_t*)b;
    bool sa = is_seq_name(A->name), sb = is_seq_name(B->name);
    if (sa != sb) return sa ? -1 : 1;                    /* seq files are the old era -> oldest */
    if (sa) {
        uint32_t na = seq_num(A->name), nb = seq_num(B->name);
        return (na < nb) ? -1 : (na > nb) ? 1 : 0;
    }
    return strcmp(A->name, B->name);                     /* timestamp names sort chronologically */
}

uint64_t fs_free_bytes(void)
{
    FATFS* fatfs;
    DWORD nclst = 0;
    if (f_getfree("0:", &nclst, &fatfs) != FR_OK) return 0;
    return (uint64_t)nclst * fatfs->csize * fatfs->ssize;
}

static int fs_collect_rec(rec_file_t* arr, int cap, const char* skip)
{
    DIR dir;
    FILINFO fno;
    int n = 0;
    if (f_opendir(&dir, "0:/") != FR_OK) return 0;
    while (n < cap && f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
        if ((fno.fattrib & AM_DIR) != 0) continue;
        if (strncmp(fno.fname, REC_DIR_STR, strlen(REC_DIR_STR)) != 0) continue;
        char* dot = strrchr(fno.fname, '.');
        if (dot == NULL || strcmp(dot + 1, REC_EXT_STR) != 0) continue;
        if (skip != NULL && strcmp(fno.fname, skip) == 0) continue;   /* never touch active recording */
        strncpy(arr[n].name, fno.fname, sizeof(arr[n].name) - 1);
        arr[n].name[sizeof(arr[n].name) - 1] = 0;
        arr[n].size = (uint32_t)fno.fsize;
        n++;
    }
    f_closedir(&dir);
    return n;
}

static int fs_delete_oldest_candidates(const char* skip, int limit)
{
    rec_file_t arr[64];
    int n = fs_collect_rec(arr, 64, skip);
    if (n == 0) return 0;
    qsort(arr, (size_t)n, sizeof(rec_file_t), rec_oldest_cmp);
    int deleted = 0;
    for (int i = 0; i < n && deleted < limit; i++) {
        char path[44];
        snprintf(path, sizeof(path), "0:/%s", arr[i].name);
        if (f_unlink(path) == FR_OK) deleted++;
    }
    return deleted;
}

int fs_delete_oldest(const char* skip) { return fs_delete_oldest_candidates(skip, 1); }

int fs_make_space(uint64_t want_free, const char* skip)
{
    if (fs_free_bytes() >= want_free) return 0;
    int deleted = 0;
    while (fs_free_bytes() < want_free) {
        int d = fs_delete_oldest_candidates(skip, 1);
        if (d == 0) break;
        deleted += d;
    }
    return deleted;
}
```

Note: `bool` requires `<stdbool.h>` — `Fs.h` already includes it. `snprintf` needs `<stdio.h>` (add if not present; `Fs.cpp` currently includes `string.h`, `ff.h`, `Config.h`).

- [ ] **Step 3: Add `CIRC` and `DELOLDEST` commands in `main.cpp`**

Define near the rec statics:

```cpp
#define CIRC_FREE_BYTES (64u * 1024u * 1024u)   /* delete oldest below this free space */
```

Add handlers in the serial dispatch chain (after `CHECK`, around the existing commands):

```cpp
} else if (strncmp(line, "CIRC", 4) == 0) {
    uint64_t fb = fs_free_bytes();
    int del = fs_make_space(CIRC_FREE_BYTES, rec_active ? rec_name + 3 : NULL);
    uint64_t fa = fs_free_bytes();
    Serial.print("CIRC free_before="); Serial.print((uint32_t)(fb >> 20)); Serial.print("MB");
    Serial.print(" deleted="); Serial.print(del);
    Serial.print(" free_after="); Serial.print((uint32_t)(fa >> 20)); Serial.println("MB");
} else if (strncmp(line, "DELOLDEST", 9) == 0) {
    int del = fs_delete_oldest(rec_active ? rec_name + 3 : NULL);
    Serial.print("DELOLDEST deleted="); Serial.println(del);
}
```

`rec_name` holds `0:/REC-...` so `rec_name + 3` is the basename the scan compares against. `fs_free_bytes()` needs the FS mounted: if the device has never mounted it in this session, run `fs_mount_result()` once before the first use — add it to the `CIRC` handler above the `fb` read:

```cpp
    fs_mount_result();
```

- [ ] **Step 4: Build**

```powershell
pio run -e dayvault
```

Expected: SUCCESS.

- [ ] **Step 5: Flash and verify on hardware**

Flash, then over COM9:

```
CIRC          -> CIRC free_before=~122000MB deleted=0 free_after=~122000MB   (free reported, no delete when above threshold)
LIST          -> note the newest REC file name
DELOLDEST     -> DELOLDEST deleted=1
LIST          -> the oldest REC file is gone (verify ordering: old REC### seq files deleted before timestamp files)
```

Confirm `deleted=0` when free is high, `DELOLDEST` removes the true oldest file, and `CIRC` prints sensible MB numbers.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/Fs.h firmware/src/Fs.cpp firmware/src/main.cpp
git commit -m "feat(fs): fs_free_bytes/fs_make_space/fs_delete_oldest with oldest-first ordering; CIRC and DELOLDEST commands"
```

---

### Task 2: PDM overflow self-recovery

**Files:**
- Modify: `firmware/src/PdmCapture.cpp`

**Interfaces:**
- Consumes: `PDM_DMA_BUF_SAMPLES` (8192), `PDM_DMA_BUF_FREE_MARGIN` (64), `PDM_DMA_CH->CNDTR`, `pdm_dma_pos`, `pdm_dma_pos2`, `pdm_dma_buf`, `overruns`.
- Produces: `pdm_dma_read` that recovers after a DMA ring lap — detects overflow via monotonic written/consumed counters, discards the stale segment, resyncs the read position behind the write head, and continues at full rate.

- [ ] **Step 1: Add the counters**

Near `pdm_dma_pos` (line ~24), add:

```cpp
static uint32_t pdm_dma_written_total = 0;
static uint32_t pdm_dma_consumed_total = 0;
static uint32_t pdm_dma_prev_written = 0;
```

- [ ] **Step 2: Rework the availability computation in `pdm_dma_read`**

In `pdm_dma_read` (`PdmCapture.cpp:227-289`), replace the current head/avail block:

```cpp
    while (n < max) {
        uint32_t ndtr = PDM_DMA_CH->CNDTR;
        uint32_t written = (PDM_DMA_BUF_SAMPLES - ndtr) & (PDM_DMA_BUF_SAMPLES - 1u);
        uint32_t avail = (written + PDM_DMA_BUF_SAMPLES - pdm_dma_pos) & (PDM_DMA_BUF_SAMPLES - 1u);
        if (avail > PDM_DMA_BUF_SAMPLES - PDM_DMA_BUF_FREE_MARGIN) {
            avail = PDM_DMA_BUF_SAMPLES - PDM_DMA_BUF_FREE_MARGIN;
        }
        if (avail == 0) break;
```

with:

```cpp
    while (n < max) {
        uint32_t ndtr = PDM_DMA_CH->CNDTR;
        uint32_t written = (PDM_DMA_BUF_SAMPLES - ndtr) & (PDM_DMA_BUF_SAMPLES - 1u);
        uint32_t delta = (written + PDM_DMA_BUF_SAMPLES - pdm_dma_prev_written) & (PDM_DMA_BUF_SAMPLES - 1u);
        pdm_dma_prev_written = written;
        pdm_dma_written_total += delta;

        uint32_t backlog = pdm_dma_written_total - pdm_dma_consumed_total;
        if (backlog > PDM_DMA_BUF_SAMPLES) {   /* DMA lapped the read position -> stale data */
            overruns++;
            uint32_t head = (written + PDM_DMA_BUF_SAMPLES - PDM_DMA_BUF_FREE_MARGIN) & (PDM_DMA_BUF_SAMPLES - 1u);
            pdm_dma_pos = head;
            pdm_dma_pos2 = head;
            pdm_dma_consumed_total = pdm_dma_written_total - (PDM_DMA_BUF_SAMPLES - PDM_DMA_BUF_FREE_MARGIN);
            backlog = PDM_DMA_BUF_SAMPLES - PDM_DMA_BUF_FREE_MARGIN;
        }
        uint32_t avail = backlog;
        if (avail > PDM_DMA_BUF_SAMPLES - PDM_DMA_BUF_FREE_MARGIN) {
            avail = PDM_DMA_BUF_SAMPLES - PDM_DMA_BUF_FREE_MARGIN;
        }
        if (avail == 0) break;
```

Then, where the loop advances the read position (after the per-sample `for`), add the consumed accounting — the existing two lines:

```cpp
        pdm_dma_pos = (pdm_dma_pos + take) & (PDM_DMA_BUF_SAMPLES - 1u);
        pdm_dma_pos2 = (pdm_dma_pos2 + take) & (PDM_DMA_BUF_SAMPLES - 1u);
```

become:

```cpp
        pdm_dma_pos = (pdm_dma_pos + take) & (PDM_DMA_BUF_SAMPLES - 1u);
        pdm_dma_pos2 = (pdm_dma_pos2 + take) & (PDM_DMA_BUF_SAMPLES - 1u);
        pdm_dma_consumed_total += take;
```

- [ ] **Step 3: Build**

```powershell
pio run -e dayvault
```

Expected: SUCCESS.

- [ ] **Step 4: Flash and verify on hardware — audio recovers after loop blocks**

1. `REC`, let it run 5 s, `STOP`: bytes ≈ full rate (~42 KB/s × elapsed). Baseline OK.
2. `REC`; then immediately fire **5 × `LIST`** commands in quick succession (each blocks the loop ~300-500 ms, overflowing the 0.25 s ring); then wait **6 s with no commands**; then `STOP`. Expected: `AUTO stop` bytes ≈ `42 KB/s × (elapsed - blocking_time)` — i.e., audio resumes to full rate after the blocks, NOT a permanent trickle. Also run `DBG` and check nothing is hung. If `bytes` is far below the full-rate expectation, report it (do not commit a silent regression).
3. Optionally confirm the overrun counter via `pdm_overruns()` if you add a print — otherwise rely on the byte math.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/PdmCapture.cpp
git commit -m "feat(pdm): detect DMA ring lap and resync read position (overflow self-recovery)"
```

---

### Task 3: Loop integration (30 s check + rec_start pre-check), persistent mount + boot warm, `INFO free=`

**Files:**
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Consumes: `fs_make_space` (Task 1), `rec_active`, `rec_name`, `rec_start`, loop structure, `INFO` handler, `fs_mount`/`fs_unmount`.
- Produces: automatic circular deletion while recording; fast (cached) free-space reads; `INFO free=<MB>`.

Context: on this exFAT volume the first `f_getfree` after a fresh mount scans the ~4 MB allocation bitmap (~10 s). `f_getfree` caches the result in `fs->free_clst` (updated incrementally by FatFs writes/deletes), but `fs_unmount()` resets it to invalid on every `rec_stop`, so the scan would repeat every recording and block the 30 s check. Fix: **mount the volume once at boot and keep it mounted** (persistent SD), so `fs_free_bytes()` stays fast.

- [ ] **Step 1: Boot-time mount + warm, before the watchdog**

In `setup()` (in `main.cpp`), before `dbg_iwdg_init();`, add a one-time mount + free-space warm:

```cpp
    fs_mount();
    fs_free_bytes();          /* ~10 s exFAT bitmap scan, caches free_clst; before IWDG so no reset risk */
```

If the SD is absent at boot, `fs_mount()` fails and this is skipped (the first recording mounts later; its first `fs_free_bytes()` will be the slow scan at `rec_start`, before audio starts — acceptable edge).

- [ ] **Step 2: Stop unmounting between recordings and after downloads**

Remove the `fs_unmount();` calls that break the persistent mount:
- `rec_stop` (the final `fs_unmount();` after `f_close`).
- `DL` download handler (`fs_unmount();` after download).
- `DL2` download handler (both the normal-path and timeout-path `fs_unmount();`).

Keep `fs_unmount()` only on error/recovery paths (e.g., failed open/write in `rec_start`). The SD is fixed and never swapped, so a permanently mounted volume is safe.

- [ ] **Step 3: Add the cadence state**

Near the rec statics, add:

```cpp
#define REC_CIRC_INTERVAL_MS 30000u
static uint32_t rec_circ_last_ms = 0;
```

- [ ] **Step 4: Pre-check in `rec_start`**

In `rec_start`, after `rec_active = true;` add:

```cpp
    fs_make_space(CIRC_FREE_BYTES, rec_name + 3);   /* free room before a long recording */
```

- [ ] **Step 5: 30 s check in `loop()`**

Inside the `if (rec_active)` block (where the checkpoint runs), add after the checkpoint call:

```cpp
        if ((millis() - rec_circ_last_ms) >= REC_CIRC_INTERVAL_MS) {
            rec_circ_last_ms = millis();
            fs_make_space(CIRC_FREE_BYTES, rec_name + 3);
        }
```

- [ ] **Step 6: `INFO free=`**

In the `INFO` handler, after the existing `sd=` field, add a free-space field:

```cpp
                        Serial.print(" free=");
                        Serial.print((uint32_t)(fs_free_bytes() >> 20)); Serial.print("MB");
```

(The volume is persistently mounted after Step 1, so `fs_free_bytes()` is instant; no `fs_mount_result()` needed.)

- [ ] **Step 7: Build, flash, verify**

```powershell
pio run -e dayvault
```

Flash. Verify:
1. `INFO` shows `free=~122000MB` and responds fast (no 10 s hang).
2. `REC` then `STOP`: still records normally; then `REC` again — the second recording starts without a slow free-space scan (mount persisted).
3. The 30 s auto-check doesn't cause issues during a longer recording (e.g., `REC`, wait ~35 s, `STOP`).
4. `CIRC` returns fast now (cache warm).

- [ ] **Step 8: Commit**

```bash
git add firmware/src/main.cpp
git commit -m "feat(rec): persistent SD mount + boot free-space warm; periodic 30s circular check; rec_start pre-check; INFO free="
```

---

### Task 4: Documentation

**Files:**
- Modify: `Docs/Serial-Command-Reference.md`

**Interfaces:** none (pure docs).

- [ ] **Step 1: Document the commands and behavior**

Add rows for `CIRC` and `DELOLDEST` to the command table, note `INFO` now reports `free=<MB>`, and add a short subsection describing circular recording:
- When recording is active, free space is checked every 30 s (and at record start); below **64 MB** free, the oldest completed recordings are deleted (sequence-name `REC###` files first, then timestamp files oldest-first) until free is back to >= 64 MB.
- The in-progress recording file is never deleted.
- `pdm_dma_read` recovers automatically after long loop blocks (a short audio gap is expected; the file remains valid).

- [ ] **Step 2: Commit**

```bash
git add Docs/Serial-Command-Reference.md
git commit -m "docs: CIRC/DELOLDEST commands, INFO free=, circular delete-oldest behavior, PDM overrun recovery"
```

---

## Self-Review

- Spec §1 (circular deletion) → Task 1 + Task 3. Spec §2 (PDM self-recovery) → Task 2. Spec §3 (affected code) → Tasks 1-4. Spec §4 (error handling) → Task 1 `fs_make_space` guard + Task 3 best-effort. Spec §5 (testing) → per-task flash/verify steps.
- No placeholders: every code step has full code; every verify step has commands and expected output.
- Type consistency: `fs_free_bytes`/`fs_make_space`/`fs_delete_oldest` declared in Task 1 are used identically in Tasks 3/4; `CIRC_FREE_BYTES` defined in Task 1 is reused in Task 3; `rec_name`, `rec_active` referenced consistently.
- The audio-recovery test (Task 2 Step 4) requires a physical SD + serial timing; if the recovery result is ambiguous, the reviewer is told to report rather than silently accept.
