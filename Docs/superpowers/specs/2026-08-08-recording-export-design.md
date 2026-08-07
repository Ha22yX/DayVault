# DayVault Stereo Recording + USB MSC Export Design

Date: 2026-08-08
Status: Approved (pending implementation)
Scope: Firmware recording feature — dual-microphone PDM capture to stereo WAV on an exFAT microSD card while USB is disconnected; on USB attach, stop recording, finalize the file, unmount FatFs, and expose the whole card as a USB MSC read-only volume to Windows. On USB detach, remount and resume recording.

## 1. Motivation and Behavior

DayVault is a wearable voice recorder. The user's requested behavior:

- **No USB attached**: start recording automatically on power-up (no button needed).
- **USB attached**: stop recording, close/save the current WAV file, unmount the FAT filesystem, hand the microSD card to USB MSC, so Windows sees it as a removable disk and can read the recordings.
- **USB detached again**: remount the card and resume recording.

Recordings are stereo WAV (2-ch PDM microphones) at 16 kHz / 16-bit. The card is a pre-formatted exFAT microSD card. File names use an incrementing sequence (`REC001.WAV`, `REC002.WAV`, ...) for now; RTC timestamps will replace them when the RTC driver lands.

## 2. Hardware Context (from Docs/02, Docs/03)

| Signal | Pin | Peripheral |
| --- | --- | --- |
| PDM clock | PC2 | DFSDM1_CKOUT |
| PDM data (shared) | PB12 | DFSDM1_DATIN1 |
| microSD CS/SCK/MISO/MOSI | PA4/PA5/PA6/PA7 | SPI1 |
| USB detect | PA9 | GPIO input |
| USB DP/DM | PA12/PA11 | USB FS |
| Battery sense | PA0 | ADC1 |
| LED | PA8 | GPIO |

Two SPH0655 microphones share one PDM data line. U1 SELECT=GND, U2 SELECT=1V8, so they drive **opposite clock edges** on the shared DATA line. STM32 DFSDM one-wire stereo uses two consecutive channels `x` and `x-1` fed from `DATINx`; the board routes to `DATIN1`, so:
- **Channel 1**: direct `DATIN1` reception, samples on one edge (mic U1).
- **Channel 0**: redirected to Channel 1's serial input, samples on the **opposite** edge (mic U2).
- Each channel feeds its own DFSDM filter + DMA stream.

**DV-001 (Docs/06)**: this stereo topology is saved in the schematic/netlist/PCB but **never validated on hardware**. Firmware must implement it, and bring-up must verify channel identity, simultaneous capture, gain, and relative delay. A mono fallback (Channel 1 only) is the escape hatch if stereo fails.

## 3. Architecture

```
+------------------------------------------------------------------+
| app.c superloop: state machine                                    |
|   IDLE/RECORDING <-> USB_ATTACH/USB_DETACH <-> MSC_EXPORT        |
+------------------------------------------------------------------+
| Pure-logic (host-tested):                                         |
|   wav (header build/patch) · ringbuf · rec_mgr (state transitions)|
+------------------------------------------------------------------+
| Hardware drivers (compile-only until board bring-up):             |
|   hw_spi_sd (SPI1 SD) · diskio_sd (FatFs glue) · hw_dfsdm (PDM)   |
|   hw_usb (existing PCD/USBD) + usbd_msc_storage (SD block dev)    |
+------------------------------------------------------------------+
| Third-party: FatFs R0.12c vendored to lib/FatFs (_FS_EXFAT=1)     |
+------------------------------------------------------------------+
```

Layers mirror the existing USB-DFU milestone (pure-logic host-tested, drivers compile-only).

## 4. Module Responsibilities

| Module | Responsibility | Testability |
| --- | --- | --- |
| `wav` | RIFF header build for 16 kHz/16-bit **stereo** PCM; patch size fields on close | host (golden bytes) |
| `ringbuf` | Lock-free byte ring buffer DMA→superloop | host (wraparound, overflow) |
| `rec_mgr` | State machine: IDLE→RECORDING on USB detach, RECORDING→STOPPING→MSC on USB attach; owns file sequence counter | host (transition table) |
| `hw_spi_sd` | SPI1 SD init, MBR parse, sector read/write; exposes both raw LBA (MSC) and partition LBA (FatFs) | compile |
| `diskio_sd` | FatFs glue over hw_spi_sd partition LBA | compile |
| `FatFs` | Vendored, `_FS_EXFAT=1`, `_MAX_SS=512`, `_VOLUMES=1` | compile |
| `hw_dfsdm` | DFSDM CKOUT + Channel 1 direct + Channel 0 redirected, opposite edges, two filters + two DMA streams, half-buffer callbacks | compile |
| `hw_usb` | Existing USB stack + register MSC class | compile |
| `usbd_msc_storage` | MSC block-device callbacks over hw_spi_sd raw LBA (read-only) | compile |
| `app` | Superloop orchestration, USB_DETECT debounce, file write pump | compile |

## 5. Data Flow (Recording)

1. `app_init`: mount FatFs, open `RECxxx.WAV`, write 44-byte stereo WAV header (data size 0), start DFSDM DMA.
2. DMA half-buffer complete → copy samples into `ringbuf` (ISR context).
3. Superloop drains `ringbuf`, **interleaves** Channel 0 + Channel 1 samples into L/R PCM, `f_write` to the open file.
4. On USB attach: stop DMA → drain remaining ringbuf → `f_lseek(0)` + patch WAV sizes → `f_sync` → `f_close` → `f_mount(NULL)` → register MSC → USB re-enumerates as MSC.

