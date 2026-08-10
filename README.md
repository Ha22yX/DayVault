<h1 align="center">DayVault</h1>

<p align="center">
  A wearable microphone module and Windows sync app for turning everyday audio into a local, searchable life archive.
</p>

<p align="center">
  <a href="README.zh-CN.md">简体中文</a> &middot;
  <a href="#quickstart">Quickstart</a> &middot;
  <a href="#project-gallery">Gallery</a> &middot;
  <a href="tools/dayvault_sync/README.md">Sync app</a> &middot;
  <a href="Docs/README.md">Hardware docs</a> &middot;
  <a href="Docs/Serial-Command-Reference.md">Serial protocol</a>
</p>

<p align="center">
  <img alt="Status" src="https://img.shields.io/badge/status-active%20prototype-D97706?style=for-the-badge" />
  <img alt="MCU" src="https://img.shields.io/badge/MCU-STM32L452-205A4B?style=for-the-badge&logo=stmicroelectronics&logoColor=white" />
  <img alt="Firmware" src="https://img.shields.io/badge/firmware-PlatformIO-6B7FD7?style=for-the-badge" />
  <img alt="Desktop app" src="https://img.shields.io/badge/desktop-PySide6-2563EB?style=for-the-badge&logo=qt&logoColor=white" />
  <img alt="Audio" src="https://img.shields.io/badge/audio-Ogg%20Opus-5F7F73?style=for-the-badge" />
</p>

<p align="center">
  <img src=".github/assets/readme-hero.svg" alt="DayVault hardware and sync workflow overview" />
</p>

## Project Gallery

<table>
  <tr>
    <td width="50%" align="center">
      <img src=".github/assets/dayvault-sync-app.png" alt="DayVault Sync Windows app downloading audio from the microphone module" />
      <br />
      <strong>Windows sync app.</strong> Detects the module over USB and downloads recordings through a resumable, CRC-verified WinUSB path.
    </td>
    <td width="50%" align="center">
      <img src=".github/assets/dayvault-mic-module.jpg" alt="DayVault microphone module prototype connected over USB-C" />
      <br />
      <strong>Microphone module.</strong> A compact board and battery prototype used for local audio capture experiments.
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src=".github/assets/dayvault-schematic.png" alt="DayVault schematic with STM32L452, PDM microphones, microSD, USB-C, charger, RTC, and power circuits" />
      <br />
      <strong>Schematic.</strong> The current EasyEDA electrical design for the recorder module.
    </td>
    <td width="50%" align="center">
      <img src=".github/assets/dayvault-pcb-layout.png" alt="DayVault PCB layout in EasyEDA" />
      <br />
      <strong>PCB layout.</strong> The board layout snapshot used for review and bring-up planning.
    </td>
  </tr>
</table>

## Why This Exists

I started DayVault because I have been learning PCB design and wanted to build something personal with it: an all-day recording device that can capture every sentence I say and preserve each day as a life archive.

The imagined workflow is simple: carry the microphone module during the day, plug it into the computer at night, let the Windows sync app pull the new `.OPUS` recordings into a local folder, and later use speech-to-text plus AI summaries to prepare a written recap of the day.

The repository keeps the full chain visible:

| Layer | What is here |
| --- | --- |
| Hardware | EasyEDA project, exported netlist, schematic/PCB snapshots, pin maps, BOM, and hardware docs. |
| Firmware | PlatformIO STM32 firmware sources and tests for recording, storage, USB protocol, Ogg Opus writing, and device behavior. |
| Desktop sync | PySide6 Windows app with WinUSB bulk export, CRC32 validation, resume, and legacy CDC fallback. |
| Protocol docs | Serial command reference for listing, downloading, time sync, DFU, battery diagnostics, and file management. |

## Quickstart

### Run The Windows Sync App

```powershell
cd tools/dayvault_sync
pip install -r requirements.txt
python main.py
```

Build a single executable:

```bat
cd tools\dayvault_sync
build_exe.bat
```

The packaged app is written to `tools\dayvault_sync\dist\DayVaultSync.exe`.

### Build Firmware

```powershell
cd firmware
platformio run
```

Run firmware tests when PlatformIO environments are available:

```powershell
cd firmware
platformio test
```

## Sync App Behavior

