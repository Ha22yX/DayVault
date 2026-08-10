# DayVault Opus Recording Design

## Goal

Store production recordings directly as standard Ogg Opus files at a target bitrate of
24 kbit/s. The recorder must not create or retain a parallel PCM/WAV original. The change
must preserve dual-microphone capture, adaptive mono fusion, noise reduction, level control,
timestamped names, circular deletion, low-battery finalization, and USB export.

## Constraints

- MCU: STM32L452RCT6, Cortex-M4 at 80 MHz, 256 KiB Flash, 128 KiB SRAM1 + 32 KiB SRAM2.
- Baseline firmware: 75,788 bytes Flash, 59,412 bytes SRAM1 BSS/data, and all 32 KiB SRAM2
  reserved by the USB transfer workspace.
- Opus accepts 8, 12, 16, 24, or 48 kHz input. The existing measured configuration produces
  about 16,026 Hz and therefore cannot be passed to an Opus encoder as exact 16 kHz audio.
- Normal recording is mono after the two microphone streams have been fused.
- The device may lose power while recording. Every durable prefix must remain a valid,
  recoverable Ogg stream even when the final EOS page is absent.
- No option bytes, boot configuration, DFU address, or USB VID may change.

## Approaches Considered

### 1. Standard Ogg Opus using the Xiph reference encoder (selected)

Vendor the official libopus 1.6.1 source under its BSD license, build fixed-point encoder
code without floating-point APIs or neural extensions, and write RFC 7845 Ogg Opus pages.
This produces files accepted directly by FFmpeg, VLC, browsers, and speech-to-text tooling.

### 2. Custom length-prefixed raw Opus packets

This is smaller to implement but produces a DayVault-specific container. Every consumer
would require a custom demuxer, seeking would be poor, and files would not be ordinary
`.opus` files. This approach is rejected.

### 3. Keep WAV and transcode on the computer

This minimizes MCU work but retains roughly 2.76 GB/day of PCM and violates the requirement
to save only the compressed recording. This approach is rejected.

## Audio Format

- Container: Ogg Opus, RFC 7845, extension `.OPUS`.
- Codec implementation: official Xiph libopus 1.6.1, pinned and checksummed.
- Input: one channel, signed 16-bit PCM, exactly 16,000 samples/s.
- Frame duration: 20 ms, 320 samples per Opus packet.
- Application: restricted SILK speech mode. Output packets remain standard Opus packets.
- Rate control: constrained VBR, target 24,000 bit/s.
- Complexity: 3 initially, selected for the all-day power budget. Runtime diagnostics expose
  worst encoding time so this can be raised only after measured headroom is known.
- DTX: disabled so low-level and distant speech is not intentionally discarded.
- In-band FEC and packet-loss redundancy: disabled because storage is reliable local media.
- Maximum packet allocation: 160 bytes. Bitrate is controlled by the encoder CTL, not by this
  safety bound.

At 24 kbit/s, codec payload is about 259.2 MB per 24 hours. Ogg page headers and metadata add
roughly a few megabytes per day, so a practical estimate is about 265 MB/day before filesystem
allocation overhead. Actual constrained-VBR size varies with content.

## Exact Capture Rate

Change the DFSDM output clock divider from 52 to 50 and the Sinc3 oversampling ratio from 96
to 100:

```
80,000,000 / 50 = 1,600,000 Hz PDM clock
1,600,000 / 100 = 16,000 Hz PCM
```

The 1.6 MHz PDM clock remains inside the SPH0655 normal operating range. One rate definition
continues to drive DFSDM setup, DSP coefficients, Opus setup, duration accounting, and tests.

## Data Flow

```
PDM DMA A/B
  -> paired 128-sample blocks
  -> adaptive fusion
  -> noise reduction (128-sample latency)
  -> speech leveler
  -> 320-sample Opus frame assembler
  -> libopus encoder
  -> Ogg page accumulator
  -> FatFs / TF card
```

