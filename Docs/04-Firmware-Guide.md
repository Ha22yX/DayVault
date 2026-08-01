# Firmware Development Guide

## Recommended first milestone

Bring the board up in this order before adding compression or noise reduction:

1. Internal system clock, GPIO, serial/SWD diagnostics, and RTC LSE.
2. Battery ADC and USB insertion input.
3. SPI1 microSD initialization and a sustained write test.
4. One PDM edge through PB12/DFSDM Channel 1 and DMA.
5. Valid mono WAV file written in fixed-duration segments.
6. USB device mode and host time synchronization.
7. Low-battery shutdown and recovery.
8. Redirected Channel 0, dual-channel capture, DSP, and compression.

The saved PCB routes PDM data to PB12/DFSDM1_DATIN1, which supports the intended paired
Channel 1/Channel 0 topology. Treat stereo as unverified until both channels pass the tests
in [05-Bringup-and-Test.md](05-Bringup-and-Test.md).

## Peripheral allocation

| Function | Peripheral/pins | Suggested implementation |
| --- | --- | --- |
| PDM input | DFSDM1, PC2 clock and PB12/DATIN1 data | DMA double buffer; validate Channel 1 mono first, then redirected Channel 0. |
| microSD | SPI1, PA4/PA5/PA6/PA7 | FatFs over HAL/LL SPI with DMA where practical. |
| USB | USB FS, PA11/PA12 | Device stack: DFU is ROM bootloader; application may expose MSC/CDC. |
| Battery ADC | PA0 | ADC1, long sample time, averaged readings. |
| USB detect | PA9 | GPIO input, optional EXTI on both edges. |
| RTC | LSE on PC14/PC15 | RTC calendar in UTC plus backup-register validity marker. |
| Debug/program | PA13/PA14 | SWD; never disable permanently during bring-up. |

Use STM32CubeMX/CubeIDE to validate the exact alternate-function and DMA request mapping
for the selected STM32L452RCT6 package. Preserve the board net names in firmware defines.

## Boot sequence

1. Start from the internal oscillator and initialize clocks conservatively.
2. Configure unused GPIOs as analog/no-pull; set `SD_CS` output high immediately.
3. Read reset cause and store it in the event log.
4. Start LSE and RTC. Check an RTC-valid magic value in a backup register.
5. Sample `USB_DETECT` and `BAT_SENSE` before starting recording.
6. Mount microSD. If mount fails, retry with bounded backoff and indicate/log the error.
7. Initialize DFSDM, DMA buffers, file writer, and watchdog.
8. Open a new recording segment only after time and storage state are known.

If USB is attached during boot, firmware may enter a host-management mode instead of
recording. Do not expose a writable USB mass-storage volume while FatFs is simultaneously
writing to the same card.

## PDM capture

### Current board: PB12/DATIN1

- PC2 produces the DFSDM clock; the TXU0202 converts it from 3.3 V to 1.8 V.
- PB12 receives the level-shifted shared data line as DFSDM1_DATIN1.
- Start with one microphone/one clock edge and verify polarity, gain, and data integrity.
- Use DMA ping-pong buffers so SD latency never blocks the acquisition interrupt.
- Monitor buffer overruns and save an overrun counter with each file/session.

### Two microphones on one data wire

With the current PB12/DFSDM1_DATIN1 assignment:

- Configure channel 1 for direct `DATIN1` reception.
- Configure channel 0 for redirected reception from channel 1.
- Use opposite serial-interface clock edges for the two channels.
- Route channels through two DFSDM filters and DMA streams.
- Calibrate the fixed sample offset and gain mismatch before beamforming or summing.

The microphone `SELECT` pins are already strapped oppositely. Firmware cannot change that
selection without a hardware revision.

## Audio format and daily storage

Recommended acquisition rate is 16 kHz speech-band audio. Record mono after any two-mic
processing; keeping stereo for the archive doubles storage without necessarily improving
speech transcription.

| Format | Nominal rate | Approximate data per 24 h | Role |
| --- | ---: | ---: | --- |
| PCM, 16 kHz, 16-bit mono | 256 kbit/s | 2.76 GB | Simplest bring-up and diagnostics. |
| IMA ADPCM, 16 kHz mono | 64 kbit/s | 691 MB | Good first production format; low MCU cost. |
| Opus, speech mono | 12-24 kbit/s | 130-259 MB | Best storage efficiency, but much more firmware work. |

These figures exclude filesystem/container overhead. Use a high-endurance card with ample
free space; do not size the card exactly to one day's nominal payload.

