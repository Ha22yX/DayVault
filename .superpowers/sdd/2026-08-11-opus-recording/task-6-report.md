# Task 6 Report: Direct Production Ogg Opus Recording

Date: 2026-08-11
Branch: `codex/dual-mic-audio`
Baseline: `834ca73 fix(audio): release failed pipeline ownership`

## Scope

Task 6 replaces only the production continuous recorder with direct mono Ogg Opus at
16 kHz and a target 24 kbit/s. Diagnostic commands that intentionally create WAV test
captures remain intact. No PCM or WAV file is created alongside a production recording.

The production path is now:

`pdm_dma_read_dual -> AudioPipeline -> DayVaultOpusEncoder -> OggOpusWriter -> FatFs`

## RED Evidence

1. Source contract before implementation:

   `python -m pytest tools/dayvault_sync/tests/test_firmware_memory.py -q`

   Result: 2 failed, 9 passed. The expected failures were the absent
   `CompressedRecorder.cpp` paired-Opus path and the temporary
   `dayvault_opus_encoder_link_probe` still present in PlatformIO and the encoder adapter.

2. Immediate one-second Ogg page behavior before implementation:

   `pio test -e native -f test_ogg_opus`

   Result: 2 failed, 11 passed. Packet 50 left only the two header pages, and the configured
   sink failure did not occur until packet 51.

3. Narrow DSP diagnostics contract before implementation:

   `pio test -e native -f test_audio_pipeline`

   Result: compile failure because `AudioPipelineStats` had no fusion, noise-reduction, or
   leveler snapshots.

## GREEN Evidence

- Source contracts: 13 passed.
- Ogg Opus writer: 13 passed, including immediate packet-50 page output and same-granule
  empty EOS for an exact page boundary.
- Audio pipeline: 11 passed, including the new DSP snapshot contract.
- Real encoder SRAM2 partition: passed with a 24 KiB encoder limit while retaining a
  non-overlapping 8 KiB Ogg page region.
- Focused native matrix: PDM rate/ring, Opus arena/encoder, Ogg writer, pipeline, fusion,
  noise reduction, and leveler all passed.
- ARM build: `pio run -e dayvault -j 1` succeeded.
- `git diff --check` succeeded; line-ending conversion warnings are repository-local Git
  policy warnings, not whitespace errors.

## Implementation

- Added `CompressedRecorder`, a fixed-memory state machine with explicit result codes and
  current/last statistics.
- Start is transactional: mount, circular deletion, free-space verification, `.OPUS`
  creation, encoder setup, non-overlapping page reservation, Ogg header writes, header
  sync, DSP reset, PDM start, and 32 paired startup samples discarded. Any failure stops
  PDM if needed, releases pipeline/encoder state, closes, and deletes the incomplete new
  file.
- Poll reads at most 128 paired samples. Frames are encoded at the existing fixed Opus
  configuration and passed to Ogg with exact valid-sample counts.
- Packet 50 immediately writes the completed one-second page. Checkpoint waits until a new
  page exists, then calls `f_sync`, avoiding a premature one-second sync that would delay
  durability until the next interval.
- Stop halts PDM, drains paired samples, finishes DSP padding/trimming, writes EOS, syncs,
  closes, ends the encoder, and then renames timestamp files using
  `valid_input_samples / 16000`.
- New sequence and timestamp names use `.OPUS`. Circular deletion accepts `.OPUS` and
  legacy `.WAV` case-insensitively and continues to skip the active basename.
- `GET2`, `DL2`, `BULK2`, `BULKSPEED`, `SDSPEED`, and `SPEED` stop/finalize recording before
  claiming SRAM2. Production start refuses while that workspace is leased.
- Added `OPUSSTAT` without file or PDM side effects. It reports bitrate, sample rate,
  frames, pages, valid samples, bytes, encoder errors, maximum encode time, workspace use,
  paired overruns, fusion weights/faults, NR state/gain, and leveler gain/limiter count.
- Removed the temporary linker-retention flag and probe function. Production references
  root the codec naturally.
- Added `-fstack-usage` so ARM stack reports remain reproducible.

## Resource Audit

From `arm-none-eabi-size -A firmware.elf` and the linker map:

