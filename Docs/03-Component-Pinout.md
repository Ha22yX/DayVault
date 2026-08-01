# Peripheral Components and Net Relationships

This document records the saved schematic connection of every functional component.
Reference designators match the EasyEDA project and exported netlist.

## U1 and U2: SPH0655LM4H-1-8 PDM microphones

| Pin | Name | U1 connection | U2 connection | Purpose |
| ---: | --- | --- | --- | --- |
| 1 | DATA | `PDM_DATA` | `PDM_DATA` | Shared, tri-stated PDM data line. |
| 2 | SELECT | `GND` | `1V8` | Selects opposite PDM clock edges for the two microphones. |
| 3 | GND | `GND` | `GND` | Ground. |
| 4 | CLOCK | `PDM_CLK` | `PDM_CLK` | Shared 1.8 V PDM clock. |
| 5 | VDD | `1V8` | `1V8` | Microphone supply. |

C20 decouples U1 VDD and C19 decouples U2 VDD; both are 100 nF to GND. Place each
capacitor beside its microphone supply pin. The bottom acoustic port must align with a
PCB hole and enclosure sound channel, with no copper, mask, adhesive, or contamination
blocking it.

## U7: TXU0202DCUR level translator

| Pin | Name | Net | Function |
| ---: | --- | --- | --- |
| 1 | B2 | `PDM_CLK_3V3` | 3.3 V clock input from MCU. |
| 2 | GND | `GND` | Ground. |
| 3 | VCCA | `1V8` | A-side supply. |
| 4 | A2Y | `PDM_CLK` | 1.8 V clock output to microphones. |
| 5 | A1 | `PDM_DATA` | 1.8 V microphone data input. |
| 6 | OE | `1V8` | Translator always enabled while the 1.8 V rail exists. |
| 7 | VCCB | `3V3` | B-side supply. |
| 8 | B1Y | `PDM_DATA_3V3` | 3.3 V data output to MCU. |

C21 is 100 nF from VCCA to GND; C22 is 100 nF from VCCB to GND. The component implements
the required fixed directions: MCU clock B-to-A and microphone data A-to-B.

## U5: STM32L452RCT6

See [02-MCU-Pinout.md](02-MCU-Pinout.md) for the complete 64-pin table. Supply grouping:

- Pins 19, 32, and 64 VDD connect to `3V3`.
- Pins 18, 31, 47, and 63 VSS connect to `GND`.
- Pin 13 VDDA/VREF+ connects to `3V3`; pin 12 VSSA/VREF- connects to `GND`.
- Pin 48 VDDUSB connects to `3V3`, not USB VBUS.
- Pin 1 VBAT connects to `3V3` and powers the RTC backup domain while 3.3 V is present.

Local 100 nF capacitors C6, C8, C9, C10, C11, and C12 are present on 3.3 V/GND in the
saved design. During PCB review, assign one directly to each VDD/VDDA/VDDUSB supply point
and keep the loop short; do not group all decouplers in one physical location.

## U6: TPS63031DSKR 3.3 V buck-boost converter

| Pin | Name | Net | Function |
| ---: | --- | --- | --- |
| 1 | VOUT | `3V3` | Regulated output. |
| 2 | L2 | internal switch node `$1N11` | One side of L1. |
| 3 | PGND | `GND` | Power ground. |
| 4 | L1 | internal switch node `$1N14` | Other side of L1. |
| 5 | VIN | `BAT` | Power input. |
| 6 | EN | `BAT` | Always enabled whenever the battery is connected. |
| 7 | PS/SYNC | `GND` | Power-save operating selection. |
| 8 | VINA | `BAT` | Analog/input supply. |
| 9 | GND | `GND` | Signal ground. |
| 10 | FB | `3V3` | Output feedback/sense. |
| 11 | EP | `GND` | Exposed pad; solder to ground. |

L1 is 1.5 uH. C1 100 nF and C2 10 uF are input capacitors. C3 and C4 are 10 uF output
capacitors and C5 is 100 nF output bypass. These values and the chosen inductor must be
validated against the exact TPS63031 datasheet recommendations and component saturation
current before manufacture. Keep both switch-node loops extremely compact.

## U4: XC6206P182MR 1.8 V LDO

| Pin | Name | Net |
| ---: | --- | --- |
| 1 | GND | `GND` |
| 2 | VOUT | `1V8` |
| 3 | VIN | `3V3` |

C17 is 1 uF from input to GND and C18 is 1 uF from output to GND. The LDO has no enable
pin, so 1.8 V remains powered whenever 3.3 V is present.

## U3: MCP73831T-2ACI/OT Li-ion charger

