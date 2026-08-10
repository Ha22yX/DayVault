# Direct Opus Recording Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace production PCM/WAV storage with standard 16 kHz mono Ogg Opus recording at a target 24 kbit/s, without retaining a PCM original.

**Architecture:** Paired PDM DMA data passes through the existing adaptive two-microphone DSP, then a fixed-memory assembler feeds 320-sample frames to the official fixed-point libopus 1.6.1 encoder. A small RFC 7845 muxer groups up to 50 packets per checksummed Ogg page and writes durable `.OPUS` files through FatFs. The existing byte-oriented USB export protocol is unchanged; only recording names and host metadata parsing expand to Opus.

**Tech Stack:** STM32L452/STM32duino, PlatformIO, DFSDM+DMA, Xiph libopus 1.6.1 fixed point, Ogg Opus RFC 7845, FatFs, Unity native tests, pytest.

## Global Constraints

- Production input is mono signed 16-bit PCM at exactly 16,000 samples/s.
- Opus uses 20 ms frames, constrained VBR at 24,000 bit/s, complexity 3, restricted SILK, DTX off, FEC off, and packets no larger than 160 bytes.
- Production creates `.OPUS` only; no parallel WAV or PCM file is created.
- Audio pages contain at most 50 packets and are synced at least once per second.
- No per-frame heap allocation is allowed. The encoder may allocate once from the existing 32 KiB SRAM2 transfer workspace through the project arena allocator.
- Legacy `.WAV` files remain downloadable and eligible for circular deletion.
- No option-byte writes or boot-bit changes. Preserve System Memory `0x1FFF0000` and USB VID `0x0483`.
- Do not flash hardware or access the microphones during this implementation pass.

---

### Task 1: Exact 16 kHz Capture Contract

**Files:**
- Modify: `firmware/src/PdmRate.h`
- Modify: `firmware/src/Config.h`
- Modify: `firmware/test/test_pdm_rate/test_pdm_rate.cpp`

**Interfaces:**
- Consumes: `pdm_clock_hz(uint32_t, uint32_t)` and `pdm_pcm_rate_hz(uint32_t, uint32_t, uint32_t)`.
- Produces: `PDM_PRODUCTION_CKOUT_DIVIDER == 50`, `PDM_PRODUCTION_OSR == 100`, and `AUDIO_SAMPLE_RATE == 16000`.

- [ ] **Step 1: Change the rate test first**

```cpp
void test_production_rate_is_exact_opus_wideband_rate(void) {
    TEST_ASSERT_EQUAL_UINT32(1600000u, PDM_PRODUCTION_CLOCK_HZ);
    TEST_ASSERT_EQUAL_UINT32(16000u, PDM_PRODUCTION_PCM_RATE_HZ);
    TEST_ASSERT_EQUAL_UINT32(50u, PDM_PRODUCTION_CKOUT_DIVIDER);
    TEST_ASSERT_EQUAL_UINT32(100u, PDM_PRODUCTION_OSR);
}
```

- [ ] **Step 2: Run the targeted test and observe the old 16,026 Hz expectation fail**

Run: `pio test -e native -f test_pdm_rate`

- [ ] **Step 3: Set divider 50 and OSR 100 in the production constants**

Keep the existing compile-time microphone clock range checks. Add:

```cpp
static_assert(PDM_PRODUCTION_PCM_RATE_HZ == 16000u,
              "Opus recording requires exact 16 kHz PCM");
```

- [ ] **Step 4: Run PDM rate and ring tests**

Run: `pio test -e native -f test_pdm_rate`

Run: `pio test -e native -f test_pdm_ring`

- [ ] **Step 5: Commit**

```powershell
git add firmware/src/PdmRate.h firmware/src/Config.h firmware/test/test_pdm_rate
git commit -m "fix(audio): produce exact 16 kHz PCM for Opus"
```

---

### Task 2: Pin the Official Fixed-Point libopus Encoder

