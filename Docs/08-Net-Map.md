# Net-to-Pin Map

This is the net-centric companion to the component pinout. It was transcribed from the
EasyEDA API export dated 2026-08-01. Endpoint notation is `reference.pin (pin name)`.

## Named signal and power nets

| Net | Connected endpoints |
| --- | --- |
| `1V8` | C18.2; C19.1; C20.1; C21.1; U1.5 VDD; U2.2 SELECT; U2.5 VDD; U4.2 VOUT; U7.3 VCCA; U7.6 OE |
| `3V3` | C3.2; C4.2; C5.2; C6.2; C8.2; C9.1; C10.1; C11.2; C12.1; C15.2; C16.2; C17.2; C22.2; CARD1.4 VDD; R4.2; R6.1; SW2.2; U4.3 VIN; U5.1 VBAT; U5.13 VDDA/VREF+; U5.19 VDD; U5.32 VDD; U5.48 VDDUSB; U5.64 VDD; U6.1 VOUT; U6.10 FB; U7.7 VCCB |
| `BAT` | C1.1; C2.1; C14.1; R7.1; U3.3 VBAT; U6.5 VIN; U6.6 EN; U6.8 VINA; battery positive pad |
| `BAT_SENSE` | C23.1; R7.2; R8.1; U5.14 PA0 |
| `GND` | All supply returns; MCU U5 pins 12, 18, 31, 47, 63; card pins 6 and 10-13; USB shell/GND; battery negative pad |
| `PDM_CLK` | U1.4 CLOCK; U2.4 CLOCK; U7.4 A2Y |
| `PDM_CLK_3V3` | U5.10 PC2; U7.1 B2 |
| `PDM_DATA` | U1.1 DATA; U2.1 DATA; U7.5 A1 |
| `PDM_DATA_3V3` | U5.33 PB12; U7.8 B1Y. U5.27 PB1 is NC. |
| `SD_CS` | CARD1.2 DAT3/CS; R6.2; U5.20 PA4 |
| `SD_SCK` | CARD1.5 CLK; U5.21 PA5 |
| `SD_MISO` | CARD1.7 DAT0/DO; U5.22 PA6 |
| `SD_MOSI` | CARD1.3 CMD/DI; U5.23 PA7 |
| `USB_5V` | C13.2; D1.5; R9.2; U3.4 VDD; USB1 A4/B9 and B4/A9 VBUS |
| `USB_DETECT` | C26.2; R9.1; R10.2; U5.42 PA9 |
| `USB_DM_CONN` | USB1 A7/B7; D1.1 |
| `USB_DM` | D1.6; U5.44 PA11 |
| `USB_DP_CONN` | USB1 A6/B6; D1.3 |
| `USB_DP` | D1.4; U5.45 PA12 |

## Ground endpoint detail

The GND net includes:

- Capacitor returns: C1-C26 wherever shown as ground in the schematic.
- U1.2 SELECT and U1.3 GND; U2.3 GND.
- U3.2 VSS; U4.1 GND; U6.3 PGND, U6.7 PS/SYNC, U6.9 GND, U6.11 EP;
  U7.2 GND.
- U5.12 VSSA/VREF- and U5.18/31/47/63 VSS.
- CARD1.6 VSS and CARD1.10/11/12/13 shell grounds.
- D1.2, R1.1, R2.1, R3.2, R5.2, R8.2, R10.1, and SW1.1.
- USB1 A1/B12, B1/A12, and shell pins 13/14.

## Auto-generated internal nets

The `$1N...` names are EasyEDA-generated and may change after editing. Identify them by
function/endpoints rather than storing the string in firmware.

| Current net | Connected endpoints | Function |
| --- | --- | --- |
| `$1N11` | L1.2; U6.2 L2 | TPS63031 switch node. |
| `$1N14` | L1.1; U6.4 L1 | TPS63031 switch node. |
| `$1N47` | R3.1; SW2.1; U5.60 BOOT0 | ROM boot selection. |
| `$1N49` | C7.2; R4.1; SW1.2; U5.7 NRST | Reset network. |
| `$1N58` | R1.2; USB1.B5 CC2 | USB-C CC2. |
| `$1N59` | R2.2; USB1.A5 CC1 | USB-C CC1. |
| `$1N73` | R5.1; U3.5 PROG | Charger current programming. |
| `$1N133` | C25.1; X1.2; U5.3 PC14/OSC32_IN | LSE input node. |
| `$1N134` | C24.2; X1.1; U5.4 PC15/OSC32_OUT | LSE output node. |

## Intentionally unconnected functions

- CARD1 pins 1 DAT2, 8 DAT1, and 9 card-detect.
- U3 pin 1 STAT.
- USB1 SBU1/SBU2.
- Main HSE crystal pins PH0/PH1.
- SWDIO/SWCLK have no connector in the saved schematic.
- MCU pins shown as `NC` in [02-MCU-Pinout.md](02-MCU-Pinout.md).

After a schematic edit, regenerate `EDA/DayVault.netlist.json` and compare this file before
firmware pin definitions or manufacturing outputs are accepted.
