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
  - USB DETACHED (pin LOW) → recording starts automatically (`REC###.WAV`).
  - USB ATTACHED (pin HIGH) → recording stops, then enumerates as serial.
- Files: `REC###.WAV` (sequential numbers), 16-bit PCM WAV on exFAT microSD.
- Manual control is possible via `REC` / `STOP`.

## 4. Command list

| Command | Description | Example response |
|---|---|---|
| `INFO` | Status dump: usb_detect, boot(BOOT0), up(millis), sysclk, ckin_div, fltcr1, sd capacity | `INFO usb_detect=1 boot=0 up=12345 sysclk=80000000 ckin_div=0 fltcr1=0 sd=125844324352B` |
| `REC` | Manually start recording | `REC started seq=62` |
| `STOP` | Manually stop recording (flushes file) | `AUTO stop err=0 bytes=158432 rate=21056` |
| `LIST` | List files on SD root with sizes | `LIST mount=0` then `  REC062.WAV 158476` ... `LIST done` |
| `DOWNLOAD <file>` | Raw file download (see §5.1) | `DLSTART <size>` ... `DLEND read=<n> wr=<n>` |
| `DL2 <file>` | Chunked ACK download — **preferred** (see §5.2) | `DLSTART <size>` ... `DLEND read=<n>` |
| `DFU` | Enter USB DFU bootloader for flashing | (device re-enumerates as DFU) |
| `MOUNT` | Mount the SD filesystem | `MOUNT fr=0` (0 = OK) |
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