**Files:**
- Create: `firmware/lib/opus/library.json`
- Create: `firmware/lib/opus/LICENSE`
- Create: `firmware/lib/opus/UPSTREAM.md`
- Create: `firmware/lib/opus/custom_support.h`
- Create: `firmware/lib/opus/include/**`
- Create: `firmware/lib/opus/src/**`
- Create: `firmware/lib/opus/silk/**`
- Create: `firmware/lib/opus/celt/**`
- Create: `firmware/src/OpusArena.h`
- Create: `firmware/src/OpusArena.cpp`
- Modify: `firmware/src/TransferBuffer.h`
- Modify: `firmware/src/TransferBuffer.cpp`
- Modify: `firmware/platformio.ini`
- Create: `firmware/test/test_opus_arena/test_opus_arena.cpp`

**Interfaces:**
- Produces: `uint8_t* transfer_workspace()`, `size_t transfer_workspace_size()` returning one contiguous 32 KiB region.
- Produces: `opus_arena_begin(void*, size_t)`, `opus_arena_used()`, `dayvault_opus_alloc`, `dayvault_opus_realloc`, and `dayvault_opus_free`.
- Produces: `<opus.h>` from the pinned official source.

- [ ] **Step 1: Add failing arena tests**

```cpp
void test_arena_aligns_allocations_and_rejects_overflow(void) {
    alignas(16) uint8_t memory[64];
    opus_arena_begin(memory, sizeof(memory));
    void* a = dayvault_opus_alloc(3);
    void* b = dayvault_opus_alloc(9);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_UINT32(0u, (uintptr_t)a & 7u);
    TEST_ASSERT_EQUAL_UINT32(0u, (uintptr_t)b & 7u);
    TEST_ASSERT_NULL(dayvault_opus_alloc(80));
}
```

- [ ] **Step 2: Run the arena test and verify the missing interface fails**

Run: `pio test -e native -f test_opus_arena`

- [ ] **Step 3: Vendor and record upstream provenance**

Extract only the official `include`, `src`, `silk`, and `celt` trees required by the normal encoder from `opus-1.6.1.tar.gz`. Copy its license unchanged. `UPSTREAM.md` must record:

```text
Version: 1.6.1
Source: https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz
SHA-256: 6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1
Local configuration: FIXED_POINT, DISABLE_FLOAT_API, DISABLE_DEBUG_FLOAT,
VAR_ARRAYS, CUSTOM_SUPPORT; DNN/DRED/OSCE and CPU runtime dispatch disabled.
```

- [ ] **Step 4: Add PlatformIO library metadata**

The library build flags must include:

```json
"flags": [
  "-DOPUS_BUILD",
  "-DFIXED_POINT=1",
  "-DDISABLE_FLOAT_API",
  "-DDISABLE_DEBUG_FLOAT",
  "-DVAR_ARRAYS",
  "-DCUSTOM_SUPPORT",
  "-DPACKAGE_VERSION=\\\"1.6.1\\\"",
  "-Iinclude", "-I.", "-Icelt", "-Isilk", "-Isilk/fixed"
]
```

Exclude tests, demos, x86, NEON, DNN, float-only SILK, and custom-mode files with `srcFilter`.

- [ ] **Step 5: Implement the fixed arena and contiguous workspace API**

The allocator rounds each request to 8-byte alignment, never wraps, and never calls `malloc`.
`realloc` may only grow the most recent allocation in place; otherwise it returns null. Free is a no-op because the whole arena is reset between recording and export.

- [ ] **Step 6: Run arena tests and a firmware compile smoke test**

Run: `pio test -e native -f test_opus_arena`

Run: `pio run -e dayvault`

- [ ] **Step 7: Commit**

```powershell
git add firmware/lib/opus firmware/src/OpusArena.* firmware/src/TransferBuffer.* firmware/platformio.ini firmware/test/test_opus_arena
git commit -m "build(audio): vendor fixed-point libopus 1.6.1"
```

---

### Task 3: RFC 7845 Ogg Opus Muxer

**Files:**
- Create: `firmware/src/OggOpusWriter.h`
- Create: `firmware/src/OggOpusWriter.cpp`
- Create: `firmware/test/test_ogg_opus/test_ogg_opus.cpp`
- Modify: `firmware/platformio.ini`

