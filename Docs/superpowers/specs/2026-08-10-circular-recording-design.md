# Circular Recording (Delete-Oldest on Low Storage) + Audio Overflow Self-Recovery — Design

Date: 2026-08-10
Status: Approved by user

## Goal

When the SD card is nearly full during recording, automatically delete the oldest
completed recordings (circular recording) to keep recording without user intervention.
Also harden the PDM audio read path so a long blocking operation (file deletion, LIST,
downloads) never permanently starves or corrupts the recorded audio.

## Background / constraints

- Recording is a single growing WAV file at ~42 KB/s (16-bit mono ~21 kHz). A 125 GB SD
  holds ~34 days; the feature exists for long unattended operation and for smaller cards.
- The audio ring is `pdm_dma_buf[8192]` samples (0.25 s). `pdm_dma_read` computes
  availability from the DMA write head (`CNDTR`) vs a read position. If the main loop is
  blocked longer than the ring (e.g., by a directory scan + `f_unlink`, LIST, DOWNLOAD),
  the DMA can wrap past the read position: availability is then mis-computed and the read
  can emit stale (already-consumed) samples, with permanent degradation until PDM restart.
- New recordings use timestamp names `REC-YYYYMMDD-HHMM[_n][_dur].WAV`; older files use
  sequence names `REC###.WAV`. The active recording must never be deleted.

## Design

### 1. Circular deletion

- **Trigger**: while recording is active, every **30 s** check free space via `f_getfree`
  (`fs_free_bytes()`), and also check once at `rec_start`. If free < **64 MB**, delete
  old files.
- **Delete loop** (`fs_make_space(want_free, skip_basename)` in `Fs.cpp`):
  - `want_free` = 64 MB (67108864 bytes headroom ≈ 26 min of recording at ~42 KB/s).
  - Scan the root directory for `REC*.WAV` files; collect name + size into a bounded
    array (64 entries). **Skip the active recording** (basename match on `skip_basename`).
  - Order oldest-first: sequence files (`REC###.WAV`, the old era) before all timestamp
    files; among sequence files by ascending number; among timestamp files by
    lexicographic name (chronological).
  - `f_unlink` each until `fs_free_bytes() >= want_free` or no candidates remain.
  - Return value = number of files deleted (0 if none / no candidates).
  - Errors: no candidates -> return 0; a failed `f_unlink` -> stop the current pass.
- **Callers**: main loop (30 s cadence while `rec_active`), `rec_start` (one pre-check).
- **Diagnostics**:
  - `CIRC` — runs one check + delete pass, prints free bytes before/after and deleted
    count (e.g. `CIRC free_before=..B deleted=.. free_after=..B`).
  - `DELOLDEST` — deletes exactly the single oldest `REC*.WAV` (safe diagnostic to verify
    ordering on hardware).
  - `INFO` adds `free=<bytes>B` (free space, via `fs_free_bytes()`).

### 2. Audio overflow self-recovery (`pdm_dma_read` in `PdmCapture.cpp`)

- Maintain two 32-bit monotonic counters:
  - `pdm_dma_written_total` — accumulate the masked DMA write-head delta each call
    (detect `CNDTR` wrap so the count is truly monotonic).
  - `pdm_dma_consumed_total` — accumulate `take` (samples returned).
- Before reading, `backlog = written_total - consumed_total`.
  - Normal: `backlog <= PDM_DMA_BUF_SAMPLES - PDM_DMA_BUF_FREE_MARGIN` -> read normally.
  - Overflow detected (`backlog > PDM_DMA_BUF_SAMPLES`): the DMA lapped the read
    position; the ring contains stale data. **Resync**: jump the read position to just
    behind the write head (`consumed_total = written_total - (PDM_DMA_BUF_SAMPLES -
    PDM_DMA_BUF_FREE_MARGIN)`), discarding the stale segment, and count the overrun.
- Effect: after any long blocking operation, audio resumes from fresh data at full rate
  with a small discard (<= 0.25 s) instead of stale/duplicated samples.

### 3. Affected existing code

| Location | Change |
|---|---|
| `firmware/src/Fs.h` | Declare `uint64_t fs_free_bytes(void); int fs_make_space(uint64_t want_free, const char* skip_basename); int fs_delete_oldest(void);` |
| `firmware/src/Fs.cpp` | Implement the three helpers + oldest-first ordering + bounded scan |
| `firmware/src/PdmCapture.cpp` | `pdm_dma_read` overrun detection + resync counters; overrun counter exposed |
| `firmware/src/main.cpp` | 30 s circular check in loop; pre-check in `rec_start`; `CIRC` + `DELOLDEST` commands; `INFO free=` |
| `Docs/Serial-Command-Reference.md` | `CIRC`/`DELOLDEST`, `INFO free=`, circular-deletion behavior |

### 4. Error handling

- `fs_make_space`: never deletes the active file (skipped by name); no candidates or
  delete failure -> returns without erroring the recording.
- The 30 s free-space check is best-effort; a failed `f_getfree` is skipped.
- Recording itself is never blocked or lost by the circular logic.

### 5. Testing

1. `CIRC` prints correct free space (`free=..B`) on the 125 GB SD.
2. `DELOLDEST` on hardware deletes the true oldest file (mixed timestamp + sequence
   names verify ordering).
3. Audio self-recovery: during active recording, fire several `LIST` commands to create
   long loop blocks, then stop; verify recording returns to full rate (bytes/s matches
   ~42 KB/s) and no permanent starvation (this re-tests the earlier suspected
   LIST-during-recording issue).
4. Build clean; docs updated.
