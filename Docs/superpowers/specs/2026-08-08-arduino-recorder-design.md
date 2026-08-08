# DayVault Arduino Recorder — Design

Date: 2026-08-08
Status: Approved by user (2026-08-08)
Scope: Switch the DayVault recorder firmware to the STM32duino (Arduino) platform and
implement the core use case: USB detach auto-starts mono recording; USB attach auto-stops
recording and re-enumerates as CDC+MSC so the 128 GB exFAT card can be exported on Windows.

## 1. Goals and Non-Goals

### Goals

- All project-owned firmware code is rewritten from scratch in an Arduino (STM32duino)
  project. No old HAL/Cube C files are reused as project code.
- USB detach -> automatically start recording.
- USB attach -> automatically stop recording, finalize the WAV file, and expose the SD
  card as a mass-storage device (MSC) so Windows shows it like an external drive.
- USB enumerates as CDC (serial) + MSC (disk) composite: serial for debug output and the
  `DFU` command, MSC for file export.
- Mono 16 kHz / 16-bit PCM WAV recording on Channel 1 (PB12/DFSDM1_DATIN1). Stereo is a
  later milestone.
- Recording files named `REC###.WAV`, one file per recording session, sequential numbers.
- exFAT 128 GB SDXC card supported (FatFs `_FS_EXFAT=1` already configured).
- Repo reorganization: old HAL/Cube firmware and firmware-related docs move to a
  `legacy/hal-firmware` git worktree; main keeps only what the new development needs.

### Non-Goals (later milestones)

- Stereo two-microphone capture (M3).
- RTC synchronization and date-based file names (current files are sequential).
- Battery ADC / low-battery handling.
- Power-fail file closure beyond f_sync/f_close.
- PDM gain calibration, IMA ADPCM or Opus.

## 2. Decisions (from brainstorming Q&A)

| Decision | Choice |
| --- | --- |
| Code rewrite scope | All project code rewritten; framework libraries (HAL, ST USB device library, FatFs) used as dependencies |
| Recording format | Mono first, stereo later (current milestone mono) |
| Audio spec | 16 kHz / 16-bit PCM WAV, `REC###.WAV` sequential, one file per session |
| USB device shape | CDC + MSC composite (serial for debug/DFU command, disk for export) |
| File naming | Sequential `REC###.WAV`, no fake date names (RTC unreliable) |
| DFU entry | BOOT button (hardware) + CDC `DFU\n` command (software jump); NO boot-time PH3 auto-trigger |
| LED feedback | PA8: slow blink while recording, solid while idle/MSC, fast blink on error |
| MSC write policy | Read-only export volume (matches prior firmware, safest) |

## 3. Repository Reorganization (worktree)

### Target layout

```
C:\Users\Administrator\Desktop\DayVault\            <- main (new Arduino development)
|-- AGENTS.md                 keep (HARD RULES)
|-- README.md / README.zh-CN.md
|-- EDA/                      keep (hardware unchanged)
|-- Docs/
|   |-- superpowers/          keep (specs/plans directory)
|   |-- 01-Hardware-Overview.md   keep
|   |-- 02-MCU-Pinout.md          keep (pin table for new firmware)
|   |-- 03-Component-Pinout.md    keep
|   |-- 07-BOM.md                 keep
|   `-- 08-Net-Map.md             keep (net -> endpoint map)
|-- firmware/                 rebuilt as Arduino project
|   |-- platformio.ini
|   |-- boards/               reuse board json + linker script
|   |-- lib/FatFs/            library dependency (exFAT enabled)
|   |-- src/                  all-new Arduino source
|   `-- test/                 host unit tests
|-- .github/ .gitignore .gitattributes .superpowers/

C:\Users\Administrator\Desktop\DayVault-legacy\     <- legacy/hal-firmware worktree
|-- firmware/                 old HAL/Cube firmware (src/, hw_*.c, arduino_test, ...)
|-- Docs/
|   |-- 00-开发速查.md            old firmware dev flow
|   |-- 05-Bringup-and-Test.md   old bring-up procedure
|   |-- 06-Known-Issues.md       old blocker register
|   |-- 09-USB-DFU-Entry-Design.md  old software-DFU design
|   `-- superpowers/plans,specs  old firmware design + implementation plan
|-- (everything else preserved as-is)
```

### Steps

1. `git worktree add` branch `legacy/hal-firmware` at sibling directory
   `DayVault-legacy`.
2. In main, `git rm` old firmware code and firmware-related docs, commit.
   Old content remains fully preserved on the `legacy/hal-firmware` branch.
3. Develop the new Arduino firmware in main.

## 4. Firmware Architecture

Module layout in `firmware/src/` (one clear responsibility per module):

| Module | Responsibility |
| --- | --- |
| `main.cpp` | `setup()`/`loop()`; clock config (80 MHz MSI->PLL, USB 48 MHz HSI48, Range 1 first); init; main scheduling |
| `Config.h` | Pin and constant definitions (PDM / SD / USB / LED / BOOT) |
| `Recorder.h/.cpp` | State machine IDLE -> RECORDING -> STOPPING -> MSC |
| `PdmCapture.h/.cpp` | DFSDM1 mono PDM capture: Channel 1 direct (DATIN1/PB12), SINC3, OSR 128, CKOUT 2.048 MHz, DMA half/complete interrupts -> ring buffer |
| `RingBuf.h/.cpp` | Lock-free-ish ring buffer (pure logic, host-testable) |
| `SdCard.h/.cpp` | SPI1 block device: CMD0/8/55/41 init, CSD read, single/multi-block read+write (PA4-7) |
| `diskio.c` | FatFs disk glue (SdCard <-> FatFs) |
| `WavFile.h/.cpp` | WAV header build + size patch (pure logic, host-testable) |
| `UsbComposite.h/.cpp` | CDC+MSC composite via the ST USB device library bundled in STM32duino; MSC storage callbacks read raw SD blocks (read-only) |
| `Dfu.h/.cpp` | Software jump to ROM bootloader (0x1FFF0000) with stack-pointer validation |
| `Cmd.h/.cpp` | Serial command parse: `DFU\n` -> enter DFU; `INFO\n` -> print status |

### Data flow (recording)

```
PDM mic (PB12) -> DFSDM1 Filter1 -> DMA half/full IRQ -> RingBuf
   -> loop() pump -> FatFs -> SD card REC###.WAV (exFAT)
