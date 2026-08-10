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

---

## Independent Review Fix Round 1/5

Date: 2026-08-11
Review input: `.superpowers/sdd/2026-08-11-opus-recording/task-6-independent-review.md`

All one Critical, five Important, and one Minor findings were addressed. This round did
not access hardware, serial, microphones, USB devices, DFU, option bytes, or flashing.

### RED Evidence

1. Ogg final boundary: `pio test -e native -f test_ogg_opus` produced 2 expected
   failures and 13 passes. The new 49-full-plus-short and 49-full-plus-zero tests observed
   50 packets on the non-EOS page instead of the required 49.
2. Encoder lookahead: `pio test -e native -f test_opus_encoder` failed to compile because
   the new `OpusFinalization.h` contract and 16 kHz lookahead getter did not exist.
3. Frozen PDM drain: `test_pdm_ring` failed to compile on the absent frozen-pair helper;
   the source suite reported 1 failure and 13 passes because no atomic freeze API existed.
4. Cleanup and rename: `test_recording_name` failed to compile on the absent bounded name
   formatter; source contracts reported 2 failures and 14 passes for missing cleanup state
   and final-destination collision probing.
5. Diagnostics: source contracts reported 1 failure and 16 passes at the first unguarded
   `DMAT` branch. The normal-close/OPUSSTAT extension subsequently reported 1 expected
   failure and 16 passes before those details were corrected.

### GREEN Evidence

- Source contracts: 17 passed.
- Focused native matrix: 82 passed across PDM rate/ring, Opus arena/real encoder, Ogg,
  audio pipeline, fusion, noise reduction, leveler, and recording-name policy.
- ARM build: `pio run -e dayvault -j 1` succeeded in 55.04 seconds.
- `git diff --check` passed; Git emitted only the repository's line-ending policy warnings.

### Fixes

- Ogg keeps an ordinary full packet 50 immediate. A short or zero-valid packet that would
  occupy slot 50 first flushes the prior 49 full packets, then remains with all following
  padding packets on the EOS page. Tests validate CRC, page flags, sequence numbers,
  packet counts, and exact nondecreasing granules. The corrected one-second case uses
  `50 * 320` captured samples and granule `312 + 16000 * 3 = 48312`.
- `DayVaultOpusEncoder` exposes the native 16 kHz lookahead and derives 48 kHz pre-skip as
  exactly three times that value. A pure finalization planner subtracts zero padding already
  present in a partial 320-sample frame, then requests enough additional all-zero frames to
  cover the remaining delay. Recorder padding packets use `valid_samples = 0`, so EOS
  granule remains the captured-input duration and cannot retreat behind a prior page.
- `pdm_stop_and_freeze()` runs under the IRQ lock, stops both DFSDM writers, accounts
  pending TC flags into exact paired producer snapshots, freezes those counters, and only
  then disables DMA/IRQs and clears residual flags. Frozen paired reads use zero margin;
  the pure ring test proves the final 64 samples withheld from a live writer are drainable.
- Failed-start cleanup now records the primary failure separately from close/unlink cleanup
  failure, clears each ownership flag only after its FatFs operation succeeds, and retries
  pending cleanup before any later start resets state. OPUSSTAT exposes primary, cleanup,
  and reported outcomes. Normal close failures also retain truthful open state.
- Every PDM claimant (`DMAT`, `ITST`, `SAMP`, `CAPT`, `DSCAN`, `NF`, `DUAL`, `RAW`)
  finalizes an active production recording before reconfiguration. All blocking diagnostic
  acquisition and scan loops service IWDG, including CAPT and DSCAN. Intentional diagnostic
  WAV capture remains unchanged apart from ownership and watchdog handling.
- Timestamp recording keeps one stable RTC stem. Final rename searches with `f_stat` for a
  free destination and places collision syntax before duration, for example
  `REC-20260811-143025_2_1m02s.OPUS`, before calling `f_rename`.

### Resource And Symbol Audit

Fresh `arm-none-eabi-size -A` and linker-map results:

| Resource | Used/reserved | Limit | Remaining |
| --- | ---: | ---: | ---: |
| Flash (PlatformIO) | 219,288 B | 262,144 B | 42,856 B |
| SRAM1 `.data + .bss + .noinit` | 71,880 B | 131,072 B | 59,192 B |
| SRAM1 including 1,536 B heap/stack linker reservation | 73,416 B | 131,072 B | 57,656 B |
| SRAM2 `.ram2` transfer workspace | 32,768 B | 32,768 B | 0 B static |

`g_transfer_workspace` remains at `0x10000000`, size `0x8000`. Required production
symbols remain in the ELF: recorder start/poll/checkpoint/stop, encoder begin/encode/end,
Ogg begin/add/finish, `opus_encode_native`, `silk_Encode`, and
`silk_encode_frame_FIX`. Decoder creation, DNN/DRED/OSCE, and temporary linker-probe
symbols are absent. The Opus archive has no malloc/calloc/realloc dependency and uses only
the DayVault arena free hook; unrelated framework libc allocator symbols remain in the ELF.

### Stack Audit

The clean ARM build generated 358 `.su` files. Relevant entries include:

- `loop`: 4,968 B static.
- `rec_stop`: 248 B static.
- `CompressedRecorder::stop`: 24 B static.
- `CompressedRecorder::drain_encoder_lookahead`: 24 B static.
- `opus_encode_native`: 880 B dynamic.
- `silk_Encode`: 184 B dynamic.
- `silk_encode_frame_FIX`: 10,752 B dynamic.
- `silk_pitch_analysis_core`: 1,016 B dynamic.

