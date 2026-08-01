# STM32L452RCT6 Pinout

This file is the firmware-facing source of truth for MCU U5. The `Current connection`
column describes the saved EasyEDA project. `NC` means no electrical connection in the
schematic; it does not mean the pin may be driven arbitrarily.

## Assigned signals

| Function | Net | MCU pin | STM32 peripheral | Firmware use |
| --- | --- | --- | --- | --- |
| Battery voltage | `BAT_SENSE` | PA0, pin 14 | ADC1 input | Read the 1:1 divider using a long ADC sample time. |
| PDM clock | `PDM_CLK_3V3` | PC2, pin 10 | DFSDM1_CKOUT | Generate the microphone clock. |
| PDM data, current | `PDM_DATA_3V3` | PB1, pin 27 | DFSDM1_DATIN0 | One-microphone/mono bring-up only; see required revision below. |
| microSD chip select | `SD_CS` | PA4, pin 20 | GPIO output | Idle high; assert low for card transactions. |
| microSD clock | `SD_SCK` | PA5, pin 21 | SPI1_SCK | SPI mode 0 is the normal starting point. |
| microSD data to MCU | `SD_MISO` | PA6, pin 22 | SPI1_MISO | Card DO. |
| microSD data from MCU | `SD_MOSI` | PA7, pin 23 | SPI1_MOSI | Card DI/CMD. |
| USB insertion detect | `USB_DETECT` | PA9, pin 42 | GPIO input | Low without VBUS, about 2.5 V/high with VBUS. |
| USB D- | `USB_DM` | PA11, pin 44 | USB_FS_DM | Native USB Full Speed. |
| USB D+ | `USB_DP` | PA12, pin 45 | USB_FS_DP | Native USB Full Speed. |
| SWD data | none yet | PA13, pin 46 | SWDIO | Reserve for SWD test pads/header. |
| SWD clock | none yet | PA14, pin 49 | SWCLK | Reserve for SWD test pads/header. |
| RTC crystal input | internal net `$1N133` | PC14, pin 3 | OSC32_IN | X1, 32.768 kHz. |
| RTC crystal output | internal net `$1N134` | PC15, pin 4 | OSC32_OUT | X1, 32.768 kHz. |
| Reset | internal net `$1N49` | NRST, pin 7 | NRST | 10 kOhm pull-up, 100 nF to GND, pushbutton to GND. |
| ROM boot select | internal net `$1N47` | PH3/BOOT0, pin 60 | BOOT0 | 10 kOhm pull-down, pushbutton to 3.3 V. |

## Required stereo PDM revision

The current board connects the shared microphone data line to
`PB1/DFSDM1_DATIN0`. STM32 DFSDM single-wire stereo reception requires a pair of
consecutive channels, `x` and `x-1`, fed from `DATINx`; therefore `x` must be at least 1.
`DATIN0` cannot form that pair.

Before ordering the stereo revision:

1. Disconnect `PDM_DATA_3V3` from PB1, pin 27.
2. Connect `PDM_DATA_3V3` to PB12, pin 33, which supports `DFSDM1_DATIN1`.
3. Leave PB1 unconnected unless another function is deliberately assigned.
4. In firmware, configure DFSDM channel 1 as direct input and channel 0 as redirected
   input, sample opposite clock edges, and feed two filters/DMA streams.

With the current PCB, develop and verify mono capture first. Do not claim two independent
microphone channels until this revision is made and tested.

## Complete 64-pin map

