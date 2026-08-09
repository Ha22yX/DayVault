# DayVault PC Sync Tool — Design

Date: 2026-08-10
Status: Approved by user

## Goal

A Windows desktop application (EXE) that:
- Detects DayVault recorder boards automatically when plugged in (USB CDC, VID 0483 / PID 5740).
- Auto-syncs each device's clock (time + timezone offset) on connect.
- Auto-downloads all not-yet-synced audio files from each device into a user-configured folder.
- Runs resident in the system tray; closing the window hides to tray, background detection continues, and only an explicit tray "Exit" terminates the program.

## Background / constraints

- Device USB identity: `USB\VID_0483&PID_5740\<serial>` where `<serial>` (e.g. `206C36943831`) is unique per MCU — the per-device key.
- Firmware serial protocol (COM9 @115200): `SETTIME <unix> [tz_minutes]` (overwrites stored tz offset), `LIST` (list root, `  <name> <size>` lines), `DL2 <file>` (chunked ACK download, preferred; prints `DLSTART <size>` then binary data; the host sends a single ACK byte, e.g. `G`, whenever no data has arrived for ~60 ms to prompt the next chunk — same behavior as the existing `download2.ps1`).
- Recordings are named `REC-YYYYMMDD-HHMM[_n][_dur].WAV` (local time) or legacy `REC###.WAV`; durations like `_5m32s` / `_1h23m45s`.
- Python 3.14 + pySerial 3.5 available on the dev machine.
- The device may be mid-recording while connected (USB attached → awake; LIST/DL2 no longer freeze recording after the firmware no-re-mount fix).

## Design

### 1. Architecture & tech stack

- **PySide6 (Qt)** GUI + **pySerial** serial + **PyInstaller** single EXE (`--windowed --onefile`).
- Threading: GUI on the main thread; all serial I/O (LIST, DL2, SETTIME) runs in a per-device worker `QThread` that emits signals (device list update, file list, download progress, errors) back to the UI. Never block the UI thread.
- Device detection: a `QTimer` (~1.5 s) polls `pyserial.tools.list_ports`, filters COM ports whose `VID:PID` is `0483:5740` (from the PNP `USB\VID_0483&PID_5740\`), diffs against the previous set → connect/disconnect events.
- Device key = the USB serial from the PNP id (e.g. `206C36943831`).

### 2. Auto-sync & download flow (per device, on connect)

Per-device worker, serialized per device, devices in parallel:
1. **Sync clock**: `SETTIME <now_unix> <local_tz_offset_minutes>` (offset = `datetime.now().utcoffset()` in minutes; overwrites stored offset on every sync).
2. **List**: `LIST` → parse `  <name> <size>` lines into a file list.
3. **Diff**: load the device's state file `{name: size}`; "to download" = listed files not in state, or size changed.
4. **Download** each missing file via `DL2` to `<sync_folder>/<serial>/`, writing to `<name>.part` then rename on success; update the state file after each successful download.
5. **Failures**: unplug mid-download → mark failed, retry on next insert; COM busy → backoff retry; non-DayVault 0483:5740 device → LIST fails → mark unrecognized and skip.

### 3. Main window, tray, settings

- **Main window**:
  - Device selector (multiple boards: tab or combo, keyed by serial).
  - File table: name, size, time (from name), sync status ("已下载" / "未下载").
  - Settings area: sync folder path picker, poll interval, launch-on-startup (optional).
- **Tray**: window close hides to tray (background detection continues). Tray icon left-click opens the window; tray menu "打开主界面" / "退出". Only "退出" exits the process.
- **Persistence**:
  - Config: `%APPDATA%/DayVault/config.json` (sync folder, poll interval, autostart).
  - Per-device state: `%APPDATA%/DayVault/state/<serial>.json` (`{name: size}`).
  - Logs: `%APPDATA%/DayVault/logs/app.log` (rotating).

### 4. Edge cases

- Recording in progress: a file without `_dur` suffix may still be growing; DL2 downloads the current bytes; a later size change re-downloads.
- Device asleep: USB attach wakes it; connected device is always awake.
- `.part` residue: cleaned on next download attempt.
- Two boards recording the same minute: per-device folders avoid collisions.

### 5. Testing & packaging

- Unit tests (host-side, no device): filename/`LIST` parsing, state-file load/save, tz offset computation.
- Integration (physical device): plug → auto time/tz sync → auto download of unsynced files to `<folder>/<serial>/` → UI shows 已下载/未下载; window close → tray resident; tray click reopens; tray Exit really exits; unplug/plug behavior.
- Packaging: PyInstaller build command; verify the single EXE runs.
