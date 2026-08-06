# DayVault Firmware Architecture Design

Date: 2026-08-06
Status: Approved (pending review)
Scope: Complete firmware for the DayVault wearable voice recorder, STM32L452RCT6.

## 1. Confirmed Decisions

| # | Decision | Choice |
| --- | --- | --- |
| D1 | Build system | PlatformIO + stm32cube framework, all-CLI (`pio run` / `pio run -t upload`). |
| D2 | Runtime structure | Bare-metal superloop + DMA ping-pong. No RTOS. |
| D3 | Implementation scope | All 8 modules implemented now; verification by host unit tests + compile-only for hardware drivers. |
| D4 | Verification strategy | Native (host) unit tests for pure logic; compile-only for hw_* layers; board bring-up later per Docs/05. |
| D5 | Low-power sleep | Standby + RTC periodic wakeup (default 30 s, configurable). USB attach detected on next periodic wake. |
| D6 | USB classes | Composite CDC (control/time-sync) + MSC (read-only export). MSC active only when recording stopped and FatFs unmounted. Fallback: CDC-only + custom binary file transfer, kept switchable. |
| D7 | Audio | 16 kHz / 16-bit mono PCM first, then IMA ADPCM encoder. Stereo DFSDM deferred to milestone M8 (hardware-gated). |

## 2. Architecture Layers

```
+-----------------------------------------------------------+
| Integration layer (bare-metal superloop)  app.c / main.c  |
+-----------------------------------------------------------+
| Pure-logic layer (no ST dependency -> host unit tests)     |
|   timeutil · wav · adpcm · segmgr · ringbuf ·             |
|   power_state (injected actions) · eventlog · usbproto    |
+-----------------------------------------------------------+
| Hardware-driver layer (HAL wrappers -> compile-only)       |
|   hw_rtc · hw_adc · hw_spi_sd · hw_dfsdm · hw_usb ·       |
|   hw_gpio · hw_pwr · hw_iwdg · diskio_sd                  |
+-----------------------------------------------------------+
| Third-party: FatFs R0.12c (vendored to lib/) ·            |
|   ST USB Device Library (auto-built) · STM32CubeL4 HAL    |
+-----------------------------------------------------------+
```

Principle: every computation expressible as integer/byte logic goes into the
pure-logic layer so it can be tested on the host without hardware. Hardware
drivers stay thin HAL wrappers.

### 2.1 Module responsibilities

| Module | Responsibility | Testability |
| --- | --- | --- |
| `timeutil` | UTC calendar math, path/filename building, UNSYNCED fallback naming | host |
| `wav` | RIFF header build/update, PCM + IMA ADPCM container | host |
| `adpcm` | IMA ADPCM encode/decode (step table, block format) | host |
| `segmgr` | 15 min segment rotation, sequence numbering, preallocation math | host |
| `ringbuf` | Lock-free circular buffer for audio -> SD path | host |
| `power_state` | State machine; transitions emit actions through injected callback struct | host |
| `eventlog` | events.csv line formatting | host |
| `usbproto` | CDC line protocol parse/serialize | host |
| `hw_rtc` | LSE start, calendar UTC, backup-register magic | compile |
| `hw_adc` | ADC1 BAT_SENSE long-sample averaging, VREFINT calibration | compile |
| `hw_gpio` | PA9 USB_DETECT EXTI with debounce | compile |
| `hw_spi_sd` | SPI1 SD init sequence, MBR parse, sector read/write | compile |
| `hw_dfsdm` | DFSDM CKOUT + Channel 1 filter + DMA ping-pong | compile |
| `hw_usb` | PCD + composite CDC/MSC glue | compile |
| `hw_pwr` | Standby entry, RTC periodic wake config | compile |
| `hw_iwdg` | Watchdog with bounded-timeout guarantees | compile |
| `app` | Superloop orchestration, event dispatch | compile |

## 3. Build Integration (verified facts)

