# RTC Timestamped Recording Filenames — Design

Date: 2026-08-09
Status: Approved by user

## Goal

Replace the current sequence-based recording filenames (`REC%03u.WAV`, e.g. `REC062.WAV`)
with human-readable names that contain the **recording start time** (local, minute
precision) and the **recording duration**, e.g.:

- During recording: `REC-20260809-1110.WAV`
- After stop (renamed): `REC-20260809-1110_5m32s.WAV` (>=1 h uses `_1h23m45s`)

## Background / constraints

- The RTC runs on a 3.3 V rail with **no coin cell**. While powered (USB or battery,
  battery is permanently attached) the RTC keeps time across reset/flash/sleep. A fresh
  board or a fully-drained battery with USB unplugged resets the clock to the default
  2026-01-01 00:00 (`dt_init`).
- `SETTIME <unix>` sets the RTC from a UTC unix epoch. The RTC stores UTC civil time;
  a UTC offset is applied only at render time (filenames, INFO).
- FatFs LFN is configured in `lib/FatFs/ffconf.h` as `_USE_LFN 2` (`_MAX_LFN 255`,
  exFAT on), but the compiled `ff.c.o` references `ff_convert` yet **no `ff_memalloc`**,
  so the effective LFN configuration is uncertain and long names are NOT yet proven to
  work. This must be brought up and verified first (see LFN bring-up).

## Design

### 1. Filename scheme

- **Start (`rec_start`)**, when time is set (`time_set == 1`):
  - Build a local-time minute-precision name: `REC-YYYYMMDD-HHMM.WAV`
  - Open with `FA_CREATE_NEW`; on `FR_EXIST`, retry with suffix `_1` .. `_9`
    (e.g. `REC-20260809-1110_2.WAV`).
  - If the long-name open fails for any other reason, **fall back to the sequence
    naming** `REC%03u.WAV` (recording must never be blocked by naming).
- **Stop (`rec_stop`)**:
  - Duration = `rec_data_bytes / byte_rate`, where `byte_rate` is taken from the actual
    `rec_cfg` (channels * bytes-per-sample * sample rate; 16-bit mono 21000 Hz = 42000 B/s).
  - Render duration: `<h>h<mm>m<ss>s` for >=1 h, `<mm>m<ss>s` for <1 h.
  - `f_rename` to `REC-YYYYMMDD-HHMM[_n]_5m32s.WAV`. The creation suffix `_n` (if any)
    is preserved before the duration.
  - If `f_rename` fails, keep the start-time name (no data loss).
- **When time is not set** (`time_set == 0`): use the existing sequence naming
  (`REC%03u.WAV`). Timestamp-named files are skipped by `fs_next_sequence` naturally
  (`parse_num` stops at `-` and yields 0), so old `REC###` files and new timestamp
  files coexist.

### 2. Time sync & timezone

Backup domain additions (same mechanism as the existing BKP0R init magic; they reset
on power loss). Stored packed in a spare backup data register (e.g. `BKP1R`):
`time_set` flag + `tz_offset` (int32 minutes, default **+480**, UTC+8, current dev PC).
Applied only at render time; the RTC calendar itself stays UTC.

Commands:

- `SETTIME <unix> [tz_minutes]` — sets the RTC to UTC unix, sets `time_set`, and
  **if `tz_minutes` is given, overwrites the stored offset** (a PC sync always sends it,
  so each sync overwrites the old timezone). If omitted, the stored offset is kept.
- `SETTZ <minutes>` — set the UTC offset only (does not touch the clock).

Rendering:

- Filenames use local time (`dt_get_unix` + offset, then civil).
- `INFO` reports `time=` as local time and adds `tz=<offset_minutes>` and `time_set=1`.
- `REC started seq=` message additionally prints the actual filename.

### 3. LFN bring-up (mandatory first step)

- Determine the ffconf actually used by the build (`.pio` staged lib / build flags;
  the lib header and the compiled object disagree today).
- Preferred fix (Plan A): implement `ff_memalloc`/`ff_memfree` as thin `malloc`/`free`
  wrappers, keeping `_USE_LFN 2`. FatFs is only called in loop context (never ISR), so
  dynamic allocation is safe.
- Fallback (Plan B): switch to `_USE_LFN 1` with `_MAX_LFN` reduced to ~40 (names we
  generate are ~30 chars max), static buffer, no dynamic allocation.
- Empirically verify on the SD card: create `REC-20260809-1110_5m32s.WAV`, see it in
  `f_readdir`, rename it with `f_rename`.

### 4. Affected existing code

| Location | Change |
|---|---|
| `main.cpp rec_start/rec_stop` | Timestamp name generation, duration math, `f_rename`, collision retry, seq fallback |
| `main.cpp check_wav_file` (CHECK cmd) | Find newest recording by lexicographically largest filename instead of `seq-1` |
| `DeviceTime` | Local-time formatting; backup-domain `time_set`/`tz_offset`; `SETTIME` offset arg; `SETTZ` |
| `INFO` | `time=` local, add `tz=` and `time_set=` |
| `REC started seq=` | Append actual filename |
| `LIST` / `fs_next_sequence` | No logic change expected (LFN `fname` is the long name; `parse_num` yields 0 for `REC-`) — verify in LFN bring-up |
| Docs `Serial-Command-Reference.md` | `SETTIME` new syntax, `SETTZ`, `INFO` new fields, recording naming behavior (timestamp+duration, collision suffix, unset fallback, RTC/timezone semantics) |

### 5. Error handling

- Long-name open failure -> sequence-name fallback; recording continues.
- `f_rename` failure -> keep the start-time filename; file is intact.
- No path may block or lose a recording.

### 6. Testing

1. LFN bring-up empirical test (create / readdir / rename on the SD card).
2. Build + flash.
3. `SETTIME <unix> 480` then `REC`/`STOP` — verify `REC-YYYYMMDD-HHMM.WAV` created and
   renamed to `REC-..._MmSs.WAV` on stop.
4. Two recordings in the same minute — verify `_1` suffix.
5. `INFO` shows local time + `tz=+480` + `time_set=1`.
6. `LIST` shows long names; `DOWNLOAD`/`DL2` still work by exact name.
7. Docs updated and reviewed.