**Interfaces:**
- Consumes: `OggOpusSink { bool (*write)(void*, const uint8_t*, size_t); void* context; }`.
- Produces: `bool OggOpusWriter::begin(OggOpusSink, uint8_t*, size_t, uint32_t serial, uint16_t pre_skip_48k)`.
- Produces: `bool OggOpusWriter::add_packet(const uint8_t*, uint16_t, uint16_t valid_input_samples)`.
- Produces: `bool OggOpusWriter::finish()` and `OggOpusStats stats() const`.

- [ ] **Step 1: Write failing golden-layout tests**

Cover these exact assertions:

```cpp
TEST_ASSERT_EQUAL_MEMORY("OggS", bytes, 4);
TEST_ASSERT_EQUAL_UINT8(0x02, bytes[5]);              // BOS
TEST_ASSERT_EQUAL_MEMORY("OpusHead", bytes + 28, 8);
TEST_ASSERT_EQUAL_UINT8(1, bytes[37]);                // mono
TEST_ASSERT_EQUAL_UINT32(16000u, read_le32(bytes + 40));
TEST_ASSERT_TRUE(all_page_crc_values_are_valid(bytes, length));
```

Add separate tests for OpusTags, sequence numbers, 50-packet page rollover, lacing values, exact `pre_skip + valid_samples * 3` EOS granule position, and a rejected packet larger than 160 bytes.

- [ ] **Step 2: Run and observe missing muxer failures**

Run: `pio test -e native -f test_ogg_opus`

- [ ] **Step 3: Implement Ogg CRC and page construction**

Use the non-reflected Ogg polynomial `0x04C11DB7`. Build each page in the caller-provided buffer: 27-byte header, segment table, then packet data. Zero bytes 22..25 while calculating CRC, then write CRC little-endian.

- [ ] **Step 4: Implement headers, deferred full-page flush, and final trimming**

`begin` emits one BOS `OpusHead` page and one `OpusTags` page. Hold a full 50-packet audio page until the next packet or `finish`, allowing an exact multiple of one second to receive EOS without an empty trailing page.

- [ ] **Step 5: Run muxer tests**

Run: `pio test -e native -f test_ogg_opus`

- [ ] **Step 6: Commit**

```powershell
git add firmware/src/OggOpusWriter.* firmware/test/test_ogg_opus firmware/platformio.ini
git commit -m "feat(audio): add fixed-memory Ogg Opus muxer"
```

---

### Task 4: Real Opus Encoder Adapter

**Files:**
- Create: `firmware/src/DayVaultOpusEncoder.h`
- Create: `firmware/src/DayVaultOpusEncoder.cpp`
- Create: `firmware/test/test_opus_encoder/test_opus_encoder.cpp`
- Modify: `firmware/platformio.ini`

**Interfaces:**
- Produces: constants `kOpusSampleRate=16000`, `kOpusFrameSamples=320`, `kOpusBitrate=24000`, and `kOpusMaxPacketBytes=160`.
- Produces: `bool DayVaultOpusEncoder::begin(void* workspace, size_t bytes)`.
- Produces: `int DayVaultOpusEncoder::encode(const int16_t pcm[320], uint8_t packet[160])`.
- Produces: `uint16_t pre_skip_48k()`, `size_t workspace_used()`, `DayVaultOpusStats stats()`, and `end()`.

- [ ] **Step 1: Add a real-codec test**

```cpp
void test_encoder_emits_bounded_standard_opus_packets(void) {
    alignas(16) uint8_t workspace[32 * 1024];
    DayVaultOpusEncoder encoder;
    TEST_ASSERT_TRUE(encoder.begin(workspace, sizeof(workspace)));
    int16_t pcm[320] = {0};
    uint8_t packet[160];
    int length = encoder.encode(pcm, packet);
    TEST_ASSERT_GREATER_THAN(0, length);
    TEST_ASSERT_LESS_OR_EQUAL(160, length);
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(workspace), encoder.workspace_used());
    TEST_ASSERT_GREATER_THAN(0, encoder.pre_skip_48k());
}
```