Sample math: 16 kHz × 2 bytes × 2 ch = 64 KB/s. DFSDM half-buffer 1024 samples/ch → 32 ms per block per channel. SPI1 @24 MHz writes 2 KB in ~80 µs — SD is never the bottleneck; DMA absorbs jitter.

## 6. State Machine (rec_mgr)

States: `R_IDLE`, `R_RECORDING`, `R_STOPPING`, `R_MSC`.

| Transition | Trigger | Action |
| --- | --- | --- |
| IDLE → RECORDING | USB_DETECT low, SD mounted | open file, start DFSDM |
| RECORDING → STOPPING | USB_DETECT high | stop DFSDM, finalize WAV, unmount |
| STOPPING → MSC | cleanup done | register MSC, USB re-enum |
| MSC → RECORDING | USB_DETECT low (after detach) | deregister MSC, remount, next REC file |
| any → IDLE | SD mount/fatal failure | log/abort, retry bounded |

Transitions are pure logic (host-tested); the `app` layer injects hardware actions.

## 7. USB MSC Details

- Composite config changes from CDC-only to **CDC + MSC**? No — in this milestone, when USB attaches we switch the device to **MSC-only** (recording stopped, FatFs unmounted). CDC remains available only in a debug capacity if desired. Decision: **MSC-only on attach** for the export milestone (simplest, matches "removable disk" behavior). CDC code stays in tree but is not registered during MSC mode.
- Because the ST USB library binds one device descriptor/class per `USBD_Init`, switching CDC→MSC on attach means: `USBD_Stop` → `USBD_DeInit` → re-`USBD_Init` with the MSC class + MSC descriptor, or use `USBD_RegisterClass`/`USBD_UnRegisterClass` for runtime switch. Implementer must pick the runtime-switch path that matches the installed library (verify `USBD_UnRegisterClass` exists in `usbd_core.h`); the descriptor set is swapped accordingly.
- `usbd_msc_storage.c`: `STORAGE_GetCapacity` reports total sectors (raw card capacity, incl. MBR partition); `STORAGE_Read`/`STORAGE_Write` operate on **raw LBA** via `hw_sd_read_sectors`. Write is denied (`USBD_MSC_LUN_WRITE_PROTECTED`) — read-only export.
- MSC buffer: one 512-byte sector buffer; `MAX_LUNS=1`.
- The SD card must NOT be accessed by FatFs while MSC is active (exclusive ownership handoff).

## 8. SD / exFAT Handling

- Card is pre-formatted exFAT. FatFs config `_FS_EXFAT=1`, `_MAX_SS=512`, `_USE_LFN=2`, `_VOLUMES=1`, `_FS_NORTC=1` (no RTC yet).
- SD driver reads MBR, finds the exFAT partition LBA offset, and stores it. FatFs `disk_read/write` use **partition-relative LBA** (offset added). MSC uses **raw LBA** (no offset). Both served by `hw_sd_read_sectors` with an explicit lba mode parameter.
- File sequence counter: derive from scanning existing `REC*.WAV` at mount (next = max+1); if scan fails, fall back to a boot counter. Simpler alternative for v1: always use boot counter (monotonic per power cycle). Decision: **scan existing files, next = max+1**, fallback boot counter.

## 9. Error Handling

- SD init failure: bounded retry (3 attempts) then IDLE + LED pattern; do not loop at full current.
- File open failure (card full/unwritable): stop recording, keep USB MSC available for user recovery.
- Buffer overrun (ringbuf full): drop samples, increment overrun counter; never block DMA.
- USB attach mid-write: finish current sector write, then stop (bounded).
- All blocking paths bounded; no unbounded waits.

## 10. Testing Strategy

| Layer | Verification |
| --- | --- |
| `wav` | host: stereo PCM header golden bytes (44 B, channels=2, byte_rate=64000), patch sizes after close |
| `ringbuf` | host: wraparound, overflow drop, read/write boundaries |
| `rec_mgr` | host: full USB attach/detach transition table with mock action hooks |
| `hw_spi_sd` / `diskio_sd` / `hw_dfsdm` / `usbd_msc_storage` | compile-only |
| Board bring-up (Docs/05) | no-USB: REC001.WAV grows on card; USB attach: Windows shows removable disk with REC001.WAV; USB detach: REC002.WAV starts |

## 11. Out of Scope (this milestone)

- RTC timestamps / file naming by time (sequence naming only).
- IMA ADPCM compression (PCM first).
- ADC battery monitoring and low-battery shutdown.
- Standby / low-power (superloop runs continuously).
- CDC + MSC composite (MSC-only on attach).
- Microphone gain calibration, DC removal, HPF (raw PCM).

## 12. Risks

- **DV-001 stereo unverified**: highest risk. Mitigation: mono fallback (Channel 1 only) via build flag if stereo bring-up fails; design keeps channels separable.
- **exFAT card partition layout**: SD driver must parse MBR correctly; validated on bring-up with the actual card.
- **MSC enumeration**: requires clean FatFs unmount before USB re-enum; timing validated on hardware.