- `stm32cube.py` auto-builds `Middlewares/ST/STM32_USB_Device_Library` Core + all
  classes (CDC/MSC/CompositeBuilder available), with `-I $PROJECT_SRC_DIR` and
  `-I $PROJECT_INCLUDE_DIR`. Project must provide `usbd_conf.h` (in `include/`)
  and `usbd_desc.c` + class glue (in `src/`).
- FatFs is NOT auto-built (script only processes `Middlewares/ST`). Vendor to
  `firmware/lib/FatFs/` (ff.c, diskio.c, ff_gen_drv.c + headers, ffconf.h).
- HAL PCD/DFSDM/SPI/IWDG/PWR sources verified present in framework package.
- `platformio.ini` gains a second environment `env:native` (`platform = native`)
  running Unity tests; `build_src_filter` selects only pure-logic sources.
- Clocks: MSI 48 MHz (already in use); USB kernel clock from MSI48 via
  `RCC_CCIPR`; RTC from LSE; DFSDM CKOUT clock source verified against RM during
  M4 implementation (PLLQ or MSI-derived).
- Linker script auto-generated for 262144 flash / 65536 RAM (already working).

## 4. Recording Pipeline

```
PC2 CKOUT --> SPH0655 pair (shared data line)
PB12 DATIN1 <-- TXU0202 level-shifted
  -> DFSDM Filter Channel 1 (mono first) -> DMA ping-pong
  -> ringbuf -> app superloop -> (ADPCM) -> wav writer
  -> FatFs -> SPI1 (PA4-7) -> microSD
```

- 16 kHz x 16-bit mono = 32 KB/s. Half-buffer 2048 samples -> 32 ms per block.
  SPI1 @24 MHz writes 2 KB in ~80 us: ~400x margin. SD never the bottleneck;
  DMA absorbs all jitter.
- Stereo (DV-001, hardware unverified): Channel 0 redirected to DATIN1 with
  opposite sampling edge, independent filter + DMA stream. Configured behind a
  build flag, enabled only in M8.
- SPH0655 has no enable pin; sleep is entered by stopping CKOUT. Low-power entry
  MUST stop DFSDM/CKOUT first (confirmed in Docs/04).

## 5. Power State Machine

States: BOOT, IDLE, RECORDING, RECORDING_LOW, STOPPING, STANDBY.

| Transition | Trigger | Action |
| --- | --- | --- |
| BOOT -> RECORDING | RTC valid, SD mounted, no USB, battery OK | open segment |
| BOOT -> IDLE | USB attached during boot | host management mode |
| BOOT -> STOPPING | battery critical at boot | clean shutdown |
| RECORDING -> RECORDING_LOW | battery <= 3.50 V (filtered) | log warning, reduce load |
| RECORDING_LOW -> RECORDING | battery > 3.55 V (hysteresis) | resume normal |
| RECORDING -> IDLE | USB attach | close segment, unmount, MSC |
| IDLE -> RECORDING | USB detach | remount, resume |
| RECORDING/LOW -> STOPPING | battery <= 3.30 V several samples | graceful stop |
| STOPPING -> STANDBY | cleanup done | Standby |
| STANDBY -> BOOT | RTC periodic wake | full rebuild |

Battery policy: 3.50/3.30/3.55 V with hysteresis, multi-sample filtering,
never act on a single transient. ADC settles ~250 ms after cold start
(500 kOhm divider x 100 nF), longest sample time, averaged, VREFINT calibrated.

STOPPING sequence: stop DFSDM/CKOUT -> drain DMA -> finalize WAV header ->
f_sync -> f_close -> f_unmount -> eventlog -> low-leakage GPIO config ->
Standby entry.

Standby: RTC keeps time on LSE; RTC wakeup alarm every WAKE_PERIOD (default
30 s, in dayvault.h). USB attach detected on next periodic wake. RAM is lost on
each Standby wake; state lives in backup registers + file system (boot counter,
RTC-valid magic). Every wake is a full rebuild — no state restore needed.