- Watches USB serial ports for the DayVault device VID/PID.
- Starts sync automatically when a device is inserted.
- Sends host time and local timezone offset to the device on each sync.
- Lists remote recordings and downloads files that are new or whose size changed.
- Writes files to `<sync_folder>\<device_serial>\`.
- Uses `.part` temporary files and retries failed downloads up to three times.
- Prefers `BULK2` WinUSB export, then falls back to `GET2` and legacy `DL2`.
- Streams directly to disk and validates the device/host CRC before finalizing a file.
- Downloads current `.OPUS` recordings and remains compatible with legacy `.WAV` files.
- Keeps state in `%APPDATA%\DayVault\state\<serial>.json`.
- Keeps logs in `%APPDATA%\DayVault\logs\app.log`.
- Minimizes to the system tray instead of exiting when the tray is available.

## Audio Recording

Production recordings are standard Ogg Opus `.OPUS` files: the two microphones are adaptively fused to one mono stream at exactly 16 kHz, then encoded in 20 ms frames at a target 24 kbit/s constrained VBR restricted-SILK setting. Plan for about 265 MB per day. No parallel WAV or PCM original is kept; legacy `.WAV` recordings remain downloadable and removable by circular storage management.

For file naming, clean-stop behavior, power-loss limits, and `OPUSSTAT`, see [Opus recording](Docs/09-Opus-Recording.md).

## Hardware At A Glance

| Subsystem | Selected part | Role |
| --- | --- | --- |
| Main controller | STM32L452RCT6 | PDM capture, storage, USB, RTC, and power-state control. |
| Microphones | 2 x SPH0655LM4H-1-8 | Digital PDM speech capture. |
| Storage | microSD over SPI1 | Local Ogg Opus `.OPUS` recording storage; legacy `.WAV` remains exportable. |
| USB | USB-C full-speed device | Sync, serial protocol, charging input, and DFU path. |
| Power | Protected single-cell LiPo + TPS63031 | Wearable power source and 3.3 V rail. |
| Charging | MCP73831 | USB-powered Li-ion charging. |
| Timekeeping | STM32 RTC + 32.768 kHz crystal | Timestamped recording names. |

## System Architecture

```mermaid
flowchart LR
    Speech["Conversation"] --> Mic["PDM microphones"]
    Mic --> MCU["STM32L452 firmware"]
    MCU --> SD["microSD Ogg Opus files"]
    USB["USB-C CDC / WinUSB"] --> Host["DayVault Sync app"]
    Host --> Folder["Per-device sync folder"]
    Folder --> Archive["Transcription / life archive"]
    Host --> MCU
```

## Repository Map

```text
DayVault/
|-- EDA/                         EasyEDA project and exported netlist
|-- Docs/                        Hardware and protocol documentation
|-- firmware/                    PlatformIO STM32 firmware
|-- tools/dayvault_sync/          Windows desktop sync app
|-- README.md
`-- README.zh-CN.md
```

## Useful Entry Points

- [Sync app documentation](tools/dayvault_sync/README.md)
- [Hardware documentation index](Docs/README.md)
- [Serial command reference](Docs/Serial-Command-Reference.md)
- [Opus recording](Docs/09-Opus-Recording.md)
- [High-speed transfer design and measurements](Docs/High-Speed-Transfer.md)
- [Hardware overview](Docs/01-Hardware-Overview.md)
- [MCU pinout](Docs/02-MCU-Pinout.md)
- [BOM](Docs/07-BOM.md)
- [Contributing guide](CONTRIBUTING.md)

## Current Status

DayVault is an active prototype, not a finished consumer product. The hardware, firmware, and sync tool are all evolving together. Treat this repository as a development record and working prototype source, not as a manufacturing release.

Known practical notes:

- Recording and sync behavior should be validated with real devices and logs.
- Firmware, battery thresholds, storage behavior, acoustic quality, and long-duration reliability still need measured bring-up evidence.
- The desktop sync tool is Windows-focused and depends on Python, PySide6, pyserial, PyUSB, and libusb when run from source.
- No open-source license has been selected yet.

## Privacy And Safety

DayVault is intended for personal recording. Recording other people may require notice or consent depending on local law and context. Protect raw audio and transcripts with strong access controls and decide retention rules before collecting sensitive conversations.

This is an unvalidated wearable electronics prototype with a Li-ion battery and charger. Use protected cells, verify polarity, provide strain relief, test charging temperature, and do not leave early hardware unattended while charging or recording.

## License

No open-source license has been selected yet. Unless a license is added later, all rights remain with the repository owner; published design and source files may be inspected but are not automatically licensed for reuse, modification, or redistribution.
