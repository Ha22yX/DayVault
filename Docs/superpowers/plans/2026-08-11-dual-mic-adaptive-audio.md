# Dual-Microphone Adaptive Audio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the noisy single-microphone recording path with a validated two-channel PDM acquisition path that adaptively preserves both the wearer and the forward conversation partner, then writes one clearer mono WAV stream.

**Architecture:** Two synchronized DFSDM filters feed paired DMA buffers. A fixed-memory DSP pipeline conditions and scores both microphone streams, smoothly fuses the cleaner speech channel, applies conservative post-fusion noise suppression and speech-gated level control, and returns mono PCM to the existing recorder. A bounded stereo diagnostic command and Windows Reminder-driven host tool produce controlled raw captures before constants and channel identity are finalized.

**Tech Stack:** STM32L452RCT6, STM32duino HAL/DFSDM/DMA, C++11 fixed-memory DSP, PlatformIO Unity native tests, Python 3/pytest/pyserial, FatFs stereo diagnostic WAV, Windows Reminder CLI.

## Global Constraints

- Never modify STM32 FLASH option bytes, `nBOOT1`, `nBOOT0`, or `nSWBOOT0`.
- Never write to `0x1FFF7800`; preserve the software DFU jump to `0x1FFF0000`.
- Preserve USB VID `0x0483`, existing USB protocols, RTC filenames, circular deletion, and low-battery behavior.
- Before any firmware flash, confirm ROM DFU is visible with `dfu-util -l`; after flashing, confirm normal USB enumeration and that ROM DFU remains reachable.
- Production files remain mono 16-bit PCM WAV; stereo is bounded diagnostic output only.
- No heap allocation, blocking serial output, or filesystem access in the per-sample DSP path.
- Do not finalize channel orientation, gain correction, delay, or noise thresholds from the uncontrolled `DUAL` snapshot.
- Keep SPH0655 PDM clock inside its documented 1.1-4.8 MHz normal-mode range.
- Use one exact verified PCM sample rate for DFSDM configuration, WAV headers, filter coefficients, storage calculations, and diagnostics.
- New continuous DSP must cause zero DMA/ring overruns and must have measured CPU and current-consumption headroom.
- The current full native suite has unrelated stale-test errors (`test_rec_mgr`, old C ring-buffer/WAV/USB tests). Run the new targeted suites plus the currently passing C++ suites; do not expand scope by repairing unrelated stale tests.

---

### Task 1: Paired Raw PDM Reader

**Files:**
- Modify: `firmware/src/PdmCapture.h`
- Modify: `firmware/src/PdmCapture.cpp`
- Create: `firmware/src/PdmRate.h`
- Modify: `firmware/platformio.ini`
- Create: `firmware/test/test_pdm_rate/test_pdm_rate.cpp`

**Interfaces:**
- Produces: `int pdm_dma_read_dual(int16_t* channel_a, int16_t* channel_b, int max_samples)` returning equal-length, same-index PCM blocks.
- Produces: `PdmCaptureStats pdm_capture_stats()` with per-channel produced/consumed counts, overrun counts, and measured output count.
- Produces: pure clock helpers `pdm_clock_hz(sysclk, divider)` and `pdm_pcm_rate_hz(sysclk, divider, osr)` for native tests and WAV configuration.
- Preserves: `pdm_dma_read()` as a temporary compatibility wrapper until Task 6 moves production recording to paired reads.

- [ ] **Step 1: Write failing clock and paired-accounting tests**

Create `test_pdm_rate.cpp` with these assertions:

```cpp
#include <unity.h>
#include "PdmRate.h"

void setUp() {}
void tearDown() {}

void test_divider_52_keeps_mic_in_normal_mode() {
    TEST_ASSERT_EQUAL_UINT32(1538461u, pdm_clock_hz(80000000u, 52u));
    TEST_ASSERT_TRUE(pdm_clock_is_sph0655_normal(80000000u, 52u));
}

void test_osr_96_produces_near_16khz() {
    TEST_ASSERT_EQUAL_UINT32(16026u, pdm_pcm_rate_hz(80000000u, 52u, 96u));
}

void test_invalid_divider_is_rejected() {
    TEST_ASSERT_FALSE(pdm_clock_is_sph0655_normal(80000000u, 13u));
    TEST_ASSERT_EQUAL_UINT32(0u, pdm_clock_hz(80000000u, 0u));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_divider_52_keeps_mic_in_normal_mode);
    RUN_TEST(test_osr_96_produces_near_16khz);
    RUN_TEST(test_invalid_divider_is_rejected);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the test and verify RED**

Run: `pio test -e native -f test_pdm_rate`

Expected: FAIL because `PdmRate.h` does not exist.

- [ ] **Step 3: Add the pure PDM clock contract**

Create `PdmRate.h` as a dependency-free header:

```cpp
#pragma once
#include <stdint.h>