- [ ] **Step 2: Run the test and verify it fails before the adapter exists**

Run: `pio test -e native -f test_opus_encoder`

- [ ] **Step 3: Initialize and configure the pinned encoder**

Call `opus_encoder_create(16000, 1, OPUS_APPLICATION_RESTRICTED_SILK, &error)` after starting the arena. Apply and check every CTL result:

```cpp
OPUS_SET_BITRATE(24000)
OPUS_SET_VBR(1)
OPUS_SET_VBR_CONSTRAINT(1)
OPUS_SET_COMPLEXITY(3)
OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE)
OPUS_SET_DTX(0)
OPUS_SET_INBAND_FEC(0)
OPUS_SET_PACKET_LOSS_PERC(0)
OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND)
```

Read `OPUS_GET_LOOKAHEAD`; multiply 16 kHz samples by three for the 48 kHz `OpusHead` pre-skip field. Fail if the arena overflowed or the state consumed more than 32 KiB.

- [ ] **Step 4: Test reset, deterministic silence, and error statistics**

Add tests that encode at least 100 frames, verify all packets are in bounds, reset the encoder, and verify no per-frame arena growth.

- [ ] **Step 5: Run real encoder and firmware builds**

Run: `pio test -e native -f test_opus_encoder`

Run: `pio run -e dayvault`

Inspect: `arm-none-eabi-size -A .pio/build/dayvault/firmware.elf`

Reject the task if Flash exceeds 262,144 bytes or `.ram2` exceeds 32,768 bytes.

- [ ] **Step 6: Commit**

```powershell
git add firmware/src/DayVaultOpusEncoder.* firmware/test/test_opus_encoder firmware/platformio.ini
git commit -m "feat(audio): configure 24 kbit Opus encoder"
```

---

### Task 5: Fixed-Memory Dual-Mic Recording Pipeline

**Files:**
- Create: `firmware/src/AudioPipeline.h`
- Create: `firmware/src/AudioPipeline.cpp`
- Create: `firmware/test/test_audio_pipeline/test_audio_pipeline.cpp`
- Modify: `firmware/platformio.ini`

**Interfaces:**
- Consumes: `AudioFusion`, `NoiseReduction`, and `SpeechLeveler`.
- Produces: callback `typedef bool (*AudioFrameSink)(void*, const int16_t*, uint16_t valid_samples)`.
- Produces: `reset(uint32_t sample_rate, AudioFrameSink, void*)`, `push(a,b,count)`, `finish()`, and `AudioPipelineStats stats()`.

- [ ] **Step 1: Add failing chunking and flush tests**

Feed the same 1,003 paired synthetic samples using chunk sizes 1, 7, 128, and 251. Assert every run emits the same number of 320-sample frames, the same `valid_samples` sequence (`320, 320, 320, 43` after latency accounting), and exactly 1,003 valid output samples in total.

- [ ] **Step 2: Run and observe the missing pipeline failure**

Run: `pio test -e native -f test_audio_pipeline`

- [ ] **Step 3: Implement paired 128-sample assembly and DSP order**

Use static arrays only. Process `fusion -> noise reduction -> speech leveler`. Suppress the first 128-sample noise-reduction latency block. Accumulate post-leveler samples into 320-sample codec frames.

- [ ] **Step 4: Implement exact finish semantics**

Pad the final paired DSP block, flush the NR overlap with one zero block, and emit one padded codec frame with `valid_samples` equal only to captured audio. Empty input emits no frame. Propagate a false frame-sink return as a latched pipeline failure.

- [ ] **Step 5: Run DSP and pipeline regressions**

Run: `pio test -e native -f test_audio_pipeline`

Run: `pio test -e native -f test_audio_fusion`

Run: `pio test -e native -f test_noise_reduction`

Run: `pio test -e native -f test_speech_leveler`

- [ ] **Step 6: Commit**

