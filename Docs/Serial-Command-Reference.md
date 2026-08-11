# DayVault Serial Command Reference

This is the USB CDC serial interface for the DayVault STM32L452 firmware and its audio export path.

## Connection

- USB CDC virtual serial port, **115200 baud**, 8N1.
- CDC enumerates as VID/PID `0483:5740`. During a `BULK2` export it temporarily re-enumerates as the WinUSB device `0483:5741`, then returns to CDC.
- Commands are case-sensitive ASCII lines ending in `\n` or `\r`.

## Recording behavior

With USB detached (PA9 low), the device starts recording automatically. With USB attached, it cleanly stops recording before serial use. `REC` and `STOP` provide the same manual control.

New recordings are standard Ogg Opus `.OPUS` files: adaptive dual-microphone fusion to mono, exact 16 kHz PCM input, 20 ms frames, and target 24 kbit/s constrained-VBR restricted SILK encoding. New production recordings do not create a WAV or PCM original. See [09-Opus-Recording.md](09-Opus-Recording.md) for storage, finalization, and recovery behavior.

Timestamped recordings start as `REC-YYYYMMDD-HHMM.OPUS`, for example `REC-20260811-1430.OPUS`. On clean stop the name gains the duration: `REC-20260811-1430_5m32s.OPUS` or `REC-20260811-1430_1h04m03s.OPUS`. A collision is inserted before the duration: `REC-20260811-1430_1.OPUS` becomes `REC-20260811-1430_1_5m32s.OPUS`. If time is not set, the fallback is `REC001.OPUS`.

Completed Ogg pages contain at most 50 packets, about one second of audio. Newly completed pages are checkpointed about once a second. A clean stop drains the pipeline and encoder lookahead, writes Ogg EOS, syncs, closes, then renames timestamped recordings. An abrupt loss preserves the last synced prefix but can lose the page in progress.

Legacy `.WAV` files are still listed, downloadable, and eligible for circular deletion. They are not produced by new recordings.

## Commands

| Command | Description | Example response |
| --- | --- | --- |
| `INFO` | Device, SD, local-time, battery, and timezone status. | `INFO ... free=119990MB time=2026-08-11 14:30:00 bat=3850mV pct=71 tz=480 time_set=1` |
| `SETTIME <unix> [tz_minutes]` | Set UTC RTC time; optional timezone offset is stored for local names and `INFO`. | `TIME set to 2026-08-11 14:30:00` |
| `SETTZ <minutes>` | Set the timezone offset only. | `TZ set to 480` |
| `TIME` | Raw RTC diagnostic. | `TR=... DR=...` |
| `REC` | Start manual Opus recording. | `REC started seq=0 name=0:/REC-20260811-1430.OPUS` |
| `STOP` | Cleanly finalize the current recording. | `AUTO stop err=ok bytes=... rate=16000` |
| `OPUSSTAT` | Print Opus, Ogg, DSP, DMA, and recorder statistics. | `OPUSSTAT bitrate=24000 rate=16000 ...` |
| `LIST` | List SD-root files and sizes, including `.OPUS` and legacy `.WAV`. | `REC001.OPUS 123456` |
| `CIRC` | Report free space and run one delete-oldest pass. | `CIRC free_before=... deleted=0 free_after=...` |
| `DELOLDEST` | Delete the oldest completed `.OPUS` or legacy `.WAV` recording. | `DELOLDEST deleted=1` |
| `DOWNLOAD <file>` | Raw legacy CDC download. | `DLSTART <size>` then `DLEND read=<n> wr=<n>` |
| `DL2 <file>` | Chunked ACK CDC download. | `DLSTART <size>` then `DLEND read=<n>` |
| `GET2 <offset> <file>` | Resumable CRC32-verified CDC export. | `GET2START ...` then `GET2END ...` |
| `BULK2 <offset> <file>` | Preferred resumable WinUSB export. | `BULK2READY ...` |
| `BULKSPEED` | Send a deterministic 2 MiB WinUSB benchmark. | `BENCH bulk ...` |
| `SPEED` / `SDSPEED` | Measure USB or SD throughput. | `BENCH usb ...` / `BENCH sd ...` |
| `DFU` | Enter the STM32 ROM DFU bootloader. | Device re-enumerates as DFU. |
| `MOUNT` / `SD` | SD mount or card-init diagnostics. | `MOUNT fr=0` / `SD init=OK ...` |
| `LTEST` | Long-filename diagnostic. | `LTEST mount=0 ...` |
| `CHECK`, `CAPT` | Legacy recording/capture diagnostics; not production Opus recording. | Diagnostic output. |
| `DBG`, `DMAT`, `DUAL`, `RAW`, `ITST`, `SAMP` | Firmware, DMA, microphone, and capture diagnostics. | Diagnostic output. |
| `MBR`, `WRITE`, `SDWRITE` | SD read/write diagnostics. | Diagnostic output. |

### `OPUSSTAT` fields

`OPUSSTAT` prints one line with these fields:

```text
OPUSSTAT bitrate=<bps> rate=<hz> frames=<n> pages=<n> valid=<samples> bytes=<n>
         enc_err=<n> max_us=<us> workspace=<bytes> dma_overruns=<n>
         fusion_wa=<q15> fusion_wb=<q15> fusion_faults=<flags>
         level_gain=<q16> limiter=<n>
         active=<0|1> primary=<result> cleanup=<result> err=<result>
```

It reports the current or latest recorder state and does not start PDM capture or open a file.

## Circular recording

At record start and every 30 seconds while recording, DayVault checks free space. Below 64 MB it deletes completed recordings until free space is back to at least 64 MB. The active file is never deleted. Both `.OPUS` and legacy `.WAV` files participate; sequential `REC###` names sort before timestamp names.

Paired DMA capture resynchronizes after an overrun. A short audio gap can occur, but subsequent capture continues and the recording remains structurally valid.

## Export protocol

Use `BULK2` when available; the sync app falls back to `GET2`, then `DL2` for older firmware. These commands export opaque file bytes, so the same protocol works for `.OPUS` and legacy `.WAV` files.

`BULK2 <offset> <file>` announces `BULK2READY`, moves to the WinUSB interface, sends `GET2START`, exactly the requested payload, `GET2END sent=<n> crc32=<crc>`, and a `BENCH bulk ...` line. The host verifies size and CRC32, sends `DONE`, and the device returns to CDC. Supplying the existing local length as `offset` resumes a partial download.

`GET2` has the same payload metadata, CRC32, and resume semantics over CDC. `DL2` sends 512-byte chunks and waits for one host acknowledgement byte after each chunk. `DOWNLOAD` has no flow control and is retained only for compatibility.

## DFU

Send `DFU` to jump to the ROM bootloader at `0x1FFF0000`, then flash the application address:

```text
dfu-util -a 0 -s 0x08000000:leave -D .pio/build/dayvault/firmware.bin
```

The hardware route is hold BOOT, press RST, then use the same command. Do not write option bytes.

## Battery and RTC

The RTC stores UTC. The configured `tz` offset, default `480` (UTC+8), is used for recording names and the `INFO time=` field. A low battery with USB detached causes a clean recording finalization before STOP sleep. Recording start/stop is debounced for 100 ms.