static inline uint32_t pdm_clock_hz(uint32_t source_hz, uint32_t divider) {
    return divider ? (source_hz / divider) : 0u;
}

static inline uint32_t pdm_pcm_rate_hz(uint32_t source_hz, uint32_t divider,
                                       uint32_t filter_osr) {
    const uint32_t clock = pdm_clock_hz(source_hz, divider);
    return filter_osr ? ((clock + filter_osr / 2u) / filter_osr) : 0u;
}

static inline bool pdm_clock_is_sph0655_normal(uint32_t source_hz,
                                                uint32_t divider) {
    const uint32_t clock = pdm_clock_hz(source_hz, divider);
    return clock >= 1100000u && clock <= 4800000u;
}
```

Add `PdmCaptureStats` and the dual-read declaration to `PdmCapture.h`. In
`PdmCapture.cpp`, track each DMA channel's `CNDTR`, wrap count, read position, and
consumed total independently. `pdm_dma_read_dual()` calculates the available sample count
for each channel and returns only `min(available_a, available_b, max_samples)`. If one DMA
has lapped its reader, move both read positions to the same safe distance behind their
respective write heads and increment a paired-overrun counter.

The dual reader returns scaled raw DFSDM PCM only. Remove HPF, shelf, peaking EQ, AGC,
and limiter operations from this function; those belong to Tasks 4 and 5. Use 32-bit
intermediate multiplication for the temporary raw gain and saturate once when producing
each diagnostic `int16_t` sample.

- [ ] **Step 4: Preserve mono compatibility explicitly**

Implement `pdm_dma_read()` as:

```cpp
int pdm_dma_read(int16_t* mono, int max_samples) {
    static int16_t discard_b[128];
    int total = 0;
    while (total < max_samples) {
        int take = max_samples - total;
        if (take > 128) take = 128;
        int n = pdm_dma_read_dual(mono + total, discard_b, take);
        if (n <= 0) break;
        total += n;
    }
    return total;
}
```

This wrapper is removed or made private after Task 6.

- [ ] **Step 5: Run focused tests and firmware build**

Run:

```powershell
pio test -e native -f test_pdm_rate
pio run -e dayvault
```

Expected: the new suite passes and firmware links without RAM overflow.

- [ ] **Step 6: Commit**

```powershell
git add firmware/src/PdmCapture.h firmware/src/PdmCapture.cpp firmware/src/PdmRate.h firmware/platformio.ini firmware/test/test_pdm_rate
git commit -m "feat(audio): expose paired raw PDM capture"
```

---

### Task 2: Bounded Stereo Calibration Capture and Reminder Runner

**Files:**
- Create: `firmware/src/AudioCalibration.h`
- Create: `firmware/src/AudioCalibration.cpp`
- Modify: `firmware/src/main.cpp`
- Create: `tools/audio_calibration.py`
- Create: `tools/dayvault_sync/tests/test_audio_calibration.py`
- Modify: `Docs/Serial-Command-Reference.md`

**Interfaces:**
- Produces firmware command: `CALRAW <SCENE> <SECONDS>` where `SCENE` is 1-12 uppercase letters, digits, `_`, or `-`, and `SECONDS` is 1-15.
- Produces responses: `CALRAW START file=<name> seconds=<n> rate=<hz>` and `CALRAW DONE file=<name> frames=<n> bytes=<n> overrun=<n>`.
- Produces host functions: `build_scenes(duration)`, `parse_calraw_start(line)`, `parse_calraw_done(line)`, and `run_calibration(...)`.
- Consumes: Task 1 `pdm_dma_read_dual()` and existing `DeviceConnection.download_get2()`.

- [ ] **Step 1: Write failing host protocol tests**

Test that the host runner:

```python
def test_build_scenes_covers_both_directions_and_noise():
    assert [s.tag for s in build_scenes(10)] == [
        "QUIET", "WEARER", "FRONT", "ALTERNATE", "CLOTHING"
    ]

