# Known Issues and Required Revisions

Status snapshot: 2026-08-01. This file deliberately distinguishes a saved connection from
a verified design. Closing an item requires a schematic/PCB change, test evidence, and a
Git commit; deleting the text is not closure.

## Release blockers

### DV-001: current PDM data pin cannot implement intended one-wire stereo

**Status:** Open, schematic and PCB change required.

`PDM_DATA_3V3` currently terminates at PB1/DFSDM1_DATIN0. STM32 DFSDM one-wire stereo uses
two consecutive channels `x` and `x-1` from input `DATINx`; DATIN0 has no lower partner.

**Required change:** move the net to PB12/DFSDM1_DATIN1 and use channel 1 direct plus
channel 0 redirected with opposite sampling edges. Current hardware is acceptable only for
mono bring-up until proven otherwise.

References:

- [ST AN5027: Interfacing PDM digital microphones using STM32 MCUs and MPUs](https://www.st.com/resource/en/application_note/dm00380469-interfacing-pdm-digital-microphones-using-stm32-mcus-and-mpus-stmicroelectronics.pdf)
- [ST AN4990: Getting started with sigma-delta digital interface on STM32](https://www.st.com/content/ccc/resource/technical/document/application_note/group0/b2/44/42/9d/46/b4/4d/34/DM00354333/files/DM00354333.pdf/jcr%3Acontent/translations/en.DM00354333.pdf)
- [STM32L452RC datasheet](https://www.st.com/resource/en/datasheet/stm32l452rc.pdf)

### DV-002: schematic and PCB are not synchronized

**Status:** Open.

The exported schematic netlist contains 50 components, while the API inspection found 47
PCB components. R9, R10, and C26 for `USB_DETECT` were added to the schematic but were not
synchronized/placed/routed on the PCB at the time of this snapshot.

**Required change:** update PCB from schematic, place the divider/filter, connect PA9, rerun
DRC, and re-export the netlist before generating manufacturing files.

### DV-003: PCB DRC is not clean

**Status:** Open.

The inspected PCB reported 152 DRC errors. Most are component/body or device-to-through-hole
clearance warnings associated with the intentionally compact placement, which the project
owner chose not to optimize at this stage. However, the report also includes electrical
copper clearances that cannot be waived merely as component-distance issues:

- 4 through-hole-pad-to-track clearances.
- 4 hole-to-track clearances.
- 1 SMD-pad-to-through-hole-pad clearance.
- 1 hole-to-SMD-pad clearance.
- 1 SMD-pad-to-slot clearance.
- 2 USB-footprint internal slot clearances requiring footprint review.

**Required change:** inspect each non-body violation by coordinates/nets, remove all possible
shorts and manufacturing-rule violations, and keep a signed-off DRC report.

### DV-004: PCB has no ground copper pour

**Status:** Open.

The API inspection found no copper pours. A continuous ground return is important for USB,
microSD current pulses, the switching converter, MCU decoupling, and microphone noise.

**Required change:** add an uninterrupted GND plane/pour, stitch it appropriately, and route
USB/switching/audio signals with controlled return paths. Refill copper and rerun DRC after
every routing change.

### DV-005: power routing and converter layout need a dedicated review

**Status:** Open.

BAT, 3V3, GND, and USB_5V traces were observed at approximately 0.2 mm width. That is too
fragile as a blanket choice for charger current, converter loops, and microSD peak current.
The PCB also did not contain normal via objects in the inspected data.

**Required change:** follow the TPS63031 reference layout, keep switch nodes tiny, use broad
power/ground copper appropriate to current and fabrication rules, place input/output
capacitors at the pins, and confirm every layer transition is intentional and manufacturable.

## High-priority design risks

### DV-006: no power-path/load-sharing controller

The MCP73831 charger and active recorder share the BAT node. System current can prevent
correct charge termination and makes charge-state interpretation ambiguous. USB insertion
may therefore charge slowly or never terminate while recording.

For a prototype, characterize charging in every operating state. For a wearable production
revision, use a charger/power-path architecture designed to power the system and charge the
battery simultaneously.

### DV-007: MCU cannot physically disconnect the main rails

TPS63031 EN is tied to BAT and XC6206 has no enable pin. Low-battery firmware can stop PDM,
close/unmount the card, and enter STM32 Standby/Shutdown, but it cannot turn off 3V3/1V8.
Quiescent current continues until battery protection disconnects.

This may be acceptable after measurement. If not, revise EN control with a safe hold/latch
or load-switch design that permits both startup and MCU-controlled shutdown.

### DV-008: no independent RTC backup source

STM32 VBAT is connected to 3V3. RTC survives MCU low-power modes but loses time when the
main battery is removed or its protection board disconnects. Firmware must detect invalid
RTC state and resynchronize from USB; add a backup cell/supercapacitor only if continuity
through complete battery loss is a product requirement.

### DV-009: no permanent SWD connector/test pads documented on PCB

USB ROM DFU can program a healthy MCU, but it cannot replace SWD for early clock failures,
bad option bytes, hard faults before USB startup, or deep debugging. Add accessible SWDIO,
SWCLK, NRST, 3V3, and GND test pads before production.

### DV-010: charging safety depends on external battery protection and assembly

The board has no reverse-polarity protection, fuse, battery temperature sensor, or connector
keying. The intended cell must have a protection board, wire polarity must be controlled,
and the soldered leads need strain relief. Validate charger and enclosure temperature on the
body-worn product; battery protection is not a substitute for thermal qualification.

### DV-011: USB differential routing needs revision

The inspected connector-to-MCU paths were approximately 2.62 mm mismatched in total length,
used 0.2 mm traces, and crossed layers. USB Full Speed is tolerant compared with high-speed
USB, but the final layout should still route D+/D- together over a continuous reference plane
with consistent geometry, minimal stubs, and the ESD part immediately at the connector.

### DV-012: high-current microSD behavior is unverified

A 10 uF plus 100 nF card decoupling network is present, but cards can draw sharp current
pulses and have long internal write stalls. Validate rail droop and buffer capacity with
multiple cards. Consider additional local bulk capacitance only after measurement and power
stage stability review.

## Functional limitations to document

### DV-013: card-detect is not connected

CARD1 pin 9 is NC. Firmware must infer card presence from command responses and handle
removal at arbitrary times. Connecting card detect is optional, not required for recording.

### DV-014: charger STAT is not connected

Firmware cannot directly report precharge/charging/complete state. It can only infer USB
presence and battery voltage. Connect STAT to a GPIO/LED in a later revision if a trustworthy
charge indication is required.

### DV-015: USB DFU entry is manual

The hardware requires BOOT and RESET sequencing. Merely inserting Type-C does not force ROM
DFU. Normal firmware can later implement a signed/validated update command that jumps to ROM
DFU or an application bootloader, but SWD recovery should remain available.

### DV-016: no external high-speed crystal

PH0/PH1 are unused. USB and precise peripheral clocks must be generated from the supported
internal oscillator/PLL/HSI48 and Clock Recovery System configuration. Verify this in
CubeMX and on hardware; do not copy an HSE-based clock tree from another board.

### DV-017: USB_DETECT input margin should be measured

The equal 100 kOhm divider produces about 2.5 V at nominal 5 V VBUS. This is expected to be
read as high by a 3.3 V GPIO, but the worst-case VBUS, resistor tolerance, MCU input threshold,
and startup state should be checked. The schematic addition is not yet on the PCB.

### DV-018: microphone mechanics are not electrically verifiable

Long-distance intelligibility depends strongly on port geometry, wind/clothing protection,
mechanical isolation, microphone spacing, and enclosure leakage. Electrical gain or software
noise suppression cannot recover speech blocked by the enclosure. Prototype and measure the
complete mechanical assembly.

## Documentation policy

- The canonical editable source is `EDA/DayVault.eprj2`.
- `EDA/DayVault.netlist.json` is an API export used for review, not a replacement for source.
- Manufacturing exports must be generated only from a Git-tagged revision whose schematic,
  PCB, netlist, BOM, and documentation agree.
- Re-run the EasyEDA API inspection after every schematic-to-PCB synchronization.
