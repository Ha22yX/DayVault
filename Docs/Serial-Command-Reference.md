# DayVault Serial Command Reference

For future firmware/software development. This documents the USB CDC serial interface of the
DayVault voice recorder firmware (STM32L452), including the audio download protocol.

## 1. Connection

- USB CDC virtual serial port, **115200 baud**, 8N1.
- On Windows the device enumerates as `COMx` ("USB 串行设备", VID `0x0483`, PID `0x5740`).
- Commands are ASCII text lines terminated by `\n` or `\r`. Case-sensitive.
- The normal interface is USB CDC. During a `BULK2` export the firmware briefly
  re-enumerates as a dedicated WinUSB device (PID `0x5741`), then returns to CDC.

## 2. Firmware build / flashing

- Build: `pio run -e dayvault` (PlatformIO, in `firmware/`).
- Flash over USB via the software DFU command (see §6) or the hardware BOOT button:
  hold BOOT + press RST, then `dfu-util -a 0 -s 0x08000000:leave -D firmware.bin`.

## 3. Core recording behavior

- **Auto recording:** the device starts/stops recording based on the USB-detect pin (PA9).
  - USB DETACHED (pin LOW) → recording starts automatically with a timestamped name
    `REC-YYYYMMDD-HHMM.WAV` (see §3.1).
  - USB ATTACHED (pin HIGH) → recording stops, then enumerates as serial.
- Files: `REC-YYYYMMDD-HHMM.WAV` (local time) when the RTC time is set; `REC###.WAV`
  (sequential numbers) fallback otherwise. 16-bit PCM WAV on exFAT microSD.
- **Persistent SD mount:** the SD volume is mounted once at boot and stays mounted across
  recordings (no unmount between recordings). Recording, `INFO free=`, and the circular
  deletion (`CIRC`/auto) all operate on the live volume.
- For full timestamped-naming details see §3.1 below.
- Manual control is possible via `REC` / `STOP`.

### 3.1 Recording filenames and timezone

- **Timestamped names** (time set, `time_set=1`): a recording starts as `REC-YYYYMMDD-HHMM.WAV`,
  named from **local** time at minute precision (e.g. `REC-20260809-2015.WAV`). On `STOP` the
  file is renamed to include the actual duration: `REC-YYYYMMDD-HHMM_MmSs.WAV` for recordings
  under an hour, or `REC-YYYYMMDD-HHMM_HhMmSs.WAV` for an hour or more
  (e.g. `REC-20260809-2015_5m32s.WAV`, `REC-20260809-2015_1h04m03s.WAV`).
- **Same-minute collision:** if a `REC-YYYYMMDD-HHMM.WAV` name already exists, a `_1`..`_9`
  suffix is appended at start time (`REC-20260809-2015_1.WAV`); the suffix is kept in the
  final renamed name before the duration (`REC-20260809-2015_1_5m32s.WAV`).
- **Time not set** (`time_set=0` — fresh board or backup-domain loss): falls back to legacy
  `REC###.WAV` sequence names (`REC%03u.WAV`) until the first `SETTIME`.
- **Power-loss recovery:** the WAV header and FAT size are checkpointed ~1/s (`f_sync`), so a
  power loss leaves a playable file with the start-time name (no duration suffix). On low
  battery the device finalizes the recording cleanly before entering STOP sleep.
- **Timezone:** the RTC stores UTC; the `tz` offset (default +480 = UTC+8) is applied for
  filenames and the `INFO` `time=` field. Each PC sync should send `SETTIME <unix> <pc_tz_offset>`
  so the offset is refreshed on every sync (a bare `SETTZ <minutes>` can adjust it standalone).
  If `SETTIME` **omits** the offset argument, the currently stored offset is kept.

## 4. Command list