| Pin | Symbol name | Current connection | Direction/use |
| ---: | --- | --- | --- |
| 1 | VBAT | `3V3` | RTC/backup-domain supply. Not raw battery voltage. |
| 2 | PC13 | NC | Spare GPIO; low drive capability restrictions apply. |
| 3 | PC14-OSC32_IN | X1/C25 | LSE input. |
| 4 | PC15-OSC32_OUT | X1/C24 | LSE output. |
| 5 | PH0-OSC_IN | NC | Main crystal input unused; internal clock is expected. |
| 6 | PH1-OSC_OUT | NC | Main crystal output unused. |
| 7 | NRST | reset RC/button | Active-low hardware reset. |
| 8 | PC0 | NC | Spare. |
| 9 | PC1 | NC | Spare. |
| 10 | PC2 | `PDM_CLK_3V3` | DFSDM1 clock output. |
| 11 | PC3 | NC | Spare. |
| 12 | VSSA/VREF- | `GND` | Analog ground/reference negative. |
| 13 | VDDA/VREF+ | `3V3` | Analog supply/reference positive. |
| 14 | PA0 | `BAT_SENSE` | ADC battery measurement. |
| 15 | PA1 | NC | Spare. |
| 16 | PA2 | NC | Spare. |
| 17 | PA3 | NC | Spare. |
| 18 | VSS | `GND` | Digital ground. |
| 19 | VDD | `3V3` | Digital supply. |
| 20 | PA4 | `SD_CS` | microSD SPI chip select. |
| 21 | PA5 | `SD_SCK` | SPI1 clock. |
| 22 | PA6 | `SD_MISO` | SPI1 MISO. |
| 23 | PA7 | `SD_MOSI` | SPI1 MOSI. |
| 24 | PC4 | NC | Spare. |
| 25 | PC5 | NC | Spare. |
| 26 | PB0 | NC | Spare. |
| 27 | PB1 | `PDM_DATA_3V3` | Current DFSDM1_DATIN0; move to PB12 for stereo. |
| 28 | PB2 | NC | Spare/boot-related restrictions must be checked before use. |
| 29 | PB10 | NC | Spare. |
| 30 | PB11 | NC | Spare. |
| 31 | VSS | `GND` | Digital ground. |
| 32 | VDD | `3V3` | Digital supply. |
| 33 | PB12 | NC | Recommended future `DFSDM1_DATIN1`. |
| 34 | PB13 | NC | Spare. |
| 35 | PB14 | NC | Spare. |
| 36 | PB15 | NC | Spare. |
| 37 | PC6 | NC | Spare. |
| 38 | PC7 | NC | Spare. |
| 39 | PC8 | NC | Spare. |
| 40 | PC9 | NC | Spare. |
| 41 | PA8 | NC | Spare. |
| 42 | PA9 | `USB_DETECT` | VBUS-presence GPIO input. |
| 43 | PA10 | NC | Spare. |
| 44 | PA11 | `USB_DM` | USB Full-Speed D-. |
| 45 | PA12 | `USB_DP` | USB Full-Speed D+. |
| 46 | PA13/JTMS/SWDIO | NC | Keep available for programming/debug. |
| 47 | VSS | `GND` | USB/digital ground. |
| 48 | VDDUSB | `3V3` | USB transceiver supply; never connect to USB 5 V. |
| 49 | PA14/JTCK/SWCLK | NC | Keep available for programming/debug. |
| 50 | PA15/JTDI | NC | Spare. |
| 51 | PC10 | NC | Spare. |
| 52 | PC11 | NC | Spare. |
| 53 | PC12 | NC | Spare. |
| 54 | PD2 | NC | Spare. |
| 55 | PB3/JTDO/TRACESWO | NC | Optional SWO; preserve if possible. |
| 56 | PB4/NJTRST | NC | Spare after debug configuration. |
| 57 | PB5 | NC | Spare. |
| 58 | PB6 | NC | Spare. |
| 59 | PB7 | NC | Spare. |
| 60 | PH3/BOOT0 | boot pull-down/button | Low for normal flash boot; high during reset for ROM loader. |
| 61 | PB8 | NC | Spare. |
| 62 | PB9 | NC | Spare. |
| 63 | VSS | `GND` | Digital ground. |
| 64 | VDD | `3V3` | Digital supply. |

## Firmware reservation rules

- Do not repurpose PA11, PA12, PA13, PA14, PC14, PC15, or the selected DFSDM pins.
- Keep PA13/PA14 physically accessible even though USB DFU is planned; SWD is the recovery
  path for clock, option-byte, bootloader, and early-startup failures.
- Configure unused GPIOs in analog mode with no pull, unless a peripheral datasheet or board
  net requires another state.
- Configure `SD_CS` high before enabling SPI to avoid accidental card selection at boot.
- Treat the complete table above as the saved-revision truth; update it whenever the schematic
  pin assignment changes.
