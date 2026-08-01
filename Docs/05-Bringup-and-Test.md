# Board Bring-up and Test Checklist

Do not begin with the Li-polymer cell connected. Use a current-limited bench supply and a
known-good USB cable. Record measured values against this checklist for every board revision.

## 1. Unpowered inspection

- [ ] Confirm battery pad polarity markings: pad 1 `BAT`, pad 2 `GND`.
- [ ] Inspect USB-C, MCU, charger, converter, LDO, level translator, microphones, and
  microSD socket for bridges or rotated parts.
- [ ] Verify the microphone acoustic-port holes are open and clean.
- [ ] Verify TPS63031 exposed pad is soldered and switch-node components are placed exactly
  as required by its reference layout.
- [ ] Measure resistance from `BAT`, `3V3`, `1V8`, and `USB_5V` to GND. Investigate a hard
  short before applying power.
- [ ] Run EasyEDA schematic ERC and PCB DRC and archive the reports with the board revision.

## 2. Bench-supply power test

1. Set the supply to 3.7 V with a conservative current limit, initially 50 mA.
2. Connect supply positive to `BAT` and negative to `GND`.
3. If the current limit is reached, disconnect immediately and inspect the power stage.
4. Measure `3V3`; it should regulate near 3.3 V.
5. Measure `1V8`; it should regulate near 1.8 V.
6. Check U6, U4, U5, U7, and the microphones for abnormal heating.
7. Sweep the bench source through the intended cell range while monitoring both rails and
   current. Do not exceed component ratings.

Repeat with an oscilloscope under microSD write bursts. Check 3.3 V ripple and undershoot at
the MCU, card socket, and converter feedback point, not only at a convenient test pad.

## 3. SWD recovery path

Before relying on USB DFU, temporarily contact PA13/SWDIO, PA14/SWCLK, NRST, 3V3, and GND
with test probes or a programming fixture.

- [ ] STM32CubeProgrammer reads the device ID.
- [ ] Flash and option bytes are readable.
- [ ] A minimal firmware can toggle a spare test point or report over the debugger.
- [ ] Reset works from SWD and from SW1.

Permanent SWD test pads are strongly recommended for production and fault recovery.

## 4. ROM USB DFU

1. Hold SW2 BOOT so BOOT0 is high.
2. Press and release SW1 RESET.
3. Release SW2 BOOT.
4. Attach USB-C and check that STM32CubeProgrammer detects the ROM DFU device.
5. Program a minimal image, return BOOT0 low, reset, and verify normal flash boot.

Check both USB-C plug orientations. If enumeration fails, inspect CC1/CC2, D+/D- continuity,
ESD-array orientation, the 3.3 V VDDUSB rail, and the firmware/boot clock configuration.

## 5. RTC test

- [ ] Firmware reports that LSE starts successfully.
- [ ] Set UTC through the debugger or USB utility and verify the calendar after reset.
- [ ] Enter a low-power mode and verify elapsed time after several hours.
- [ ] Measure 24-hour drift and record board temperature.
- [ ] Remove all power and confirm firmware correctly marks RTC invalid on the next boot.

## 6. Battery ADC test

1. Wait at least 250 ms after applying power.
2. Measure BAT and BAT_SENSE with a calibrated multimeter.
3. Confirm BAT_SENSE is approximately BAT/2.
4. Compare ADC-derived battery voltage at several bench-supply settings.
5. Store gain/offset calibration if the required accuracy cannot be met analytically.
6. Test warning, graceful-stop, and recovery hysteresis without a microSD filesystem error.

## 7. USB insertion detection

- [ ] Without USB, PA9 reads low.
- [ ] With USB, the divider node measures about 2.5 V and PA9 reads high.
- [ ] Attach/detach interrupts are debounced and logged once per event.
- [ ] Insertion during recording closes or syncs the active file before storage ownership
  changes.

## 8. microSD test

Begin at a low SPI clock, then increase only after initialization is reliable.

