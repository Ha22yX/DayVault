# Dual-Microphone Adaptive Audio Design

**Date:** 2026-08-11  
**Status:** Approved direction; implementation not started  
**Target:** DayVault STM32L452 recorder firmware

## 1. Objective

Improve speech intelligibility and reduce audible background noise while preserving the
all-day, low-power recording goal. The device is worn on the chest. One microphone faces
forward toward conversation partners and the other faces inward toward the wearer. The
wearer's voice and the forward speaker have equal priority.

The production recording remains mono. Both microphones are captured and processed
internally, then adaptively fused into one 16-bit PCM stream so storage and USB transfer
requirements do not double.

## 2. Current-State Findings

The present firmware contains the hardware setup for two DFSDM filters and two DMA
buffers, but `pdm_dma_read()` only consumes `pdm_dma_buf`. It advances the second read
position without using `pdm_dma_buf2`, so production WAV files contain only one
microphone.

The active single-channel processing chain is also too aggressive for a distant-speech
recorder:

- a 2 kHz peaking filter adds about 5 dB;
- a 3 kHz peaking filter adds about 10 dB;
- AGC can apply up to 8x gain;
- clipping occurs before and after parts of the filter chain;
- the FFT noise-reduction implementation exists but is not called by the recorder.

Analyzed recordings report sample rates around 20.97-21.07 kHz even though the public
configuration says 16 kHz and filter comments assume about 21.7 kHz. This mismatch makes
filter placement and storage estimates unreliable.

The previously observed `DUAL` result was collected while uncontrolled computer audio was
playing. It is not a silence measurement and must not be used as a microphone-noise,
sensitivity, or fault conclusion. The existing diagnostic also compares same-index DMA
samples without first proving channel synchronization. A controlled dual-raw capture is
required before tuning.

## 3. Constraints

- Do not modify STM32 option bytes or any DFU/boot configuration.
- Keep USB VID `0x0483` and the existing software DFU path intact.
- Preserve continuous recording and circular-delete behavior.
- Preserve the existing mono WAV contract for the sync application.
- Do not double SD storage or USB transfer volume in production mode.
- Avoid dynamic allocation in the audio path.
- Processing must not cause PDM DMA or SD ring-buffer overruns.
- Additional continuous CPU load must be measured because this is a 400 mAh wearable.

## 4. Considered Approaches

### 4.1 Equal average

Gain-match and average both microphones on every sample. This is inexpensive and can
reduce uncorrelated electronic noise by up to about 3 dB when channels observe the same
signal. It is rejected as the primary method because the microphones face opposite
directions. A fixed average can add clothing noise, weaken the better microphone, and
produce comb filtering when the two copies of one voice have different delays.

### 4.2 Fixed delay-and-sum beamforming

Align both channels for one look direction and sum them. This is rejected because a fixed
beam would favor either the wearer or the forward conversation partner, while both are
equally important. It is also a poor fit for changing head, body, and partner positions.

### 4.3 Adaptive two-sided fusion

Condition and score both channels independently, then use smoothly varying weights. A
clear forward speaker favors the forward microphone; the wearer's voice favors the inward
microphone. Correlated copies may be delay-aligned and combined, while poorly correlated
or contaminated channels are cross-faded rather than blindly summed. This is the selected
approach.

## 5. Signal-Path Architecture

```text
PDM shared data line
        |
        +--> DFSDM channel/filter A --> DMA A --+
        |                                       |
        +--> DFSDM channel/filter B --> DMA B --+--> alignment and validation
                                                --> per-channel conditioning
                                                --> frame quality analysis
                                                --> adaptive fusion
                                                --> conservative noise suppression
                                                --> speech AGC and limiter
                                                --> mono PCM ring --> WAV --> microSD
```

The audio path is separated into five testable stages. Each stage has a bypass mode so
recordings can be compared without rebuilding unrelated firmware.

## 6. Stage 1: Controlled Dual-Raw Capture

Before production fusion is enabled, add a bounded diagnostic that records both unprocessed
DFSDM outputs as a short stereo WAV. This mode is diagnostic only and is never used for
all-day recording.

Capture the following controlled scenes using identical placement:

1. quiet room for at least 10 seconds;
2. wearer speaking normally for at least 10 seconds;
3. speaker one metre in front for at least 10 seconds;
4. wearer and front speaker alternating;
5. light clothing contact and normal walking movement.

The host-side calibration runner invokes
`C:\Users\Administrator\Desktop\Windows Reminder\reminder.cmd` to announce each scene.
It shows a short preparation notice, a three-second countdown, and explicit start/stop
notifications for quiet and speech segments. The runner records the corresponding host
timestamps in a test manifest so analysis uses the intended windows instead of guessing
from waveform energy. DayVault firmware and the production sync application do not depend
on Windows Reminder; it is test orchestration only. No notification is shown until the user
explicitly starts a calibration run.

The desktop analysis reports, per channel:

- actual sample rate from DMA sample counts;
- DC offset, RMS, peak, clipping count, and crest factor;
- speech-band and out-of-band energy;
- narrow spectral peaks;
- gain ratio, polarity, normalized cross-correlation, and best integer lag;
- channel dropout or repeated-sample evidence.

No adaptive-fusion constants are finalized until this capture proves that both microphone
streams are valid.

## 7. Stage 2: Acquisition and Channel Alignment

Configure the two consecutive DFSDM channels for opposite PDM clock edges and start their
filters in a synchronized manner. Maintain independent DMA write positions and verify that
both streams advance at the same rate. Discard the microphone/filter settling interval
after PDM clock start.

The target production PCM rate is 16 kHz because it covers speech and reduces continuous
storage and DSP work. The selected DFSDM clock-divider and oversampling combination must:

- keep the SPH0655 in its documented normal-mode clock range;
- produce a measured, stable output rate;
- use that exact rate for WAV headers and all filter coefficients.

If an exact 16 kHz rate cannot be produced from the existing clock tree without destabilizing
other peripherals, use the nearest verified rate consistently instead of patching the WAV
rate from recording duration.

Keep samples in at least 32-bit intermediate form through calibration and filtering. Apply
saturation only at explicit output boundaries. Detect a dead, stuck, or persistently
clipped channel and fall back to the healthy channel.

## 8. Stage 3: Per-Channel Conditioning

Process each microphone independently before fusion:

- remove DC and apply an approximately 100 Hz high-pass filter;
- apply a speech-band low-pass near the Nyquist-safe upper limit;
- perform slow gain matching, constrained to a safe range such as +/-6 dB;
- track a noise estimate only during non-speech frames;
- calculate clipping, impulsiveness, low-frequency rub, and high-frequency noise penalties.

Remove the existing +5 dB and +10 dB peaking cascade. Any speech-presence EQ added later
is limited to a broad, measured boost of about 0-2 dB and is applied after the raw signal
path is known to be clean.

## 9. Stage 4: Adaptive Fusion

Use 20 ms analysis frames with quality updates every 10 ms. For each channel, estimate a
quality score from speech-band signal-to-noise ratio and subtract penalties for clipping,
clothing rub, impulsive noise, and persistent tones.

The fusion behavior is:

- when one channel has clearly better speech quality, smoothly favor it;
- when both channels contain useful but different speech, retain both with bounded weights;
- when channels are strongly correlated, estimate a small integer delay before combining;
- when correlation is weak, avoid phase-sensitive equal summation and use a soft selector;
- smooth weight changes to avoid clicks, stereo-like image jumps, and syllable pumping;
- never allow an unverified or degraded channel to dominate solely because it is louder.

Initial production weight limits are 0.10 to 0.90 rather than hard 0/1 switching. These are
tuning bounds, not final constants. A failed-channel state overrides the limits and selects
the healthy channel completely.

Because the microphones face opposite directions, this block is a two-sided speech selector
and mixer, not a fixed beamformer.

## 10. Stage 5: Noise Suppression, AGC, and Limiting

Run noise suppression after fusion so the expensive spectral stage processes one stream.
Start with a conservative mode:

- update the noise spectrum only in confidently non-speech frames;
- smooth gains over both time and neighboring frequency bins;
- keep the spectral gain floor around 0.20-0.30;
- limit maximum attenuation to avoid musical noise and isolated noise speckles;
- retain a bypass mode for objective A/B recordings.

The existing 0.06 gain floor is too aggressive for the desired natural conversation sound.
The implementation may use an optimized CMSIS-DSP FFT or a lower-cost fixed-point path,
but only after measuring CPU time and stack/RAM use.

Apply AGC after noise suppression:

- use speech-gated level estimation;
- freeze or release gain slowly during silence;
- target a natural speech level rather than a constant peak;
- limit make-up gain initially to about 4x;
- use fast peak limiting near -1 dBFS;
- count every limiter activation and hard clip for diagnostics.

## 11. Mechanical Noise Requirement

The inward microphone port must not rub directly against clothing. The enclosure or clip
must maintain an air gap and use an acoustically transparent barrier that does not seal the
port. Firmware can recognize and reduce some low-frequency rub, but it cannot recover
speech masked by direct mechanical contact.

## 12. Runtime Diagnostics

Extend diagnostics without changing normal recording commands:

- report both raw RMS/peak/DC values;
- report measured channel lag and correlation confidence;
- report current fusion weights and channel-fault flags;
- report estimated noise level, AGC gain, limiter count, and processing time;
- retain `RAW` and `DUAL` compatibility where practical;
- provide a short dual-raw diagnostic command with an explicit maximum duration.

Diagnostic commands must not block the recorder long enough to overrun DMA. A controlled
capture writes through the existing ring/SD path or pauses normal recording explicitly.

## 13. Acceptance Tests

### Functional

- Both microphone DMA streams contain independent, valid audio.
- Production WAV remains mono and is accepted by the existing sync application.
- USB, DFU, RTC filenames, circular deletion, and low-battery behavior are unchanged.
- One failed microphone still produces a valid recording from the other microphone.

### Audio

- No hard clipping in normal wearer and one-metre conversation tests.
- Quiet-scene output noise is at least 6 dB below the current processed recording under
  the same placement and environment.
- Neither wearer nor forward speaker is suppressed by more than 3 dB relative to the best
  available microphone for that speaker.
- No obvious pumping, clicking, comb-filter coloration, or musical-noise speckles.
- Fixed tonal interference is removed at its acquisition source where possible; a notch is
  added only when a repeatable raw-capture tone remains.
- Speech-to-text output on the same test script improves or does not regress for both
  directions.

### Performance and power

- Zero PDM DMA and audio ring overruns during a one-hour stress recording.
- DSP processing has measured headroom during worst-case SD activity.
- Continuous current is measured before and after; an unexplained increase is a release
  blocker.
- At 16 kHz, mono 16-bit PCM remains about 2.76 GB per 24 hours instead of doubling for
  stereo storage.

## 14. Delivery Sequence

1. Add dual-raw diagnostic capture and host analysis.
2. Prove/synchronize both DFSDM channels and establish the real sample rate.
3. Add safe per-channel conditioning with all enhancement bypassed.
4. Implement adaptive two-sided fusion and failed-channel fallback.
5. Replace aggressive EQ/AGC with the conservative chain.
6. Add and benchmark post-fusion noise suppression.
7. Run controlled A/B recordings, tune constants, and document measured results.
8. Build release firmware only after regression and power tests pass.