| Resource | Used | Limit | Remaining |
| --- | ---: | ---: | ---: |
| Flash (PlatformIO) | 218,120 B | 262,144 B | 44,024 B |
| SRAM1 data + BSS + noinit | 71,216 B | 131,072 B | 59,856 B |
| SRAM1 including 1,536 B linker heap/stack reservation | 72,752 B | 131,072 B | 58,320 B |
| SRAM2 `.ram2` workspace | 32,768 B | 32,768 B | 0 B statically |

SRAM2 is intentionally shared, not simultaneous: encoder arena and Ogg page while
recording, transfer buffers while exporting. The real-codec host test proves the encoder
starts within the 24 KiB limit and leaves the 8 KiB page region non-overlapping.

## Stack Audit

The clean ARM build generated 356 `.su` files. Relevant worst entries include:

- `silk_encode_frame_FIX`: 10,752 B, dynamic.
- `silk_pitch_analysis_core`: 1,016 B, dynamic.
- `opus_encode_native`: 880 B, dynamic.
- `loop`: 4,968 B, static.
- `CompressedRecorder::encode_frame`: 24 B, static.
- `DayVaultOpusEncoder::encode`: 24 B, static.

A conservative simultaneous recording call-path sum includes the full `loop` frame,
recorder/pipeline callback frames, Opus wrappers, restricted-SILK frame storage, and the
deepest pitch branch: approximately 18,656 B. Subtracting this from the 58,320 B remaining
after static sections and linker reservation leaves approximately **39,664 B** conservative
SRAM1 headroom, exceeding the required 8 KiB by 31,472 B. Interrupt stack use is not fully
represented by `.su`, but this margin is large relative to the requirement.

## Symbol Audit

Present in the final ELF:

- `CompressedRecorder::{start,poll,checkpoint,stop}`
- `DayVaultOpusEncoder::{begin,encode,end}`
- `OggOpusWriter::{begin,add_packet,finish}`
- `opus_encode`, `opus_encode_native`, `silk_Encode`, `silk_encode_frame_FIX`
- `g_transfer_workspace` at `0x10000000`, size `0x8000`

Absent from the final ELF/source configuration:

- `dayvault_opus_encoder_link_probe`
- Opus decoder creation symbols
- DNN, DRED, and OSCE symbols
- libc `malloc`, `calloc`, or `realloc` references from `libopus.a`; the archive references
  the DayVault arena hooks instead.

## Safety Scan

- No `0x1FFF7800` reference was found in firmware source.
- No `FLASH_OPTR`, option-byte programming, or boot-bit modification was found.
- Software DFU remains `SYSTEM_MEMORY_BASE 0x1FFF0000u`.
- USB VID remains `0x0483` in the board metadata and WinUSB descriptors.
- Production `CompressedRecorder` contains no `wav_build_header`, `f_lseek`, mono
  `pdm_dma_read`, or parallel PCM/WAV write path.
- Intentional WAV diagnostics remain in `main.cpp`.
- No flashing, serial session, microphone capture, or board access was performed.

## Self-Review

- Startup cleanup covers failures before and after file creation, header writes/sync,
  pipeline ownership, PDM start, and paired discard.
- Ogg write callbacks check both `FRESULT` and exact byte count.
- Checkpoint does not seek or rewrite headers and cannot mark a checkpoint complete before
  a new audio page is written.
- Error stops preserve the latest durable prefix and never fall back to WAV.
- Low-battery stop and USB attach/detach still route through the production stop/start
  wrappers. The main loop continues to service the watchdog.
- Timestamp collision suffixes and RTC-unset sequence naming are preserved with `.OPUS`.
- Shared SRAM2 ownership is explicit for all named transfer and benchmark paths.

## Concerns And Follow-Up

- Per instruction, this pass did not exercise a physical board, microphones, serial port,
  DFU, or flashing. `OPUSSTAT max_us` and real-time overrun behavior still require a later,
  separately authorized hardware validation pass.
- The upstream fixed-point libopus build emits existing GCC warnings around float-cast
  fallbacks and a SILK zero-length-array diagnostic; native codec tests and the ARM link
  succeed, and these warnings were not introduced by Task 6.
- The linker emits its existing RWX load-segment warning. Section limits and symbols were
  inspected directly, but linker permission cleanup is outside Task 6.
- FatFs startup failure branches are source-reviewed and transactionally implemented; no
  native fake-FatFs fault-injection harness exists in this task.

## Commit

Requested commit message: `feat(rec): save production audio directly as Opus`