| Command | Description | Example response |
|---|---|---|
| `INFO` | Status dump: usb_detect, boot(BOOT0), up(millis), sysclk, ckin_div, fltcr1, sd capacity, free (MB), time (**local**), bat, pct, tz, time_set | `INFO usb_detect=1 boot=0 up=12345 sysclk=80000000 ckin_div=0 fltcr1=0 sd=125844324352B free=119990MB time=2026-08-09 20:00:00 bat=3850mV pct=71 tz=480 time_set=1` |
| `SETTIME <unix> [tz_minutes]` | Set the RTC time from a Unix-epoch value (UTC). Optional `tz_minutes` also refreshes the stored timezone offset on every PC sync | `TIME set to 2026-08-09 20:00:00` |
| `SETTZ <minutes>` | Set the UTC offset only, e.g. `SETTZ 480`; negative for west (stored in backup domain, default +480 = UTC+8) | `TZ set to 480` |
| `TIME` | Raw RTC diagnostic: TR/DR/SSR/CR/ISR registers, LSE/LSION status, BKP1 (packed tz + time_set) | `TR=224023 DR=235114 SSR=F3 CR=0 ISR=37 LSE=1 LSION=1 BKP1=A5A5A5A5` |
| `REC` | Manually start recording | `REC started seq=1 name=0:/REC-20260809-1925.WAV` |
| `STOP` | Manually stop recording (flushes file) | `AUTO stop err=0 bytes=158432 rate=21056` |
| `CIRC` | Report free space, then run one delete-oldest pass (see §4.1) | `CIRC free_before=119990MB deleted=0 free_after=119990MB` |
| `DELOLDEST` | Delete the single oldest completed recording (see §4.1) | `DELOLDEST deleted=1` |
| `LIST` | List files on SD root with sizes | `LIST mount=0` then `  REC062.WAV 158476` ... `LIST done` |
| `DOWNLOAD <file>` | Raw file download (see §5.1) | `DLSTART <size>` ... `DLEND read=<n> wr=<n>` |
| `DL2 <file>` | Legacy chunked ACK download (see §5.2) | `DLSTART <size>` ... `DLEND read=<n>` |
| `GET2 <offset> <file>` | Resumable CRC32-verified continuous CDC export | `GET2START ...`, raw payload, `GET2END ...`, `BENCH e2e ...` |
| `BULK2 <offset> <file>` | Preferred resumable WinUSB bulk export (see §5) | CDC: `BULK2READY ...`; WinUSB: `GET2START ...`, payload, completion trailer |
| `BULKSPEED` | Transfer a deterministic 2 MiB WinUSB benchmark payload | Same framing as `BULK2`, with `mode=bench` in the CDC ready line |
| `SPEED` / `SDSPEED` | Measure CDC transmit / SD multi-block read throughput | `BENCH usb ...` / `BENCH sd ...` |
| `DFU` | Enter USB DFU bootloader for flashing | (device re-enumerates as DFU) |
| `MOUNT` | Mount the SD filesystem | `MOUNT fr=0` (0 = OK) |
| `LTEST` | Long-filename diagnostic: create/readdir/rename/delete a long test name | `LTEST mount=0 ...` |
| `SD` | Init SD card | `SD init=OK cap=125844324352B` |
| `CHECK` | Validate a recording file | — |
| `CAPT` | Diagnostic capture test | — |
| `DBG` | Debug: step marker, fault PC, reset cause | `DBG step=0 fstep=0 pc=...` |
| `DMAT` | DMA capture diagnostic (RMS/levels) | `DMAT cnt=... rms=...` |
| `DUAL` | Dual-mic diagnostic | `DUAL u1=... u2=... corr=...` |
| `RAW` | Raw filter-output diagnostic | `RAW rms=... zcr=... peak=...` |
| `ITST` / `SAMP` | Interrupt/polling diagnostics | — |
| `MBR` / `WRITE` / `SDWRITE` | SD diagnostic read/write tests | — |

### 4.1 Circular recording (delete-oldest)

While recording, free space on the SD volume is checked every **30 s** and again at record
start. When free space drops below **64 MB**, the oldest **completed** recordings are deleted
until free space is back to >= 64 MB. Deletion order: sequence-name `REC###.WAV` files first,
then timestamp-named files oldest-first. The in-progress recording file is never deleted.
This requires the SD volume to be persistently mounted at boot (§3).