The recorder drains paired DMA samples without blocking. A fixed-memory pipeline accepts
arbitrary read lengths, preserves channel pairing, and emits complete 128-sample DSP blocks.
The resulting mono stream is accumulated into 320-sample codec frames. Stop pads only the
encoder input needed to finish the final frame; the final Ogg granule position trims those
padding samples so playback duration matches real captured samples.

## Ogg Layout and Durability

1. Write and sync an `OpusHead` BOS page.
2. Write and sync an `OpusTags` page identifying DayVault and the encoder version.
3. Accumulate 50 packets (one second) per audio page. Each page has a valid Ogg CRC and a
   48 kHz granule position.
4. Write each completed page with one checked FatFs write.
5. Call `f_sync` at least once per second, after a completed page.
6. On clean stop, encode the partial final frame with zero padding, use the exact original
   sample count for end trimming, flush an EOS page, sync, close, and append duration to the
   filename.

On abrupt power loss, only an in-memory partial page can be lost. All previously synced pages
remain structurally complete. Many decoders accept a stream without an EOS page; a future
repair tool can add EOS without transcoding if needed.

## Memory Strategy

No per-frame allocation is permitted. The encoder state, Ogg page buffer, PCM frame, DSP
blocks, and packet buffer all use fixed storage. Recording and USB bulk export are mutually
exclusive, so the existing contiguous 32 KiB SRAM2 transfer workspace may be reused by the
encoder and page buffer while recording. Startup checks the actual libopus restricted-SILK
state size and fails recording cleanly if the pinned build no longer fits.

The firmware build is rejected if Flash exceeds 256 KiB or if static SRAM sections exceed
their linker regions. Stack-usage reports for codec entry points are inspected because
libopus uses variable-length scratch arrays in the fixed-point build.

## File and Protocol Changes

- Production names become `REC-YYYYMMDD-HHMM.OPUS` and
  `REC-YYYYMMDD-HHMM_HhMmSs.OPUS`, with the existing collision suffix behavior.
- RTC-unset fallback names become `REC001.OPUS` through `REC999.OPUS`.
- Circular deletion recognizes both new `.OPUS` files and legacy `.WAV` files, and never
  deletes the active file.
- `LIST`, `GET2`, `BULK2`, and WinUSB remain byte-oriented and require no wire-format change.
- The Windows sync parser and UI recognize and sort `.OPUS` recordings while retaining
  legacy `.WAV` compatibility.
- Add `OPUSSTAT` diagnostics for configured bitrate, frame/page counts, encoded bytes,
  encoder failures, maximum encode time, DMA overruns, and DSP state. It never records audio.

## Error Handling

- If encoder initialization, header writing, page writing, or syncing fails, stop PDM,
  close the file, preserve the last durable prefix, report a specific recorder error, and do
  not create a WAV fallback.
- If DSP cannot keep up, DMA overrun counters expose the loss. The recorder never silently
  changes codec bitrate or sample rate.
- Low battery uses the same clean finalization path before STOP mode.
- USB export commands first stop/finalize recording before borrowing the shared SRAM2
  workspace.

## Tests and Acceptance

- Golden tests for Ogg CRC, `OpusHead`, `OpusTags`, lacing, sequence numbers, granule
  positions, final trimming, and EOS.
- Real libopus host test: encode deterministic PCM and verify every return code and packet
  bound.
- Pipeline tests: arbitrary paired input chunking, NR latency flush, 320-sample framing, and
  exact final sample accounting.
- Filesystem/name tests accept `.OPUS` and legacy `.WAV`.
- Sync parser tests cover timestamp, collision, duration, sequence, and both extensions.
- Firmware build and linker map stay within Flash/SRAM limits.
- Static safety scan confirms no option-byte writes, System Memory remains `0x1FFF0000`, and
  USB VID remains `0x0483`.
- No board flashing, microphone sampling, or environmental recording is part of this
  implementation pass.

## References

- Xiph libopus 1.6.1 release: https://opus-codec.org/release/stable/2026/01/14/libopus-1_6_1.html
- Opus API 1.6: https://opus-codec.org/docs/opus_api-1.6.pdf
- Ogg Opus encapsulation: https://www.rfc-editor.org/rfc/rfc7845