def test_parse_calraw_done_extracts_verified_file():
    result = parse_calraw_done(
        "CALRAW DONE file=CAL-QUIET.WAV frames=160260 bytes=641040 overrun=0"
    )
    assert result.file == "CAL-QUIET.WAV"
    assert result.frames == 160260
    assert result.overrun == 0

def test_scene_tag_rejects_command_injection():
    with pytest.raises(ValueError):
        validate_scene_tag("QUIET;DFU")
```

- [ ] **Step 2: Run the tests and verify RED**

Run: `python -m pytest tools/dayvault_sync/tests/test_audio_calibration.py -q`

Expected: FAIL because `tools.audio_calibration` does not exist.

- [ ] **Step 3: Implement the host runner without showing a real notification in tests**

`run_calibration()` performs this sequence for every scene:

1. call Windows Reminder with a preparation message and `--time 5`;
2. wait for explicit console confirmation before the first scene only;
3. show a three-second countdown notification;
4. send `CALRAW <TAG> <SECONDS>`;
5. wait for the matching `CALRAW DONE` line;
6. download the named stereo WAV with `download_get2()`;
7. write start/end host timestamps and device response fields to `calibration.json`.

Use argument-list subprocess execution, never `shell=True`:

```python
subprocess.run([
    str(reminder_cmd), title, body,
    "--icon", icon, "--time", str(seconds),
], check=True)
```

Default reminder path:
`C:\Users\Administrator\Desktop\Windows Reminder\reminder.cmd`. Add
`--reminder-path`, `--port`, `--output`, `--duration`, and `--no-notifications` options.

- [ ] **Step 4: Implement bounded stereo WAV capture**

`AudioCalibration.cpp` validates the request before opening a file. It writes a normal
44-byte PCM WAV header configured for two channels, reads paired blocks of at most 128
frames, interleaves `[A0, B0, A1, B1, ...]`, and writes through a 1024-byte staging buffer.
It kicks the watchdog inside the loop, aborts on USB removal, write failure, or overrun, and
always patches/closes the file. It refuses to run while normal recording is active.

Use a bounded filename such as `0:/CAL-<SCENE>-<HHMMSS>.WAV`; never pass the scene text
directly to FatFs until validation has succeeded.

- [ ] **Step 5: Run tests and build**

Run:

```powershell
python -m pytest tools/dayvault_sync/tests/test_audio_calibration.py -q
pio run -e dayvault
```

Expected: tests pass and the firmware build reports no section overflow.

- [ ] **Step 6: Flash and collect the pre-change baseline safely**

Run `dfu-util -l` first and save the listing. Enter DFU using the existing `DFU` serial
command or the hardware BOOT/RESET sequence. Flash only the application address already
used by this repository; do not issue any option-byte command. After reset, verify
VID/PID `0483:5740`, `INFO`, and a second ROM-DFU entry.

Run:

```powershell
python tools/audio_calibration.py --port COM9 --output artifacts/audio-baseline --duration 10
```

Expected: five stereo files, a manifest, zero capture overruns, and Reminder prompts for
quiet, wearer, front, alternating, and clothing scenes.

- [ ] **Step 7: Commit**

```powershell
git add firmware/src/AudioCalibration.h firmware/src/AudioCalibration.cpp firmware/src/main.cpp tools/audio_calibration.py tools/dayvault_sync/tests/test_audio_calibration.py Docs/Serial-Command-Reference.md
git commit -m "feat(audio): add controlled dual-raw calibration capture"
```

---

### Task 3: Correct PDM Clock, Sample Rate, and Filter Synchronization

**Files:**
- Modify: `firmware/src/Config.h`
- Modify: `firmware/src/PdmCapture.cpp`
- Modify: `firmware/src/PdmCapture.h`
- Modify: `firmware/src/main.cpp`
- Modify: `firmware/test/test_pdm_rate/test_pdm_rate.cpp`
- Modify: `Docs/Serial-Command-Reference.md`

**Interfaces:**
- Produces: `uint32_t pdm_output_rate_hz()` and command `PDMRATE`.
- Changes: Channel/Filter 0 is the software-trigger master; Filter 1 uses `DFSDM_FILTER_SYNC_TRIGGER` and is armed before Filter 0 starts.
- Changes: `PDM_CKOUT_DIVIDER=52`, `PDM_OSR=96`, fast mode enabled, nominal PCM rate `16026` Hz pending hardware count verification.

- [ ] **Step 1: Extend the failing rate tests**

Add assertions that production constants yield 1.538 MHz PDM and 16026 Hz PCM, and that
the old divider 13 yields an out-of-range 6.153 MHz clock. Add a compile-time assertion in
`Config.h` that the configured mic clock is within 1.1-4.8 MHz.

- [ ] **Step 2: Run and verify RED**

Run: `pio test -e native -f test_pdm_rate`

Expected: FAIL because production constants are still divider 13.

- [ ] **Step 3: Apply synchronized DFSDM configuration**

Set Filter 0 regular trigger to software and Filter 1 regular trigger to synchronous. In
`pdm_start()`, enable/arm the synchronous Filter 1 before starting Filter 0. Set
`FastMode=ENABLE` for both filters, keep Sinc3/OSR96/IOSR1, clear both overrun flags, and
discard at least the calculated Sinc3 startup latency before exposing samples.

Add `PDMRATE` to count paired samples over exactly 2000 ms without writing SD. Report:

```text
PDMRATE expected=16026 measured=<count/2> a=<countA> b=<countB> skew=<abs(A-B)> overrun=<n>
```

- [ ] **Step 4: Stop estimating WAV rate from file-close elapsed time**

Initialize `rec_cfg.sample_rate` from `pdm_output_rate_hz()` and remove the duration-based
sample-rate rewrite in `rec_stop()`. File close, `f_sync()`, and SD stalls are not audio-clock
measurements.

- [ ] **Step 5: Verify on host and hardware**

Run:

```powershell
pio test -e native -f test_pdm_rate
pio run -e dayvault
```

After safe flash, run `PDMRATE` three times. Acceptance: each channel is within 0.5% of
the configured rate, A/B skew is at most one sample, and overruns are zero. If hardware
measurement does not match 16026 Hz, keep the clock in the mic's normal range and update
the single source-of-truth rate constant, tests, WAV header, and later DSP coefficients to
the measured stable value before committing.

- [ ] **Step 6: Commit**

```powershell
git add firmware/src/Config.h firmware/src/PdmCapture.cpp firmware/src/PdmCapture.h firmware/src/main.cpp firmware/test/test_pdm_rate Docs/Serial-Command-Reference.md
git commit -m "fix(audio): synchronize DFSDM at a valid speech clock"
```

---

### Task 4: Adaptive Two-Sided Fusion Core

**Files:**
- Create: `firmware/src/AudioFusion.h`
- Create: `firmware/src/AudioFusion.cpp`
- Modify: `firmware/platformio.ini`
- Create: `firmware/test/test_audio_fusion/test_audio_fusion.cpp`

**Interfaces:**
- Produces: `AudioFusion`, a fixed-memory class with `reset(sample_rate)`, `process(channel_a, channel_b, mono, count)`, and `stats()`.
- Produces: `AudioFusionStats { rms_a, rms_b, noise_a, noise_b, weight_a_q15, weight_b_q15, lag, correlation_q15, fault_flags, speech_present }`.
- Consumes blocks of 128 paired samples; returns 128 mono samples.

- [ ] **Step 1: Write failing deterministic fusion tests**

Use generated integer sine waves and deterministic xorshift noise. Cover these behaviors:

```cpp
void test_equal_clean_channels_stay_balanced();
void test_clean_a_beats_noisy_b_after_smoothing();
void test_clean_b_beats_noisy_a_after_smoothing();
void test_clipped_channel_falls_back_to_healthy_channel();
void test_weight_change_is_bounded_between_blocks();
void test_correlated_delayed_channels_report_the_best_lag();
void test_output_never_wraps_or_hard_clips();
```

For clean/noisy tests, process at least 40 blocks so noise and smoothing states settle.
Assert the cleaner channel receives at least 70% weight, the minority healthy channel keeps
at least 10%, and a failed channel receives 0%.

- [ ] **Step 2: Run and verify RED**

Run: `pio test -e native -f test_audio_fusion`

Expected: FAIL because `AudioFusion.h` does not exist.

- [ ] **Step 3: Implement per-channel conditioning**

For each channel, keep separate fixed-point state:

- first-order DC/100 Hz high-pass using a Q15 coefficient calculated from the verified rate;
- slow gain matching constrained to 0.5x-2.0x;
- rolling signal and noise energy with 64-bit accumulators;
- clipping, crest-factor/impulse, low-frequency rub, and high-frequency difference penalties;
- failed-channel detection for stuck, silent, or repeatedly saturated input.

Do not include the old +5/+10 dB peaking filters.

- [ ] **Step 4: Implement lag, quality score, and bounded weights**

Use a 256-sample history and search integer lags `[-8, +8]` with normalized correlation.
Only use the lag for coherent blending when correlation confidence exceeds 0.75. Otherwise
compute weights from each channel's speech SNR and penalties, clamp healthy weights to
`0.10..0.90`, and smooth toward the target. Use faster movement toward a newly clean
channel and slower release to prevent syllable pumping.

Mix in 32-bit arithmetic:

```cpp
int32_t mixed = ((int32_t)a * weight_a_q15 +
                 (int32_t)b_aligned * weight_b_q15 + 16384) >> 15;
