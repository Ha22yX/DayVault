# Opus Recording

DayVault records conversations as standard Ogg Opus files. The device combines its two PDM microphones with adaptive fusion, then stores one 16-bit mono stream at exactly 16 kHz. Noise reduction and speech leveling run before encoding.

## Format and storage

- New recordings use the `.OPUS` extension and contain standard Ogg Opus data. They can be decoded by normal Ogg Opus-compatible software.
- The encoder uses 20 ms frames (320 samples), target **24 kbit/s** constrained VBR, and restricted SILK for speech.
- The STM32L452 production build uses Opus complexity 0 and compiles only the vendored codec at `-O2`; the rest of the firmware remains size-optimized. A 30.6-second board test measured a 10.8 ms slowest encode time and zero paired DMA overruns.
- Budget about **265 MB per day** of recording. Actual size varies with constrained VBR and Ogg container overhead.
- Production recording writes only `.OPUS`; it does not keep a parallel WAV or raw PCM original.
- Existing `.WAV` recordings are legacy files. They remain available for export and are eligible for circular deletion, but new recordings are not WAV files.

## Names

With a set RTC, a recording begins with a local-time name such as `REC-20260811-1430.OPUS`. On a clean stop it is renamed to include the measured duration, for example `REC-20260811-1430_5m32s.OPUS` or `REC-20260811-1430_1h04m03s.OPUS`.

If the minute-level start name already exists, DayVault adds the collision suffix **before** the duration: `REC-20260811-1430_1.OPUS` while recording, then `REC-20260811-1430_1_5m32s.OPUS` after stopping. Without a valid RTC, the fallback form is `REC001.OPUS`.

## Stop and recovery

Audio is grouped into Ogg pages of at most 50 packets, or about one second. The recorder checkpoints newly completed pages roughly once per second with `f_sync`.

On a clean stop, DayVault stops capture, drains pending audio through the DSP, accounts for Opus encoder lookahead, writes the final Ogg EOS page, syncs, closes, and then adds the duration to timestamped filenames. Low-battery stopping uses the same finalization path.

After abrupt power loss or removal, the last successfully synced Ogg prefix remains available. The page being assembled or written at the time of loss may be absent, so the latest audio can be lost and the filename may retain its start-time form without a duration.

## `OPUSSTAT`

Send `OPUSSTAT` over USB CDC to print the recorder's current or most recent statistics. It does not start capture or create a recording.

| Field | Meaning |
| --- | --- |
| `bitrate`, `rate` | Configured target bit rate and PCM sample rate. |
| `frames`, `pages`, `valid`, `bytes` | Encoded frame count, Ogg page count, valid 16 kHz input samples, and bytes written. |
| `enc_err`, `max_us`, `workspace` | Encoder error count, slowest encode time in microseconds, and Opus workspace use. |
| `dma_overruns` | Paired microphone DMA overruns. |
| `fusion_wa`, `fusion_wb`, `fusion_faults` | Adaptive fusion weights and microphone fault flags. |
| `nr_ready`, `nr_gain` | Noise-reduction model readiness and average gain. |
| `level_gain`, `limiter` | Speech-leveler gain and limiter activations. |
| `active`, `primary`, `cleanup`, `err` | Recording state and primary, cleanup, and latest recorder result. |

The complete serial command and export reference is in [Serial-Command-Reference.md](Serial-Command-Reference.md).