```

### USB switch behavior

```
USB detach (PA9 debounced) -> open REC###.WAV + start capture -> RECORDING
USB attach (PA9 debounced) -> stop capture -> drain RingBuf -> patch WAV header
   -> f_sync + f_close -> f_unmount (FatFs flushed)
   -> start CDC+MSC composite USB (Windows: disk + serial)
USB detach again -> stop USB -> f_mount -> record next REC###.WAV
```

### Clocks

Reuse the clock tree already proven in `arduino_test`: MSI 8 MHz -> PLL (M=1,N=20,R=2) ->
80 MHz SYSCLK; USB FS 48 MHz from HSI48. L4 requires VOS Range 1 before 80 MHz.

### RAM budget (64 KB total)

DFSDM ring buffer starts at 16 KB (enough for DMA double-buffer + disk pump), FatFs +
USB stack + SD buffers ~20 KB, remainder for stack/heap. Measured on hardware; shrink ring
buffer if needed. Reference: old HAL firmware used 48 KB (73%), so this must be verified.

### LED (PA8)

Slow blink while recording; solid while idle/MSC; fast blink on error.

## 5. Error Handling

| Scenario | Handling |
| --- | --- |
| SD init/mount fails at boot | No recording; enter MSC state; LED fast blink; retry on next USB cycle |
| SD write failure during recording | Stop capture, patch header, close file, mark failed (file kept), LED error, recover on USB cycle |
| Ring buffer overflow | Count overruns (queryable via serial), drop frames, never crash |
| USB attach during power risk | Periodic f_sync keeps FatFs flushed; header patched at close |
| Software DFU jump | Validate bootloader stack pointer before jump; loop forever (no jump) if invalid |

## 6. Testing Strategy

### Host unit tests (`native` env, Unity)

- `RingBuf`: write/read, wraparound, full/empty, ISR atomicity.
- `WavFile`: header bytes golden test (16 kHz / 16-bit mono), size patch.
- `Recorder`: state machine transitions for USB attach/detach event sequences.
- `Cmd`: `DFU\n` and `DFU\r\n` produce the enter-DFU event; unknown lines do not.

### Board verification (DFU flash + serial + disk)

1. Power on, USB detached -> recording starts (LED slow blink).
2. Plug USB -> recording stops -> Windows shows exFAT 128 GB disk + serial port.
3. Open disk: REC001.WAV copies out and plays.
4. Serial `DFU\n` -> device re-enumerates as STM32 BOOTLOADER -> can re-flash.

### Milestones

- **M0** Repo reorganization (worktree) + Arduino project skeleton compiles.
- **M1** Mono PDM -> SD (exFAT) recording with USB auto start/stop (this milestone).
- **M2** USB CDC+MSC composite + export verification.
- **M3** Stereo, RTC/battery, and follow-ups.

## 7. Safety (AGENTS.md compliance)

- Never modify option bytes / boot-config bits; this design touches flash only for the
  application image via the ROM DFU path.
- Software DFU entry goes through the validated `dfu_enter`-style jump; the BOOT button
  remains the hardware recovery path.
- Confirm DFU availability (`dfu-util -l`) before every DFU flash and confirm normal
  reset after flashing.
- No boot-time PH3 auto-DFU trigger in the new firmware.