## 6. USB Behavior

- Composite descriptor: CDC (Interface 0) + MSC (Interface 1).
- CDC line protocol (pure-logic parser, host-tested):
  - `SYNC <unix>` -> atomically set RTC; reply `OK old=<ts> new=<ts>`
  - `TIME` -> return current UTC
  - `STAT` -> battery V, SD status, state, uptime s
  - `FLUSH` -> close current segment
- MSC is a READ-ONLY export volume, active only when recording stopped and
  FatFs unmounted (forbidden to expose writable MSC while FatFs writes — Docs/04).
  Recording + USB attach: cleanly close segment -> unmount -> hand card to MSC.
  Detach: remount -> resume recording.
- If composite proves too heavy on RAM/Flash, fallback to CDC-only with custom
  binary transfer protocol. Ownership handoff logic designed switchable.

## 7. File Structure and Segmentation

```
/DAYVAULT/
  2026/08/01/
    20260801T083000Z_0001.wav
    events.csv
```

- RTC kept in UTC internally; TZ applied only at export.
- 15-minute segments (configurable); segment preallocated; WAV header size
  fields updated every 10 s via f_sync.
- No valid time -> `/DAYVAULT/UNSYNCED/BOOT<counter>/` (boot counter from a
  backup register).
- events.csv appended: boot, reset cause, card failure, buffer overrun,
  low battery, USB attach/detach, time sync (old + new ts).

## 8. Watchdog and Error Handling

- IWDG enabled only after all blocking paths have bounded timeouts; fed every
  superloop iteration.
- SD init: bounded backoff retry (3 attempts). Repeated write failure -> clean
  stop capture, log, never loop at full current.
- Buffer overrun counter persisted per file/session and logged.
- Emergency buffer keeps current WAV header finalizable on low battery.

## 9. Testing Strategy

Native env + Unity (host):
- `wav`: PCM/ADPCM header golden bytes, size fields after header update
- `adpcm`: encode->decode round-trip error bound, step table constants
- `timeutil`: path/filename, calendar validity (leap years, month lengths),
  UNSYNCED naming
- `segmgr`: 15-min rotation, sequence increment, preallocation math
- `ringbuf`: wraparound, full/empty boundaries
- `power_state`: full event x state transition table with mock action hooks
- `usbproto`: command parse/serialize

Hardware drivers (hw_*, diskio_sd, USB glue): compile-only; behavior verified on
board using Docs/05 bring-up checklist.

## 10. Milestones

| M | Content | Verification |
| --- | --- | --- |
| M0 | Native test scaffolding + Unity + pure-logic skeleton | host green |
| M1 | RTC/LSE + backup magic + timeutil | compile + host timeutil |
| M2 | Battery ADC (VREFINT cal) + USB_DETECT EXTI debounce | compile |
| M3 | SPI1 SD + diskio + FatFs vendor + mount backoff + events.csv | compile |
| M4 | Mono PDM/DFSDM Ch1 + DMA ping-pong + ringbuf | compile |
| M5 | WAV writer + segments + 10 s sync + ADPCM encoder | compile + host all pure logic |
| M6 | USB composite CDC+MSC + time-sync protocol | compile + host usbproto |
| M7 | Power state machine integration + Standby/RTC wake + recovery | compile + host transition table |
| M8 | Stereo DFSDM redirect + board calibration | hardware; DV-001 |

## 11. Risks / Hardware-Gated Items

- Stereo DFSDM redirect (DV-001): unverified; mono-first, M8.
- Microphone polarity/gain/clock offset: calibrated on board.
- Composite CDC+MSC footprint: fallback to CDC-only defined.
- Battery thresholds: engineering starting points; tune against real cell.
- Storage reality: PCM 2.76 GB/day (>= 8 GB card); ADPCM 691 MB/day.
  ADPCM lands before stereo (pure logic, fully host-testable).
- Standby wake latency: default 30 s (configurable); acceptable per D5.