mono[i] = saturate_i16(mixed);
```

- [ ] **Step 5: Run focused tests and build**

Run:

```powershell
pio test -e native -f test_audio_fusion
pio run -e dayvault
```

Expected: all deterministic fusion tests pass with no floating nondeterminism or allocation.

- [ ] **Step 6: Commit**

```powershell
git add firmware/src/AudioFusion.h firmware/src/AudioFusion.cpp firmware/platformio.ini firmware/test/test_audio_fusion
git commit -m "feat(audio): add adaptive two-sided microphone fusion"
```

---

### Task 5: Conservative Noise Suppression and Speech Leveling

**Files:**
- Replace: `firmware/src/NoiseReduction.h`
- Replace: `firmware/src/NoiseReduction.cpp`
- Create: `firmware/src/SpeechLeveler.h`
- Create: `firmware/src/SpeechLeveler.cpp`
- Modify: `firmware/platformio.ini`
- Create: `firmware/test/test_noise_reduction/test_noise_reduction.cpp`
- Create: `firmware/test/test_speech_leveler/test_speech_leveler.cpp`

**Interfaces:**
- Produces: `NoiseReduction::process(in, out, 128, speech_present)` with 256-point, 50%-overlap processing and fixed storage.
- Produces: `SpeechLeveler::process(in, out, 128, speech_present)` and stats containing Q16 gain, limiter count, and hard-clip count.
- Consumes: Task 4 `speech_present`; returns one mono block without changing sample count.

- [ ] **Step 1: Write failing noise-reduction tests**

Generate a 1 kHz tone plus deterministic white noise. After the learning interval, require:

- noise-only RMS decreases by at least 6 dB;
- tone correlation with the clean reference remains above 0.95;
- no output sample is an isolated full-scale spike;
- changing `speech_present` to true freezes the noise estimate;
- bypass returns bit-identical samples.

- [ ] **Step 2: Write failing leveler tests**

Require:

- silence does not increase gain above unity;
- low-level speech receives gradual gain but never more than 4x;
- a full-scale transient is limited to at most 32700 without integer wrap;
- gain changes more slowly during release than attack;
- limiter and hard-clip counters are observable.

- [ ] **Step 3: Run both tests and verify RED**

Run:

```powershell
pio test -e native -f test_noise_reduction
pio test -e native -f test_speech_leveler
```

Expected: FAIL because the stateful class APIs do not exist.

- [ ] **Step 4: Refactor the spectral suppressor conservatively**

Retain a 256-point FFT only if the embedded timing budget passes. Use a Hann window,
128-sample hop, decision-directed gain, time/frequency gain smoothing, and a gain floor of
0.25. Update noise only when `speech_present == false`; remove the assumption that the
first 120 frames are always silence. Add a compile-time bypass switch for A/B testing.

- [ ] **Step 5: Implement speech-gated AGC and limiter**

Use RMS-based speech leveling after noise reduction. Initial constants are target RMS 5000,
maximum Q16 gain `4 << 16`, unity minimum gain, fast limiter attack, slow speech release,
and frozen/no-increase behavior when speech is absent. Keep all filter and gain state inside
the object so `reset()` fully restarts a recording.

- [ ] **Step 6: Run tests, build, and record timing**

Run:

```powershell
pio test -e native -f test_noise_reduction
pio test -e native -f test_speech_leveler
pio run -e dayvault
```

Expected: tests pass. Add a debug timing counter measured around the combined 128-sample
processing call; one block must finish well before the block's approximately 8 ms real-time
deadline.

- [ ] **Step 7: Commit**

```powershell
git add firmware/src/NoiseReduction.h firmware/src/NoiseReduction.cpp firmware/src/SpeechLeveler.h firmware/src/SpeechLeveler.cpp firmware/platformio.ini firmware/test/test_noise_reduction firmware/test/test_speech_leveler
git commit -m "feat(audio): add conservative denoising and speech leveling"
```

---

### Task 6: Integrate the DSP Pipeline into Continuous Recording

**Files:**
- Modify: `firmware/src/main.cpp`
- Modify: `firmware/src/PdmCapture.h`
- Modify: `firmware/src/PdmCapture.cpp`
- Modify: `firmware/src/Config.h`
- Modify: `Docs/Serial-Command-Reference.md`

**Interfaces:**
- Consumes: paired PCM from Task 1, verified rate from Task 3, fusion from Task 4, and enhancement from Task 5.
- Produces: mono PCM to the existing `rec_chunk`/FatFs path.
- Produces commands: `AUDIOSTAT` and `AUDIOBYPASS <0|1>`.

- [ ] **Step 1: Add a failing source-contract test**

Extend `tools/dayvault_sync/tests/test_firmware_memory.py` or add a focused source test that
asserts the production recorder calls `pdm_dma_read_dual`, `AudioFusion::process`,
`NoiseReduction::process`, and `SpeechLeveler::process`, and no longer calls the legacy
single-channel DSP path.

- [ ] **Step 2: Run and verify RED**

Run: `python -m pytest tools/dayvault_sync/tests/test_firmware_memory.py -q`

Expected: FAIL because production still calls `pdm_dma_read()`.

- [ ] **Step 3: Integrate one fixed 128-frame block path**

Create static DSP objects and aligned A/B/fused/enhanced arrays. At `rec_start()`, reset all
DSP state and discard the calculated DFSDM settling samples. At `rec_poll_samples()`:

```cpp
int n = pdm_dma_read_dual(raw_a, raw_b, 128);
if (n == 128) {
    fusion.process(raw_a, raw_b, fused, 128);
    noise_reduction.process(fused, denoised, 128,
                            fusion.stats().speech_present);
    leveler.process(denoised, enhanced, 128,
                    fusion.stats().speech_present);
    rec_write_pcm(enhanced, 128);
}
```

Retain partial input until a complete DSP block exists; never discard a short DMA read.
Drain complete buffered blocks during stop, then patch and close WAV exactly as before.

- [ ] **Step 4: Add runtime diagnostics and bypass**

`AUDIOSTAT` reports rate, channel RMS/noise, weights, lag/correlation, fault flags, NR
state, AGC gain, limiter/clips, processing maximum microseconds, and DMA overruns on one
line. `AUDIOBYPASS 1` uses a safe channel soft-selector without NR/EQ/AGC for controlled
A/B recordings; it is volatile and resets to enhanced mode after reboot.

- [ ] **Step 5: Run regression tests and build**

Run:

```powershell
python -m pytest tools/dayvault_sync/tests/test_firmware_memory.py -q
pio test -e native -f test_pdm_rate
pio test -e native -f test_audio_fusion
pio test -e native -f test_noise_reduction
pio test -e native -f test_speech_leveler
pio test -e native -f test_crc32
pio test -e native -f test_export_protocol
pio test -e native -f test_sd_protocol
pio test -e native -f test_winusb_descriptors
pio run -e dayvault
```

Expected: all listed targeted suites pass and firmware builds. Note the pre-existing stale
suite errors separately rather than claiming the unfiltered native suite is green.

- [ ] **Step 6: Commit**

```powershell
git add firmware/src/main.cpp firmware/src/PdmCapture.h firmware/src/PdmCapture.cpp firmware/src/Config.h Docs/Serial-Command-Reference.md tools/dayvault_sync/tests/test_firmware_memory.py
git commit -m "feat(audio): record adaptive dual-mic mono audio"
```

---

### Task 7: Controlled A/B Tuning and Release Verification

**Files:**
- Create: `tools/analyze_audio_calibration.py`
- Create: `tools/dayvault_sync/tests/test_audio_analysis.py`
- Modify: `tools/audio_calibration.py`
- Create: `Docs/09-Audio-Calibration.md`
- Modify: `README.md`
- Modify: tuning constants only in `firmware/src/AudioFusion.cpp`, `firmware/src/NoiseReduction.cpp`, and `firmware/src/SpeechLeveler.cpp`

**Interfaces:**
- Produces: JSON and Markdown reports comparing raw A/B, bypass mono, and enhanced mono.
- Consumes: stereo calibration files/manifests and mono A/B files.

- [ ] **Step 1: Write failing WAV-analysis tests**

Generate small temporary WAV fixtures and assert analysis calculates channel count, sample
rate, RMS, peak, clipping, noise-floor percentile, tone-to-neighbor ratio, correlation, and
best lag. Assert it rejects compressed/non-PCM input and unequal stereo frame data.

- [ ] **Step 2: Run and verify RED**

Run: `python -m pytest tools/dayvault_sync/tests/test_audio_analysis.py -q`

Expected: FAIL because the analysis module does not exist.

- [ ] **Step 3: Implement deterministic analysis and channel mapping**

Use Python standard `wave`, `array`, and `math` modules plus NumPy only when already
available. Infer the forward/inward mapping from controlled WEARER and FRONT scenes only
when each direction has at least a 3 dB advantage; otherwise report mapping as ambiguous
and keep neutral firmware labels A/B. Never infer orientation from the uncontrolled snapshot.

- [ ] **Step 4: Safely flash the integrated firmware**

Repeat the DFU pre-check and post-check from Task 2. Do not alter option bytes. Verify
`INFO`, `PDMRATE`, `AUDIOSTAT`, SD mount, normal recording, USB reconnect, sync download,
and software `DFU` entry before audio tuning.

- [ ] **Step 5: Record bypass and enhanced controlled scenes**

Run the Reminder-driven sequence once with `AUDIOBYPASS 1` and once with
`AUDIOBYPASS 0`, using identical device placement and playback level. The quiet scene must
actually be quiet; the runner announces and timestamps it. Download all files and preserve
the manifests under `artifacts/audio-validation/<firmware-commit>/`.

- [ ] **Step 6: Tune only from measured comparisons**

Acceptance gates:

- zero hard clips in normal wearer and one-metre forward speech;
- enhanced quiet RMS at least 6 dB below the old processed recording under the same scene;
- each direction is no more than 3 dB below its best raw microphone;
- no repeatable isolated spectral spike introduced by NR;
- no audible weight clicks, pumping, watery/musical noise, or comb coloration;
- `AUDIOSTAT` reports zero DMA overruns and processing time below 50% of one block period.

If a gate fails, add a deterministic fixture reproducing it before changing a tuning constant.

- [ ] **Step 7: Run one-hour and power checks**

Record for one hour while forcing periodic SD sync and one USB connect/disconnect cycle.
Verify WAV duration/rate, file finalization, circular free-space check, no DMA overruns, and
no watchdog reset. Measure battery current for existing firmware, bypass dual capture, and
enhanced DSP using the same battery voltage and SD card; record the values in
`Docs/09-Audio-Calibration.md`.

- [ ] **Step 8: Run final verification**

Run the targeted firmware/Python suites from Task 6, `pio run -e dayvault`, and inspect
`git diff --check`. Confirm the firmware binary/map fit the STM32L452RC flash/RAM limits.

- [ ] **Step 9: Commit**

```powershell
git add tools/analyze_audio_calibration.py tools/audio_calibration.py tools/dayvault_sync/tests/test_audio_analysis.py Docs/09-Audio-Calibration.md README.md firmware/src/AudioFusion.cpp firmware/src/NoiseReduction.cpp firmware/src/SpeechLeveler.cpp
git commit -m "test(audio): validate and tune dual-mic recording"
```

---

## Plan Self-Review

- Spec coverage: controlled raw capture, synchronized dual DFSDM, exact rate, per-channel
  conditioning, opposite-direction adaptive fusion, conservative NR, speech AGC, fallback,
  diagnostics, Reminder prompts, mono storage, power, and audio acceptance tests all map to
  Tasks 1-7.
- Type consistency: `pdm_dma_read_dual()` feeds 128-sample `AudioFusion`,
  `NoiseReduction`, and `SpeechLeveler` blocks; all return unchanged sample counts.
- Safety: every flash step repeats DFU visibility checks and explicitly excludes option-byte
  writes.
- Scope: stale unrelated native tests are documented but not repaired.

