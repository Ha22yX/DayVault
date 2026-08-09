# DayVault Serial Command Reference

For future firmware/software development. This documents the USB CDC serial interface of the
DayVault voice recorder firmware (STM32L452), including the audio download protocol.

## 1. Connection

- USB CDC virtual serial port, **115200 baud**, 8N1.
- On Windows the device enumerates as `COMx` ("USB 串行设备", VID `0x0483`, PID `0x5740`).
- Commands are ASCII text lines terminated by `\n` or `\r`. Case-sensitive.
- The serial console is only active when USB is attached AND the device is NOT in MSC mode
  (MSC disk export was removed; serial is the only USB function).

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
| `INFO` | Status dump: usb_detect, boot(BOOT0), up(millis), sysclk, ckin_div, fltcr1, sd capacity, time (**local**), bat, pct, tz, time_set | `INFO usb_detect=1 boot=0 up=12345 sysclk=80000000 ckin_div=0 fltcr1=0 sd=125844324352B time=2026-08-09 20:00:00 bat=3850mV pct=71 tz=480 time_set=1` |
| `SETTIME <unix> [tz_minutes]` | Set the RTC time from a Unix-epoch value (UTC). Optional `tz_minutes` also refreshes the stored timezone offset on every PC sync | `TIME set to 2026-08-09 20:00:00` |
| `SETTZ <minutes>` | Set the UTC offset only, e.g. `SETTZ 480`; negative for west (stored in backup domain, default +480 = UTC+8) | `TZ set to 480` |
| `TIME` | Raw RTC diagnostic: TR/DR/SSR/CR/ISR registers, LSE/LSION status, BKP1 (packed tz + time_set) | `TR=224023 DR=235114 SSR=F3 CR=0 ISR=37 LSE=1 LSION=1 BKP1=A5A5A5A5` |
| `REC` | Manually start recording | `REC started seq=1 name=0:/REC-20260809-1925.WAV` |
| `STOP` | Manually stop recording (flushes file) | `AUTO stop err=0 bytes=158432 rate=21056` |
| `LIST` | List files on SD root with sizes | `LIST mount=0` then `  REC062.WAV 158476` ... `LIST done` |
| `DOWNLOAD <file>` | Raw file download (see §5.1) | `DLSTART <size>` ... `DLEND read=<n> wr=<n>` |
| `DL2 <file>` | Chunked ACK download — **preferred** (see §5.2) | `DLSTART <size>` ... `DLEND read=<n>` |
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

## 5. Audio download protocol

Two download methods exist. **Use `DL2`** (chunked ACK) — it is robust against the USB CDC
transmit-timeout issue that breaks long `DOWNLOAD` transfers. A working host script lives at
`out/download2.ps1` (`.\out\download2.ps1 -File REC062.WAV`).

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
