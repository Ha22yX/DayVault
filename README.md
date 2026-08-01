# DayVault

DayVault is a battery-powered wearable voice logger designed to record a full day,
store audio on microSD, and export the recordings over USB for speech-to-text and
personal archive processing.

## Repository layout

- `EDA/DayVault.eprj2` - EasyEDA project snapshot.
- `EDA/DayVault.netlist.json` - schematic netlist exported through the EasyEDA API.
- `EDA/Backups/` - EasyEDA automatic project backups.
- `Docs/` - hardware, pinout, firmware, bring-up, and known-issue documentation.

## Hardware snapshot

- MCU: STM32L452RCT6
- Microphones: 2 x SPH0655LM4H-1-8 PDM MEMS
- Storage: microSD over SPI1
- USB: USB-C USB 2.0 Full Speed, charging, and ROM DFU
- Battery: single-cell protected Li-polymer battery
- Main rail: TPS63031 3.3 V buck-boost
- Microphone rail: XC6206P182MR 1.8 V LDO
- RTC: 32.768 kHz crystal on the STM32 backup domain

## Start here

Read [Docs/README.md](Docs/README.md) before changing hardware or writing firmware.
Chinese quick reference: [Docs/00-开发速查.md](Docs/00-开发速查.md).
The current schematic contains a blocking stereo-PDM pin assignment issue documented
in [Docs/06-Known-Issues.md](Docs/06-Known-Issues.md).

## Privacy

DayVault is intended for personal recording. Recording other people may require clear
notice or consent depending on local law and context. Keep raw recordings and generated
transcripts encrypted and access-controlled.