```powershell
git add firmware/src/AudioPipeline.* firmware/test/test_audio_pipeline firmware/platformio.ini
git commit -m "feat(audio): assemble dual-mic DSP into Opus frames"
```

---

### Task 6: Replace Production WAV Recording with Opus

**Files:**
- Create: `firmware/src/CompressedRecorder.h`
- Create: `firmware/src/CompressedRecorder.cpp`
- Modify: `firmware/src/main.cpp`
- Modify: `firmware/src/Config.h`
- Modify: `firmware/src/Fs.cpp`
- Modify: `firmware/src/Fs.h`
- Modify: `tools/dayvault_sync/tests/test_firmware_memory.py`

**Interfaces:**
- Consumes: `pdm_dma_read_dual`, `AudioPipeline`, `DayVaultOpusEncoder`, `OggOpusWriter`, FatFs `FIL`, and the SRAM2 transfer workspace.
- Produces: start/poll/checkpoint/stop methods returning explicit error codes and `CompressedRecorderStats`.
- Preserves: existing `rec_start`, `rec_stop`, USB auto-start/stop behavior, low-battery finalization, circular deletion, and USB downloads.

- [ ] **Step 1: Add source-contract tests before changing production**

Extend `test_firmware_memory.py` to assert:

```python
assert 'pdm_dma_read_dual' in recorder_text
assert 'REC_EXT_STR "OPUS"' in config_text
assert 'wav_build_header' not in production_recorder_text
assert 'OPUSSTAT' in main_text
assert '0x1FFF0000u' in main_text
assert '0x1FFF7800' not in all_firmware_text
```

- [ ] **Step 2: Run pytest and confirm the new Opus contract fails**

Run: `python -m pytest tools/dayvault_sync/tests/test_firmware_memory.py -q`

- [ ] **Step 3: Implement the FatFs sink and compressed recorder**

At start: mount, make space before starting PDM, open `.OPUS`, initialize encoder in SRAM2, write/sync Ogg headers, reset DSP, start PDM, and discard 32 paired startup samples.

At poll: read up to 128 paired samples, push them through `AudioPipeline`, encode every emitted 320-sample frame, and add each packet to the Ogg writer. The Ogg sink checks both `FRESULT` and exact byte count.

At checkpoint: flush any completed page and call `f_sync`; do not seek or rewrite a header.

At stop: stop PDM, drain paired samples, finish DSP, finish Ogg with EOS, sync, close, release the Opus arena, and rename using valid input samples divided by 16,000.

- [ ] **Step 4: Update file discovery and circular deletion**

Use `.OPUS` for new sequence scans. Circular deletion accepts a recording when it starts with `REC` and ends in `.OPUS` or legacy `.WAV`. Keep the active basename skip and existing chronological ordering.

- [ ] **Step 5: Add non-recording diagnostics**

`OPUSSTAT` prints one line containing bitrate, sample rate, frame count, page count, valid samples, bytes, encoder errors, max encode microseconds, Opus workspace used, paired DMA overruns, fusion weights/faults, NR readiness/gain, and leveler gain/limiter count. It must not start PDM or open a file.

- [ ] **Step 6: Make export and shared-workspace ownership explicit**

Before `GET2`, `DL2`, `BULK2`, and benchmark commands use the transfer workspace, stop/finalize an active recording. A recording cannot start while a bulk export is active.

- [ ] **Step 7: Run source contracts and firmware build**

Run: `python -m pytest tools/dayvault_sync/tests/test_firmware_memory.py -q`

Run: `pio run -e dayvault`

Inspect Flash, SRAM1, SRAM2, and generated `.su` stack-usage files for `opus_encode` and the restricted-SILK path. Keep at least 8 KiB conservative SRAM1 stack headroom after measured static and worst reported codec stack use.

- [ ] **Step 8: Commit**

```powershell
git add firmware/src/CompressedRecorder.* firmware/src/main.cpp firmware/src/Config.h firmware/src/Fs.* tools/dayvault_sync/tests/test_firmware_memory.py
git commit -m "feat(rec): save production audio directly as Opus"
```