- `CIRC` runs one free-space check + delete pass manually: `CIRC free_before=119990MB
  deleted=0 free_after=119990MB`.
- `DELOLDEST` deletes the single oldest completed recording:
  `DELOLDEST deleted=1` (`deleted=0` when nothing was removed).
- **PDM overflow self-recovery:** after a long loop block the audio read path (`pdm_dma_read`)
  resyncs and resumes at full rate automatically. A short audio gap may occur, but the
  recording file remains valid.

## 5. Audio download protocol

Use `BULK2` when available. The desktop sync tool automatically tries `BULK2`,
falls back to `GET2`, then uses legacy `DL2` only for older firmware.

### 5.0 `BULK2 <offset> <file>` (preferred)

1. Host sends `BULK2 0 REC062.WAV\r\n` over CDC.
2. Device replies `BULK2READY size=... offset=... length=...`, disconnects CDC,
   and enumerates as VID/PID `0483:5741` with bulk IN `0x81` and bulk OUT `0x02`.
3. Bulk IN sends a `GET2START` line, exactly `length` binary payload bytes, a
   `GET2END sent=<n> crc32=<crc>` line, then a `BENCH bulk ...` line.
4. Host validates byte count and CRC32 before sending `DONE\n` to bulk OUT.
5. Device returns to CDC PID `0x5740`. A partial destination file can be resumed
   by sending its current length as the next offset.

The completion trailer is appended to the final source buffer and sent in the
same USB transfer, so payload/trailer framing remains deterministic. The STM32L4
bulk IN endpoint deliberately uses the reliable single-PMA-buffer path; the HAL
double-buffer path was rejected after real-hardware tests found block-boundary
corruption without a throughput benefit.

### 5.0.1 `GET2 <offset> <file>` (CDC fallback)

`GET2` uses the same metadata, CRC32, resume semantics, and completion trailer,
but keeps the normal CDC interface active. It is the automatic fallback when the
dedicated WinUSB interface is unavailable.

### 5.1 `DOWNLOAD <file>` (raw, legacy)

Firmware behavior:
1. `DOWNLOAD <fname>` → prints `DL mount=<fr>\n`, opens the file, prints `DLSTART <size>\n`.
2. Streams the whole file as raw bytes (512 B chunks) with no flow control.
3. Prints `DLEND read=<total> wr=<written>\n`.

**Limitation:** no flow control. The USB CDC layer's `USB_CDC_TRANSMIT_TIMEOUT` (build flag,
default 10000 ms) can cause the transmit path to abort on long transfers. Prefer `DL2`.

### 5.2 `DL2 <file>` (chunked ACK, preferred)

Firmware behavior (exact protocol):

```
Host -> "DL2 REC062.WAV\r\n"
Dev  -> "DLSTART 158476\n"
Dev  -> <512 bytes of file data>   (then flush)
Host -> <any single byte, e.g. 'G'>     (ACK: ready for next chunk)
Dev  -> <512 bytes> ... repeat
Dev  -> "DLEND read=158476\n"
```

Details:
- After each 512-byte chunk the firmware flushes and waits for **any** received byte
  (5 s timeout → `DL2 ACK timeout`). The received byte(s) are consumed and ignored.
- The last chunk may be < 512 bytes.
- The host accumulates bytes until the advertised `DLSTART <size>` is reached.
- Verifying the `DLEND` line after the raw data confirms a clean transfer.

Host rules (see `out/download2.ps1`):
- Parse `DLSTART <size>` from the initial text.
- Read until the size is reached; after each read burst that pauses (~60 ms idle), send `G`.
- Save the first `size` bytes as the file; ignore trailing `DLEND ...` text.

### 5.3 Why DL2 exists

The CDC transmit path busy-waits when the host reads slowly. With no flow control, the
host's ~11.5 kB/s virtual-baud read can outlast `USB_CDC_TRANSMIT_TIMEOUT`, aborting the
transfer. The per-chunk ACK gives the firmware a natural pause so the USB queue drains and
the transmit-complete IRQ never stalls.

