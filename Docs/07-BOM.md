# Bill of Materials

Generated from `EDA/DayVault.netlist.json`, schematic component count: **50**. Values and
supplier codes below describe the saved design; availability, lifecycle, footprint, voltage
rating, and assembly suitability must be rechecked before ordering.

## Integrated circuits and functional parts

| Qty | References | Manufacturer part/value | LCSC code | Saved footprint | Function |
| ---: | --- | --- | --- | --- | --- |
| 2 | U1, U2 | SPH0655LM4H-1-8 | C22376158 | MIC-SMD_SPH0655LM4H-1-8 | 1.8 V PDM microphones. |
| 1 | U3 | MCP73831T-2ACI/OT | C424093 | SOT-23-5 | 1-cell Li-ion charger. |
| 1 | U4 | XC6206P182MR | C21659 | SOT-23-3 | Fixed 1.8 V LDO. |
| 1 | U5 | STM32L452RCT6 | C192722 | LQFP-64, 0.5 mm pitch | Main MCU. |
| 1 | U6 | TPS63031DSKR | C15516 | SON-10 with exposed pad | Fixed 3.3 V buck-boost. |
| 1 | U7 | TXU0202DCUR | C5186957 | VSSOP-8 | Dual fixed-direction level translator. |
| 1 | D1 | USBLC6-2SC6 | C7519 | SOT-23-6 | USB D+/D- ESD protection. |
| 1 | L1 | SWPA3015S1R5NT, 1.5 uH | C56594 | 3.0 x 3.0 mm inductor | TPS63031 power inductor. |
| 1 | X1 | CM8V-T1A-32.768KHZ-7PF-20PPM-TA-QC | C5136974 | 2.0 x 1.2 mm crystal | 32.768 kHz RTC crystal, 7 pF. |
| 1 | CARD1 | TF-01A | C91145 | TF-SMD_TF-01A | microSD socket. |
| 1 | USB1 | TYPE-C 16PIN 2MD(073) | C2765186 | USB-C-SMD_TYPE-C-16PIN-2MD-073 | USB 2.0 Type-C receptacle. |
| 2 | SW1, SW2 | TS-1088-AR02016 | C720477 | 3.9 x 3.0 mm SMD switch | RESET and BOOT buttons. |

## Resistors

All saved resistor footprints are 0603. Use at least 1% parts unless a later calculation
specifies tighter tolerance.

| Qty | References | Value | Purpose |
| ---: | --- | ---: | --- |
| 2 | R1, R2 | 5.1 kOhm | USB Type-C CC1/CC2 Rd pull-downs. |
| 4 | R3, R4, R5, R6 | 10 kOhm | BOOT0 pull-down, NRST pull-up, charger current programming, SD_CS pull-up. |
| 2 | R7, R8 | 1 MOhm | Battery ADC divider. |
| 2 | R9, R10 | 100 kOhm | USB VBUS detect divider; schematic-only until PCB synchronization. |

## Capacitors

All saved capacitor footprints are 0603.

| Qty | References | Value | Main use |
| ---: | --- | ---: | --- |
| 15 | C1, C5-C12, C15, C19-C23 | 100 nF | Local decoupling, reset RC, battery sense filter. |
| 1 | C26 | 10 nF | USB insertion-detect filter; schematic-only until PCB synchronization. |
| 4 | C2, C3, C4, C16 | 10 uF | Converter input/output and microSD bulk decoupling. |
| 2 | C13, C14 | 4.7 uF | Charger input and battery output capacitors. |
| 2 | C17, C18 | 1 uF | 1.8 V LDO input and output capacitors. |
| 2 | C24, C25 | 10 pF | RTC crystal load capacitors. |

Selection requirements:

- Use C0G/NP0 dielectric for C24 and C25.
- Use X7R/X5R for rail capacitors and account for DC-bias loss, especially 10 uF in 0603.
- USB_5V and BAT capacitors should have comfortable voltage margin; 10 V-rated parts are a
  practical starting point, subject to exact vendor curves.
- Confirm the TPS63031 input/output effective capacitance and ESR against its datasheet.
- Place every 100 nF decoupler at its assigned IC supply pin, not merely anywhere on the net.

## Off-board items

| Qty | Item | Requirement |
| ---: | --- | --- |
| 1 | Li-polymer battery | Intended 802525, 3.7 V, 400 mAh, with protection board and correctly polarized leads. |
| 1 | microSD card | High-endurance, capacity selected from final codec and retention target. |
| 1 | USB-C cable | USB 2.0 data-capable cable, not charge-only. |
| 1 | Battery wire strain relief | Mechanical retention so solder joints cannot carry pull force. |

## BOM validation before purchase

- Confirm every exact LCSC code is stocked and eligible for the intended JLCPCB assembly
  service tier.
- Verify symbol pin numbers against the manufacturer datasheet, not only the marketplace
  listing.
- Verify footprint dimensions, pin-1 orientation, exposed pad, acoustic port, connector
  shell, and microSD card insertion direction with a 1:1 print.
- Confirm L1 saturation current and DCR for worst-case converter load.
- Confirm the selected 400 mAh cell permits the programmed approximately 100 mA charge rate.
- Freeze passive manufacturer parts and voltage/tolerance/dielectric fields before production.
- Regenerate this table after any future schematic/PCB change and reconcile the remaining
  DRC-reported netlist mismatch before manufacturing release.