- [ ] Card initializes and mounts in SPI mode.
- [ ] A file can be created, written, synced, closed, remounted, and verified on a PC.
- [ ] Sustained writes exceed the audio producer rate with worst-case cards.
- [ ] DMA/buffer overrun count remains zero during a multi-hour test.
- [ ] Sudden reset damages at most the current segment, not the entire filesystem.
- [ ] Removal or card failure is handled without a firmware deadlock.
- [ ] 3.3 V remains in tolerance during card current spikes.

Run tests with at least two card models and with a nearly full, fragmented filesystem.

## 9. PDM microphone test

Current board acceptance is mono:

- [ ] PDM clock is present at MCU side (`PDM_CLK_3V3`) and microphone side (`PDM_CLK`).
- [ ] Clock amplitudes remain inside their respective 3.3 V and 1.8 V rails.
- [ ] `PDM_DATA` and `PDM_DATA_3V3` show activity without contention.
- [ ] DFSDM DMA produces stable nonzero samples.
- [ ] A 1 kHz acoustic test tone has the expected sample rate and no dropped blocks.
- [ ] Speech is intelligible at the intended mounting distance.
- [ ] Stopping PDM clock substantially reduces microphone current.

After moving data to PB12 for stereo, separately confirm both SELECT phases, channel identity,
gain matching, relative delay, and simultaneous DMA operation.

## 10. Charger and battery test

Use only the intended protected cell and monitor temperature throughout the first charge.

- [ ] USB_5V is present with either plug orientation.
- [ ] Charge current is close to the R5-programmed value under valid battery conditions.
- [ ] Battery voltage never exceeds the charger/cell limit.
- [ ] Charger enters precondition, constant-current, constant-voltage, and termination states
  as expected.
- [ ] The system load does not prevent termination or cause repeated recharge cycling.
- [ ] Unplugging USB does not reset or corrupt an active recording.
- [ ] Reversed battery wiring cannot occur in the final assembly process.

Because the board has no power-path controller, characterize charging both while idle and
while recording. Do not enclose the battery until thermal behavior is known.

## 11. USB application test

- [ ] CDC/control endpoint exchanges version, battery, time, and status data.
- [ ] Host time synchronization updates RTC and records an event.
- [ ] MSC is exposed only after recording stops and FatFs unmounts.
- [ ] Firmware refuses to resume local writes while the host owns the volume.
- [ ] Eject/detach returns card ownership cleanly to firmware.
- [ ] Repeated cable attach/detach creates no filesystem corruption.

## 12. Power and endurance qualification

Measure current at the battery in each real state:

| State | Conditions | Measured current |
| --- | --- | ---: |
| Recording PCM | PDM + DFSDM + SD active | TBD |
| Recording compressed | final codec and file sync policy | TBD |
| USB file export | USB + SD active | TBD |
| Graceful low-battery sleep | PDM clock and SD stopped | TBD |
| RTC-only/lowest supported state | final firmware | TBD |

Calculate runtime from measured average current, not MCU datasheet current alone:

```text
runtime_hours ~= usable_battery_mAh / average_battery_current_mA
```

Apply margin for converter efficiency, card variation, battery aging, cold temperature, and
the protected cell's cutoff. Validate with at least three full day/night cycles while logging
battery voltage, resets, overruns, SD latency, and file checksums.

## Release gates

A revision is not ready for unattended wearable use until all of the following are true:

- [ ] Schematic ERC and all electrical PCB DRC violations are resolved or formally waived.
- [ ] The PDM mono/stereo capability matches the product claim.
- [ ] Low-battery shutdown always leaves a readable filesystem.
- [ ] USB, charging, and recording ownership transitions are deterministic.
- [ ] Battery charging and enclosure temperatures are safe.
- [ ] A 24-hour recording survives and every segment decodes correctly.
- [ ] The exact manufacturing files and matching documentation are tagged in Git.