The observed deepest recording path sums to about 18.5 KiB. Rounding this upward to a
19 KiB conservative runtime allowance leaves `57,656 - 19,456 = 38,200 B` SRAM1 stack
headroom, exceeding the required 8 KiB by 30,008 B. Interrupt usage is not fully described
by GCC `.su` files, but it is covered comfortably by this remaining margin.

### Safety And Compatibility Scan

- No firmware/config reference to `0x1FFF7800`, `FLASH_OPTR`, boot option bits, or option-
  byte programming APIs was found.
- Software DFU remains `0x1FFF0000u`; USB VID remains `0x0483` in board and descriptor
  sources.
- Production `CompressedRecorder` contains no `wav_build_header`, `f_lseek`, mono
  `pdm_dma_read`, PCM file, or parallel WAV path.
- Legacy `.WAV` circular deletion compatibility remains case-insensitive alongside the
  configured `.OPUS` extension.
- Transfer-workspace exclusion, low-battery finalization, USB attach/detach recording
  behavior, and temporary-probe removal remain intact.

### Self-Review And Concerns

- Ogg boundary behavior was checked for full packet 50, short packet slot 50, zero-valid
  padding slot 50, exact EOS flags, CRCs, sequences, and monotonic granules.
- Lookahead planning covers exact-frame, partial-frame, already-sufficient partial padding,
  and empty-input cases. Empty input does not invent an Opus audio packet.
- PDM ordering was reviewed statically: DFSDM production is halted while IRQs are locked,
  TC flags are accounted by the existing double-snapshot logic, and frozen counters are
  stored before DMA disable/final clear. No hardware run was performed.
- FatFs failure handling is source-contracted rather than native fault-injected because the
  firmware currently has no fake FatFs harness. Close/unlink transitions and deterministic
  retry paths were reviewed directly.
- The upstream fixed-point Opus warnings and existing RWX load-segment warning remain; this
  round introduced no new compiler or linker failures.
- Real-time encode latency and physical stop-boundary capture still need a separately
  authorized hardware validation pass. This round intentionally performed none.

---

## Independent Review Fix Round 2/5

Date: 2026-08-11
Review input: `.superpowers/sdd/2026-08-11-opus-recording/task-6-rereview-1.md`

The remaining combined Ogg final-boundary Critical was reproduced and fixed. No hardware,
serial, microphone, USB-device, DFU, option-byte, or flashing operation was performed.

### RED Evidence

Added `test_short_then_lookahead_at_page_boundary_stay_together_on_eos` with the exact
production-reachable sequence: 48 full 320-valid packets, one 300-valid short packet, and
one zero-valid lookahead packet. Before the production change, focused Ogg testing reported
1 failed and 15 passed; the new test failed immediately after the short packet with
`Expected 3 Was 2`, proving the writer had not separated the 48 complete packets before
entering finalization.

### Implementation

`OggOpusWriter::add_packet()` now flushes buffered ordinary packets only on the transition
to the first final packet (`final_packet && !final_packets_started_`). It performs that
transition before appending the short packet. Once `final_packets_started_` is true, later
short or zero-valid packets are buffered with the first final packet until `finish()` emits
their shared EOS page, including when their combined packet count reaches the normal
50-packet rollover boundary. Ordinary full packet 50 still flushes immediately.

The combined regression validates:

- Sequence 2 is non-EOS and contains exactly 48 full packets.
- Sequence 2 granule is `312 + 48 * 320 * 3 = 46392`.
- Sequence 3 has EOS set and contains the short and lookahead packets as two laces.
- Sequence 3 granule is `312 + (48 * 320 + 300) * 3 = 47292`.
- Packet payload placement, page lengths, sequence numbers, flags, monotonic granules, and
  all page CRCs are correct.

The existing rollover sink-failure fixture used `valid_samples = 160` despite intending to
exercise ordinary full-frame packet 50. It was corrected to 320 so it continues to test the
production rollover path rather than finalization behavior.

### GREEN Evidence

- Focused Ogg writer: 16 passed.
- Real libopus encoder/finalization: 9 passed.
- Complete focused native matrix: 83 passed.
- Source contracts: 17 passed.
- ARM: `pio run -e dayvault -j 1` succeeded.
- `git diff --check` passed with only repository line-ending policy warnings.

### Resource, Symbol, Stack, And Safety Audit

- Flash: 219,284 B of 262,144 B, leaving 42,860 B.
- SRAM1 `.data + .bss + .noinit`: 71,880 B. Including the 1,536 B linker
  heap/stack reservation leaves 57,656 B before runtime stack.
- SRAM2 remains exactly 32,768 B at `0x10000000` for the shared transfer workspace.
- The conservative 19 KiB recording call-path allowance remains unchanged and leaves
  38,200 B SRAM1 headroom, exceeding the required 8 KiB by 30,008 B.
- Recorder, Ogg, encoder, `opus_encode_native`, `silk_Encode`, and
  `silk_encode_frame_FIX` symbols remain present. Decoder and temporary linker-probe
  symbols remain absent.
- No `0x1FFF7800`, option-byte programming, boot-option mutation, production
  `wav_build_header`, production `f_lseek`, or mono production PDM read was found.
- Software DFU remains `0x1FFF0000u`; USB VID remains `0x0483`; `.OPUS` production and
  legacy `.WAV` deletion compatibility remain intact.

### Self-Review And Concern

The mutation guarded by the new test is removing the first-final state check or allowing a
later final packet to preflush a page that already contains a short packet. Either change
breaks the asserted page count/flags before EOS. Physical decoder interoperability and
stop-boundary timing remain for a separately authorized hardware pass; none was attempted.