For development, produce standard WAV PCM first, then WAV IMA ADPCM. Validate every file
with a desktop decoder before changing the encoder. Opus should be added only after CPU,
RAM, stack, and worst-case SD-write margins are measured on the exact firmware build.

## File structure and integrity

Suggested layout:

```text
/DAYVAULT/
  2026/
    08/
      01/
        20260801T083000Z_0001.wav
        20260801T090000Z_0002.wav
        events.csv
```

- Keep RTC internally in UTC; apply local timezone during export/transcription.
- Segment files every 15 to 30 minutes so one interrupted write cannot lose the day.
- Preallocate each segment when possible to reduce FAT updates and fragmentation.
- Periodically update file length and sync metadata, balancing corruption exposure against
  write amplification. Start with a 10-second sync interval and measure it.
- On clean shutdown, stop acquisition, drain buffers, update the WAV header, `f_sync`,
  close, and unmount.
- Log boot, reset cause, card failure, buffer overrun, low battery, USB attach/detach, and
  time synchronization events.

## RTC and trusted time

The 32.768 kHz LSE lets the MCU RTC keep time in low-power modes while 3.3 V remains
present. It does not preserve time after battery removal/protection cutoff because VBAT is
tied to 3.3 V rather than an independent backup cell.

Recommended policy:

1. Store an RTC-valid magic value and last time-sync source in backup registers.
2. On USB connection, receive UTC from a desktop tool and update RTC atomically.
3. If RTC is invalid, write into an `UNSYNCED` directory using a boot counter rather than
   pretending the calendar is correct.
4. Record both the old and new timestamp when correcting time during an active day.
5. Measure LSE drift over several days and store a calibration value if needed.

## Battery measurement

For a 12-bit ADC and equal R7/R8 values:

```text
Vbat = ADC_code / 4095 * Vref * 2
```

Use VREFINT calibration or a measured 3.3 V rail for better accuracy. The divider's
Thevenin resistance is 500 kOhm and C23 is 100 nF, so its time constant is about 50 ms.
Wait roughly 250 ms after a cold start, select the longest practical ADC sample time, take
multiple readings, and calibrate against a multimeter.

Example policy to tune with the actual protected cell under load:

| State | Starting threshold | Action |
| --- | ---: | --- |
| Normal | above 3.50 V | Record normally. |
| Warning | at/below 3.50 V | Log warning and reduce nonessential work. |
| Graceful stop | at/below 3.30 V for several samples | Close files, stop PDM clock, unmount card. |
| Recovery | above 3.55 V or USB attached | Permit recording after debounce. |

Use hysteresis and time filtering; never stop on a single transient sample. Thresholds are
engineering starting points, not cell specifications. Validate against the actual battery,
load pulse, protection board, and converter behavior.

After graceful stop, stop DFSDM/PDM clock, deinitialize SPI/USB as appropriate, configure
GPIOs for low leakage, and enter STM32 Standby/Shutdown. The 3.3 V converter remains on
because TPS63031 EN is tied to BAT, but the MCU and microphones can reach low current:
SPH0655 sleep is entered when its clock stops.

## USB behavior

There are two distinct firmware paths:

- **ROM DFU:** selected with BOOT0 high during reset. No application firmware is needed;
  use STM32CubeProgrammer over USB.
- **Application USB:** implement USB device classes in normal firmware. A practical design
  is CDC for control/time sync and MSC only when recording is stopped and FatFs is unmounted.

Use `USB_DETECT` for attach/detach policy rather than inferring VBUS solely from USB stack
events. Debounce the input for at least several milliseconds. On attach, finish the current
file safely before switching ownership of the card to MSC.

## Noise reduction strategy

Do not begin with a large neural denoiser on the MCU. Establish an auditable pipeline:

1. High-pass filter around 80-120 Hz to reduce handling/rumble.
2. DC removal and conservative automatic gain control/limiter.
3. With revised stereo hardware, delay/gain calibration and simple directional combining.
4. Optional stationary-noise suppression after measuring CPU headroom.
5. Leave heavyweight transcription and enhancement to the nightly desktop/AI pipeline.

Mechanical design dominates distant clarity: expose both acoustic ports correctly, isolate
them from clothing rub and PCB vibration, and keep them away from the switching converter,
USB connector, and microSD current loops.

## Watchdog and failure recovery

- Enable the independent watchdog only after all blocking paths have bounded timeouts.
- Never wait indefinitely for a card or USB operation.
- Record reset cause on the next successful boot.
- If SD writes repeatedly fail, stop capture cleanly rather than overwriting RAM or looping
  at full current.
- Keep a small emergency buffer so the current WAV header can be finalized on low battery.