---

### Task 7: Windows Sync Compatibility

**Files:**
- Modify: `tools/dayvault_sync/dayvault/proto.py`
- Modify: `tools/dayvault_sync/tests/test_proto.py`
- Modify: `tools/dayvault_sync/dayvault/app.py` only if display code assumes WAV.
- Modify: `tools/dayvault_sync/README.md`

**Interfaces:**
- Preserves: `parse_rec_name(name) -> dict | None` and all byte-oriented download methods.
- Produces: parsed metadata key `format` equal to `"opus"` or `"wav"`.

- [ ] **Step 1: Add failing `.OPUS` parser cases**

```python
def test_parse_opus_timestamp_duration():
    item = parse_rec_name("REC-20260811-0905_1h02m03s.OPUS")
    assert item["kind"] == "timestamp"
    assert item["duration_secs"] == 3723
    assert item["format"] == "opus"

def test_parse_legacy_wav_remains_supported():
    assert parse_rec_name("REC062.WAV")["format"] == "wav"
```

- [ ] **Step 2: Run and observe parser failures**

Run: `python -m pytest tools/dayvault_sync/tests/test_proto.py -q`

- [ ] **Step 3: Generalize extension matching without changing transfer code**

Match `OPUS|WAV` case-insensitively in timestamp and sequence expressions. Keep duration and collision parsing unchanged. Add `format` based on the captured extension.

- [ ] **Step 4: Run the complete sync test suite**

Run: `python -m pytest tools/dayvault_sync/tests -q`

- [ ] **Step 5: Commit**

```powershell
git add tools/dayvault_sync
git commit -m "feat(sync): recognize Opus recordings"
```

---

### Task 8: Documentation and Final Verification

**Files:**
- Create: `Docs/09-Opus-Recording.md`
- Modify: `Docs/Serial-Command-Reference.md`
- Modify: `README.md`
- Modify: `README.zh-CN.md`

**Interfaces:**
- Documents the production format, storage estimate, power-loss behavior, diagnostics, and decoder compatibility.

- [ ] **Step 1: Document user-visible behavior**

State that new recordings are mono 16 kHz Ogg Opus at target 24 kbit/s, approximately 265 MB/day, and that no WAV original is stored. Document `OPUSSTAT`, clean stop, abrupt-loss behavior, and legacy WAV export support.

- [ ] **Step 2: Run targeted native suites**

```powershell
pio test -e native -f test_pdm_rate
pio test -e native -f test_pdm_ring
pio test -e native -f test_opus_arena
pio test -e native -f test_ogg_opus
pio test -e native -f test_opus_encoder
pio test -e native -f test_audio_pipeline
pio test -e native -f test_audio_fusion
pio test -e native -f test_noise_reduction
pio test -e native -f test_speech_leveler
pio test -e native -f test_crc32
pio test -e native -f test_export_protocol
pio test -e native -f test_sd_protocol
pio test -e native -f test_winusb_descriptors
```

- [ ] **Step 3: Run host and firmware regressions**

```powershell
python -m pytest tools/dayvault_sync/tests -q
pio run -e dayvault
git diff --check
```

- [ ] **Step 4: Inspect binary safety and resource limits**

Verify the linker map is within 262,144-byte Flash, 128 KiB SRAM1, and 32 KiB SRAM2. Search the firmware tree for option-byte addresses/writes, confirm System Memory remains `0x1FFF0000`, and confirm the USB descriptors still use VID `0x0483`.

- [ ] **Step 5: Independently validate an Ogg fixture**

Write a deterministic host-test `.opus` fixture to a temporary directory, then run `ffprobe` or `opusinfo` when available. Require one mono Opus stream, 16,000 Hz decoder output capability, valid Ogg CRCs, and duration matching valid input samples within one sample at the 48 kHz granule scale. Do not add the generated fixture to git.

- [ ] **Step 6: Commit documentation and verification metadata**

```powershell
git add Docs README.md README.zh-CN.md
git commit -m "docs(audio): document direct Opus recording"
```