## 6. USB DFU entry

Two ways to enter the ST DFU bootloader for flashing:

1. **Software (recommended):** open the serial console and send `DFU\n`. The firmware jumps
   to the ROM bootloader (0x1FFF0000). Flash with:
   ```
   dfu-util -a 0 -s 0x08000000:leave -D .pio/build/dayvault/firmware.bin
   ```
2. **Hardware:** hold BOOT (SW2) + press RST. Use the same `dfu-util` command.

## 6.5 Battery and RTC behavior

- **Battery**: `INFO` reports `bat=..mV pct=..`. The battery is read via ADC1 on PA0
  (1 M/1 M divider → terminal = ADC × 2), calibrated against VDDA using the internal
  VREFINT reference (factory calibration at 0x1FFF75AA) and 8-sample averaged.
  `pct` is linear 3.0 V→0 %, 4.2 V→100 %.
- **RTC**: driven by the 32.768 kHz LSE crystal (PC14/PC15) in the backup domain, so it
  keeps running across resets and STOP sleep (VBAT = 3.3 V rail). Set it with
  `SETTIME <unix> [tz_minutes]` (UTC epoch; the optional offset also refreshes the stored
  timezone). It is not battery-backed independently of the 3.3 V rail.
- **Low-battery sleep**: with USB detached, if the battery stays below 3.0 V for 3 s the
  device enters **STOP mode**. It wakes every 4 s (RTC wake-up timer) to refresh the IWDG
  and re-check; it re-enters STOP until the voltage rises above 3.3 V or USB is attached
  (USB attach also wakes it via PA9 EXTI). USB-attached (charging) never triggers sleep.
  During STOP, the RTC keeps the time; clocks are re-initialized (`SystemClock_Config`)
  on wake.
- **Recording start/stop** is debounced (100 ms stable) so a noisy USB-detect pin or a
  threshold hover cannot flip recording repeatedly.

## 7. Important firmware facts (debugging)

- PDM audio config (stable): CKOUT divider 13 → ~2.08 MHz mic clock, OSR 96, SINC3,
  21.1 kHz output, U1 mic only. Voice DSP chain: 150 Hz HPF → 250 Hz low shelf (−4 dB)
  → 3 kHz +10 dB + 2 kHz +5 dB presence → slow noise-aware AGC.
- IWDG watchdog (5 s) is armed in `setup()` and **must** be kicked in `loop()` —
  `dbg_iwdg_kick()`. Removing it causes a 5 s reset loop (USB repeatedly reconnects).
- The `g_dbg_step` / `g_fault_pc` noinit debug state survives resets; read via `DBG`.
- MSC / USB-disk export was tried and reverted: STM32duino core has no official MSC support;
  the composite and MSC-only experiments did not produce a usable drive. Serial download
  (`DL2`) is the supported export path.
# Performance benchmarks

The firmware keeps the legacy benchmark text and also emits one
machine-readable result line:

```text
BENCH <usb|sd|e2e> bytes=<count> ms=<elapsed> kib_s=<rate> crc32=<8-hex-digits>
```

- `SPEED` streams a deterministic 2 MiB pattern and reports `BENCH usb`.
- `SDSPEED` reads up to 2 MiB from the largest SD file and reports `BENCH sd`.
- `GET2 <offset> <filename>` reports `BENCH e2e` after `GET2END`.
- `BULKSPEED` reports `BENCH bulk` after the 2 MiB binary payload.

Validated on the DayVault prototype (2026-08-11): SD multi-block read about
`671 KiB/s`, continuous CDC about `235 KiB/s`, and reliable WinUSB bulk about
`225-227 KiB/s`. Three consecutive 2 MiB WinUSB transfers matched both host and
device CRC32; a 4,508,018-byte real recording and a resumed transfer also matched.

CRC32 uses the IEEE polynomial and covers exactly the payload byte count in the
same result line. Host tools should validate both byte count and CRC before
using a performance result.