| Pin | Name | Net | Function |
| ---: | --- | --- | --- |
| 1 | STAT | NC | Open-drain charge status is not used. |
| 2 | VSS | `GND` | Ground. |
| 3 | VBAT | `BAT` | Battery charge output. |
| 4 | VDD | `USB_5V` | 5 V charging input. |
| 5 | PROG | R5 to `GND` | Charge-current program. |

R5 is 10 kOhm, giving approximately 100 mA nominal charge current. C13 is 4.7 uF from
USB_5V to GND and C14 is 4.7 uF from BAT to GND. Only use a protected, single-cell Li-ion
or Li-polymer battery. There is no charger power-path/load-sharing controller.

## CARD1: TF-01A microSD socket

| Pin | Card signal | Net |
| ---: | --- | --- |
| 1 | DAT2 | NC |
| 2 | DAT3/CS | `SD_CS` |
| 3 | CMD/DI | `SD_MOSI` |
| 4 | VDD | `3V3` |
| 5 | CLK/SCLK | `SD_SCK` |
| 6 | VSS | `GND` |
| 7 | DAT0/DO | `SD_MISO` |
| 8 | DAT1 | NC |
| 9 | CD | NC |
| 10, 11, 12, 13 | shell/GND | `GND` |

R6 is a 10 kOhm pull-up from SD_CS to 3.3 V. C15 100 nF and C16 10 uF decouple the card.
Card detect is intentionally unused; firmware determines availability from card command
responses.

## USB1: USB Type-C receptacle, USB 2.0 device

| Connector pins | Net | Purpose |
| --- | --- | --- |
| A1/B12 and B1/A12 | `GND` | USB ground. |
| A4/B9 and B4/A9 | `USB_5V` | VBUS input. |
| A5 CC1 | R2 5.1 kOhm to GND | Device/sink Rd. |
| B5 CC2 | R1 5.1 kOhm to GND | Device/sink Rd. |
| A6 and B6 | `USB_DP_CONN` | D+ before ESD array. |
| A7 and B7 | `USB_DM_CONN` | D- before ESD array. |
| A8 and B8 | NC | SBU signals unused. |
| EH pins 13 and 14 | `GND` | Connector shield/hold-down tabs. |

Both same-polarity data contacts are joined so either plug orientation works. This is a
USB 2.0 peripheral/sink implementation; it is not USB host or USB Power Delivery.

## D1: USBLC6-2SC6 USB ESD protector

| Pin | Net |
| ---: | --- |
| 1 | `USB_DM_CONN` |
| 2 | `GND` |
| 3 | `USB_DP_CONN` |
| 4 | `USB_DP` |
| 5 | `USB_5V` |
| 6 | `USB_DM` |

Place D1 immediately behind the connector. Route connector data into pins 1/3 and leave
through pins 6/4 toward the MCU, without stubs. Pin 2 needs a very short ground path.

## X1: 32.768 kHz RTC crystal

- X1 pin 1 connects to PC15/OSC32_OUT and C24 10 pF to GND.
- X1 pin 2 connects to PC14/OSC32_IN and C25 10 pF to GND.
- The crystal is marked 7 pF load capacitance in the project.

Keep X1 and both load capacitors beside the MCU. Route no clocks, USB, SD, PDM, or switch
nodes beneath or near this loop. Do not place a ground trace between the two crystal pins.

## Reset and ROM boot controls

- R4 10 kOhm pulls NRST to 3.3 V; C7 100 nF connects NRST to GND; SW1 pulls NRST to GND.
- R3 10 kOhm pulls BOOT0 to GND; SW2 connects BOOT0 to 3.3 V while pressed.
- Normal boot: release both buttons and reset/power the board.
- ROM USB DFU: hold BOOT, press RESET, release RESET, then release BOOT.

## Battery sensing and USB detection

Battery measurement:

- R7 1 MOhm: `BAT` to `BAT_SENSE`.
- R8 1 MOhm: `BAT_SENSE` to GND.
- C23 100 nF: `BAT_SENSE` to GND.
- `BAT_SENSE` connects to PA0.

USB insertion measurement:

- R9 100 kOhm: `USB_5V` to `USB_DETECT`.
- R10 100 kOhm: `USB_DETECT` to GND.
- C26 10 nF: `USB_DETECT` to GND.
- `USB_DETECT` connects to PA9.

## Battery connection pads

The PCB uses two large free through-hole pads rather than a connector:

| Pad | Net | Approximate finished geometry |
| --- | --- | --- |
| 1 | `BAT` | 1.8 mm copper diameter, 0.8 mm drill |
| 2 | `GND` | 1.8 mm copper diameter, 0.8 mm drill |

Mark polarity clearly on silkscreen and add mechanical strain relief for battery wires.
The intended battery must include its own protection board.
