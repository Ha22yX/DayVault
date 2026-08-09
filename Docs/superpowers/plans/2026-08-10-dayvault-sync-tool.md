# DayVault PC Sync Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Windows desktop EXE (PySide6) that auto-detects DayVault boards (USB VID 0483 / PID 5740), auto-syncs their clocks (time + PC timezone offset), auto-downloads unsynced recordings into a configured folder, and runs resident in the system tray.

**Architecture:** A Python package under `tools/dayvault_sync/` split into protocol/parsing (`proto`), serial I/O (`dio`, incl. DL2 chunked download), state/config (`store`), a plain sync engine (`engine`, testable without Qt), and the PySide6 GUI + tray (`app`, `main`). GUI runs serial work in per-device threads; background detection polls COM ports every ~1.5 s.

**Tech Stack:** Python 3.14, PySide6, pySerial 3.5, PyInstaller 6.21, pytest.

## Global Constraints

- Device identification: only COM ports whose PNP `VID`/`PID` are `0483`/`5740` (parse from `pyserial` port `hwid`); device key = USB serial (e.g. `206C36943831`).
- Firmware protocol (115200): `SETTIME <unix> [tz]` overwrites the stored tz offset; `LIST` prints `  <name> <size>` lines; `DL2 <file>` prints `DLSTART <size>` then binary data, host sends one ACK byte (e.g. `G`) whenever no data has arrived for ~60 ms.
- Downloads go to `<sync_folder>/<serial>/`, written as `<name>.part` then renamed on success.
- State files at `%APPDATA%/DayVault/state/<serial>.json` (`{name: size}`); config at `%APPDATA%/DayVault/config.json`; logs at `%APPDATA%/DayVault/logs/app.log`.
- Do not modify firmware. This is a host tool only.
- Every command/feature must be documented (tool README).

---

## File Structure

- `tools/dayvault_sync/dayvault/__init__.py` — package marker.
- `tools/dayvault_sync/dayvault/proto.py` — `parse_list_output`, `parse_rec_name`, `local_tz_offset_minutes`.
- `tools/dayvault_sync/dayvault/dio.py` — `list_dayvault_ports`, `DeviceConnection` (open/close/command/DL2).
- `tools/dayvault_sync/dayvault/store.py` — config + per-device state + logging.
- `tools/dayvault_sync/dayvault/engine.py` — `sync_device(...)` plain orchestration.
- `tools/dayvault_sync/dayvault/app.py` — `MainWindow`, `Tray`, `DeviceMonitor` (detection timer), thread wiring.
- `tools/dayvault_sync/main.py` — entry point.
- `tools/dayvault_sync/tests/test_proto.py`, `test_store.py`, `test_dio.py`.
- `tools/dayvault_sync/requirements.txt`, `build_exe.bat`, `README.md`.

---

### Task 1: Package skeleton + protocol/parsing module (`proto.py`)

**Files:**
- Create: `tools/dayvault_sync/dayvault/__init__.py`, `tools/dayvault_sync/dayvault/proto.py`, `tools/dayvault_sync/tests/test_proto.py`

**Interfaces:**
- Consumes: nothing (pure functions).
- Produces:
  - `parse_list_output(text: str) -> list[tuple[str, int]]` — `(name, size)` for each `  <name> <size>` line.
  - `parse_rec_name(name: str) -> dict | None` — `{"kind": "timestamp"|"seq", "base": "REC-20260809-1110"|"REC062", "seq": int|None, "duration_secs": int|None}`; `None` for non-REC files. Supports `REC-YYYYMMDD-HHMM`, `_n` collision suffix, `_MmSs`/`_HhMmSs` duration, and legacy `REC###`.
  - `local_tz_offset_minutes(now: datetime | None = None) -> int` — UTC offset in minutes (e.g. +480 for UTC+8).

- [ ] **Step 1: Create the package and `proto.py`**

Create the directory structure and files. `proto.py`:

```python
"""Parsing for the DayVault serial protocol and recording names."""
from __future__ import annotations
import re
from datetime import datetime, timedelta, timezone

_TIMESTAMP_RE = re.compile(
    r"^REC-(\d{4})(\d{2})(\d{2})-(\d{2})(\d{2})(?:_(\d+))?"
    r"(?:_(?:(\d+)h)?(\d+)m(\d+)s)?\.WAV$"
)
_SEQ_RE = re.compile(r"^REC(\d{3})\.WAV$")


def parse_list_output(text: str) -> list[tuple[str, int]]:
    """Parse LIST output: each line '  <name> <size>' -> [(name, size)]."""
    out: list[tuple[str, int]] = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("LIST"):
            continue
        parts = line.rsplit(" ", 1)
        if len(parts) != 2:
            continue
        name, size = parts[0], parts[1]
        try:
            out.append((name, int(size)))
        except ValueError:
            continue
    return out


def parse_rec_name(name: str) -> dict | None:
    """Parse a recording filename into metadata, or None if not a REC file."""
    m = _TIMESTAMP_RE.match(name)
    if m:
        y, mo, d, h, mi = (int(g) for g in m.groups()[:5])
        dur_h = int(m.group(7)) if m.group(7) else 0
        dur_m = int(m.group(8)) if m.group(8) else 0
        dur_s = int(m.group(9)) if m.group(9) else 0
        return {
            "kind": "timestamp",
            "timestamp": f"{y:04d}-{mo:02d}-{d:02d} {h:02d}:{mi:02d}",
            "collision": int(m.group(6)) if m.group(6) else 0,
            "duration_secs": dur_h * 3600 + dur_m * 60 + dur_s if (m.group(7) or m.group(8) or m.group(9)) else None,
        }
    m = _SEQ_RE.match(name)
    if m:
        return {"kind": "seq", "seq": int(m.group(1)), "duration_secs": None}
    return None


def local_tz_offset_minutes(now: datetime | None = None) -> int:
    """Local UTC offset in minutes (e.g. +480 for UTC+8)."""
    now = now or datetime.now().astimezone()
    off = now.utcoffset() or timedelta(0)
    return int(off.total_seconds() // 60)
```

- [ ] **Step 2: Write `tests/test_proto.py`**

```python
from dayvault.proto import parse_list_output, parse_rec_name, local_tz_offset_minutes
from datetime import datetime, timedelta, timezone


def test_parse_list_output():
    text = "LIST mount=0\n  REC062.WAV 158476\n  REC-20260809-1925_0m05s.WAV 242880\nLIST done\n"
    assert parse_list_output(text) == [
        ("REC062.WAV", 158476),
        ("REC-20260809-1925_0m05s.WAV", 242880),
    ]


def test_parse_rec_name_timestamp_duration():
    r = parse_rec_name("REC-20260809-1925_1h23m45s.WAV")
    assert r["kind"] == "timestamp"
    assert r["timestamp"] == "2026-08-09 19:25"
    assert r["duration_secs"] == 1 * 3600 + 23 * 60 + 45
    assert r["collision"] == 0


def test_parse_rec_name_timestamp_collision_no_duration():
    r = parse_rec_name("REC-20260809-1110_2.WAV")
    assert r["kind"] == "timestamp"
    assert r["collision"] == 2
    assert r["duration_secs"] is None


def test_parse_rec_name_seq():
    r = parse_rec_name("REC062.WAV")
    assert r["kind"] == "seq"
    assert r["seq"] == 62


def test_parse_rec_name_non_rec():
    assert parse_rec_name("TEST1.TXT") is None


def test_local_tz_offset_minutes_utc8():
    now = datetime(2026, 8, 10, 12, 0, tzinfo=timezone(timedelta(hours=8)))
    assert local_tz_offset_minutes(now) == 480
```

- [ ] **Step 3: Run the tests**

```powershell
cd tools/dayvault_sync
python -m pytest tests/test_proto.py -v
```

Expected: all pass. (pytest must be installed; if not: `pip install pytest`.)

- [ ] **Step 4: Commit**

```bash
git add tools/dayvault_sync
git commit -m "feat(host): dayvault sync tool package skeleton + protocol/parsing module with tests"
```

---

### Task 2: Serial layer (`dio.py`) — port detection, connection, DL2 download

**Files:**
- Create: `tools/dayvault_sync/dayvault/dio.py`, `tools/dayvault_sync/tests/test_dio.py`

**Interfaces:**
- Consumes: pySerial.
- Produces:
  - `list_dayvault_ports() -> list[tuple[str, str]]` — `(port_name, serial)` for COM ports whose `hwid` contains `VID:PID=0483:5740`; serial parsed from `SER=<serial>` (fall back to `serial_number` attribute).
  - `class DeviceConnection`: `open()`, `close()`, `readline(timeout_s)` -> str, `send_command(cmd) -> str` (write `cmd\r\n`, read the response line), `download_dl2(name, dest_path, progress_cb=None, ack_byte=b"G", idle_ms=60) -> int` (returns bytes written; raises on failure). Implements the DL2 chunked-ACK protocol; writes to `dest_path` directly (caller passes the `.part` path).

- [ ] **Step 1: Write `dio.py`**

```python
"""Serial device detection and the DL2 chunked-ACK download protocol."""
from __future__ import annotations
import re
import time
import serial
import serial.tools.list_ports as list_ports

TARGET_VID = 0x0483
TARGET_PID = 0x5740
_VIDPID_RE = re.compile(r"VID:PID=([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})")
_SER_RE = re.compile(r"SER=(\S+)")


def list_dayvault_ports() -> list[tuple[str, str]]:
    """Return [(port, serial)] for DayVault boards (VID 0483 / PID 5740)."""
    out: list[tuple[str, str]] = []
    for p in list_ports.comports():
        hw = p.hwid or ""
        m = _VIDPID_RE.search(hw)
        if not m:
            continue
        if int(m.group(1), 16) != TARGET_VID or int(m.group(2), 16) != TARGET_PID:
            continue
        ser = _SER_RE.search(hw)
        serial_no = ser.group(1) if ser else (p.serial_number or "unknown")
        out.append((p.device, serial_no))
    return out


class DeviceConnection:
    def __init__(self, port: str, baud: int = 115200, timeout: float = 2.0):
        self._port = port
        self._ser = serial.Serial(port, baud, timeout=timeout, write_timeout=2.0)

    def close(self):
        try:
            self._ser.close()
        except Exception:
            pass

    def readline(self, timeout_s: float = 5.0) -> str:
        """Read one line; raises TimeoutError on timeout."""
        deadline = time.time() + timeout_s
        buf = b""
        while time.time() < deadline:
            chunk = self._ser.readline()
            if chunk:
                buf += chunk
                if buf.endswith(b"\n"):
                    return buf.decode("utf-8", errors="replace").rstrip("\r\n")
            else:
                if buf:
                    return buf.decode("utf-8", errors="replace").rstrip("\r\n")
                time.sleep(0.02)
        raise TimeoutError(f"no line from {self._port} in {timeout_s}s")

    def send_command(self, cmd: str, timeout_s: float = 10.0) -> str:
        """Send a command and return the first non-empty response line."""
        self._ser.reset_input_buffer()
        self._ser.write((cmd + "\r\n").encode())
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            line = self.readline(timeout_s=min(5.0, timeout_s))
            if line:
                return line
        raise TimeoutError(f"no response to '{cmd}'")

    def download_dl2(self, name: str, dest_path: str,
                     progress_cb=None, ack_byte: bytes = b"G", idle_ms: int = 60) -> int:
        """DL2 chunked-ACK download of <name> to <dest_path>. Returns bytes written."""
        self._ser.reset_input_buffer()
        self._ser.write(f"DL2 {name}\r\n".encode())

        # find "DLSTART <size>" line
        buf = b""
        deadline = time.time() + 15.0
        size = -1
        while time.time() < deadline:
            chunk = self._ser.read(4096)
            if chunk:
                buf += chunk
                idx = buf.find(b"DLSTART ")
                if idx >= 0:
                    nl = buf.find(b"\n", idx)
                    if nl >= 0:
                        try:
                            size = int(buf[idx + 8:nl].strip())
                        except ValueError:
                            raise IOError("bad DLSTART size")
                        buf = buf[nl + 1:]
                        break
            else:
                time.sleep(0.02)
        if size < 0:
            raise IOError(f"no DLSTART for {name}")

        total = 0
        with open(dest_path, "wb") as f:
            if buf:
                take = min(len(buf), size)
                f.write(buf[:take])
                total += take
                if take < len(buf):
                    buf = buf[take:]  # trailing bytes beyond size: leave on stream for next reads
                    if len(buf) > 0:
                        # push back is impossible; extra bytes beyond size are rare -> drop them
                        pass
            no_data = 0
            deadline = time.time() + 300.0
            while total < size and time.time() < deadline:
                remain = size - total
                n = self._ser.in_waiting
                if n > 0:
                    n = min(n, remain)
                    chunk = self._ser.read(n)
                    if chunk:
                        f.write(chunk)
                        total += len(chunk)
                        no_data = 0
                        if progress_cb:
                            progress_cb(total, size)
                else:
                    no_data += 1
                    if no_data >= max(1, int(idle_ms / 20)):
                        self._ser.write(ack_byte)
                        no_data = 0
                    time.sleep(0.02)
        if total != size:
            raise IOError(f"incomplete download {name}: {total}/{size}")
        self._ser.write(ack_byte)
        return total
```

Note: `in_waiting` may not exist on all serial objects; it does on the Windows pyserial backend. Keep the `serial.Serial` Windows path (target is Windows).

- [ ] **Step 2: Write `tests/test_dio.py`** (port filtering only; DL2 needs hardware)

```python
from dayvault.dio import list_dayvault_ports, _VIDPID_RE, _SER_RE


def test_vidpid_regex():
    hw = "USB VID:PID=0483:5740 SER=206C36943831"
    m = _VIDPID_RE.search(hw)
    assert m and int(m.group(1), 16) == 0x0483 and int(m.group(2), 16) == 0x5740
    assert _SER_RE.search(hw).group(1) == "206C36943831"


def test_other_vidpid_rejected():
    assert not _VIDPID_RE.search("USB VID:PID=1D6B:0002").group(2) == "5740"
```

(Full `list_dayvault_ports` behavior is verified on hardware.)

- [ ] **Step 3: Run the tests**

```powershell
cd tools/dayvault_sync
python -m pytest tests/test_dio.py -v
```

- [ ] **Step 4: Hardware smoke test**

With the device plugged on COM9, from `tools/dayvault_sync`:

```python
from dayvault.dio import list_dayvault_ports, DeviceConnection
print(list_dayvault_ports())          # expect [('COM9', '206C36943831')]
c = DeviceConnection('COM9')
print(c.send_command('INFO'))         # expect INFO usb_detect=... free=... 
c.download_dl2('REC062.WAV', r'C:\Users\Administrator\AppData\Local\Temp\opencode\rec062_test.wav')
print('downloaded ok')
c.close()
```

Verify the downloaded file size equals the LIST size for that file.

- [ ] **Step 5: Commit**

```bash
git add tools/dayvault_sync
git commit -m "feat(host): serial layer - DayVault port detection + DeviceConnection + DL2 chunked-ACK download"
```

---

### Task 3: State & config store (`store.py`)

**Files:**
- Create: `tools/dayvault_sync/dayvault/store.py`, `tools/dayvault_sync/tests/test_store.py`

**Interfaces:**
- Consumes: `os`, `json`, `logging`.
- Produces:
  - `app_data_dir() -> Path` — `%APPDATA%/DayVault`.
  - `load_config() -> dict` / `save_config(cfg: dict)` — `{sync_folder, poll_interval_ms, autostart}`; defaults `sync_folder = <home>/Documents/DayVault`, `poll_interval_ms = 1500`, `autostart = False`.
  - `load_state(serial: str) -> dict[str, int]` / `save_state(serial: str, files: dict[str, int])` — `state/<serial>.json`.
  - `setup_logging() -> None` — rotating file handler at `logs/app.log`.

- [ ] **Step 1: Write `store.py`**

```python
"""Config, per-device sync state, and logging under %APPDATA%/DayVault."""
from __future__ import annotations
import json
import logging
import os
from logging.handlers import RotatingFileHandler
from pathlib import Path


def app_data_dir() -> Path:
    base = os.environ.get("APPDATA") or str(Path.home())
    d = Path(base) / "DayVault"
    d.mkdir(parents=True, exist_ok=True)
    return d


def _cfg_path() -> Path:
    return app_data_dir() / "config.json"


def load_config() -> dict:
    default = {
        "sync_folder": str(Path.home() / "Documents" / "DayVault"),
        "poll_interval_ms": 1500,
        "autostart": False,
    }
    p = _cfg_path()
    if p.exists():
        try:
            cfg = json.loads(p.read_text(encoding="utf-8"))
            default.update(cfg)
        except Exception:
            pass
    return default


def save_config(cfg: dict) -> None:
    _cfg_path().write_text(json.dumps(cfg, ensure_ascii=False, indent=2), encoding="utf-8")


def _state_path(serial: str) -> Path:
    d = app_data_dir() / "state"
    d.mkdir(parents=True, exist_ok=True)
    return d / f"{serial}.json"


def load_state(serial: str) -> dict[str, int]:
    p = _state_path(serial)
    if p.exists():
        try:
            return json.loads(p.read_text(encoding="utf-8"))
        except Exception:
            return {}
    return {}


def save_state(serial: str, files: dict[str, int]) -> None:
    _state_path(serial).write_text(json.dumps(files, ensure_ascii=False, indent=2), encoding="utf-8")


def setup_logging() -> None:
    d = app_data_dir() / "logs"
    d.mkdir(parents=True, exist_ok=True)
    handler = RotatingFileHandler(d / "app.log", maxBytes=512 * 1024, backupCount=2, encoding="utf-8")
    handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s"))
    root = logging.getLogger()
    root.setLevel(logging.INFO)
    if not any(isinstance(h, RotatingFileHandler) for h in root.handlers):
        root.addHandler(handler)
```

- [ ] **Step 2: Write `tests/test_store.py`** (use a temp APPDATA via monkeypatch)

```python
import json
from pathlib import Path
from dayvault import store


def test_config_roundtrip(tmp_path, monkeypatch):
    monkeypatch.setenv("APPDATA", str(tmp_path))
    cfg = store.load_config()
    assert "sync_folder" in cfg
    cfg["sync_folder"] = str(tmp_path / "sync")
    store.save_config(cfg)
    assert store.load_config()["sync_folder"] == str(tmp_path / "sync")


def test_state_roundtrip(tmp_path, monkeypatch):
    monkeypatch.setenv("APPDATA", str(tmp_path))
    store.save_state("SERIAL123", {"REC-20260809-1110.WAV": 189332})
    assert store.load_state("SERIAL123") == {"REC-20260809-1110.WAV": 189332}
    assert store.load_state("OTHER") == {}
```

- [ ] **Step 3: Run the tests**

```powershell
cd tools/dayvault_sync
python -m pytest tests/test_store.py -v
```

- [ ] **Step 4: Commit**

```bash
git add tools/dayvault_sync
git commit -m "feat(host): config, per-device sync state JSON, and rotating log store"
```

---

### Task 4: Sync engine (`engine.py`)

**Files:**
- Create: `tools/dayvault_sync/dayvault/engine.py`, `tools/dayvault_sync/tests/test_engine.py`

**Interfaces:**
- Consumes: `proto`, `dio.DeviceConnection`, `store`.
- Produces:
  - `sync_device(conn, serial, config, progress_cb=None, log=None) -> dict` — performs the full flow and returns `{"synced": int, "downloaded": [names], "failed": [names]}`.
  - `plan_downloads(remote: list[tuple[str, int]], state: dict[str, int]) -> list[str]` — names to download (not in state or size changed).

- [ ] **Step 1: Write `engine.py`**

```python
"""Per-device sync orchestration (clock sync, list, diff, download)."""
from __future__ import annotations
import logging
import time
from datetime import datetime
from pathlib import Path

from . import proto, store


def plan_downloads(remote: list[tuple[str, int]], state: dict[str, int]) -> list[str]:
    """Remote files not in state (or with a changed size) that need downloading."""
    to_get = []
    for name, size in remote:
        if state.get(name) != size:
            to_get.append(name)
    return to_get


def sync_device(conn, serial: str, config: dict,
                progress_cb=None, log: logging.Logger | None = None) -> dict:
    """Sync one device: set clock, list files, download unsynced, update state."""
    log = log or logging.getLogger("dayvault")
    result = {"synced": 0, "downloaded": [], "failed": []}

    # 1. clock + timezone (every sync overwrites the offset, per design)
    now = int(time.time())
    tz = proto.local_tz_offset_minutes()
    conn.send_command(f"SETTIME {now} {tz}")
    result["synced"] = 1

    # 2. list
    lines = conn.send_command("LIST")
    remote = proto.parse_list_output(lines)
    # LIST prints many lines; drain the rest
    try:
        while True:
            extra = conn.readline(timeout_s=1.5)
            if not extra or "LIST done" in extra:
                break
            remote.extend(proto.parse_list_output(extra))
    except TimeoutError:
        pass

    # 3. diff against state
    state = store.load_state(serial)
    to_get = plan_downloads(remote, state)

    # 4. download each to <sync_folder>/<serial>/
    folder = Path(config["sync_folder"]) / serial
    folder.mkdir(parents=True, exist_ok=True)
    for name in to_get:
        size = dict(remote).get(name, 0)
        part = folder / (name + ".part")
        final = folder / name
        try:
            n = conn.download_dl2(name, str(part), progress_cb=progress_cb)
            if n != size:
                raise IOError(f"size mismatch {n} != {size}")
            part.replace(final)
            state[name] = size
            store.save_state(serial, state)
            result["downloaded"].append(name)
        except Exception as e:
            log.error("download %s failed: %s", name, e)
            result["failed"].append(name)
            try:
                part.unlink(missing_ok=True)
            except Exception:
                pass
    return result
```

- [ ] **Step 2: Write `tests/test_engine.py`** (pure logic; integration on hardware)

```python
from dayvault.engine import plan_downloads


def test_plan_downloads_new_and_changed():
    remote = [("A.WAV", 100), ("B.WAV", 200), ("C.WAV", 300)]
    state = {"A.WAV": 100, "B.WAV": 999}
    assert plan_downloads(remote, state) == ["B.WAV", "C.WAV"]


def test_plan_downloads_empty():
    remote = [("A.WAV", 100)]
    state = {"A.WAV": 100}
    assert plan_downloads(remote, state) == []
```

- [ ] **Step 3: Run the tests**

```powershell
cd tools/dayvault_sync
python -m pytest tests/test_engine.py -v
```

- [ ] **Step 4: Integration on hardware** (with the device on COM9)

```python
from dayvault.dio import DeviceConnection, list_dayvault_ports
from dayvault.engine import sync_device
from dayvault import store

cfg = store.load_config()
cfg["sync_folder"] = r"C:\Users\Administrator\AppData\Local\Temp\opencode\dvsync_test"
ports = list_dayvault_ports()
assert ports, "no DayVault device"
port, serial = ports[0]
conn = DeviceConnection(port)
res = sync_device(conn, serial, cfg, log=__import__("logging").getLogger("t"))
conn.close()
print(res)   # synced=1, downloaded=[...], failed=[]
```

Verify: files land under the sync folder's `<serial>/` subfolder, `state/<serial>.json` updated, clock synced (`INFO` shows the right local time).

- [ ] **Step 5: Commit**

```bash
git add tools/dayvault_sync
git commit -m "feat(host): sync engine - clock sync, list, diff, DL2 download, state update"
```

---

### Task 5: GUI + tray (`app.py`, `main.py`)

**Files:**
- Create: `tools/dayvault_sync/dayvault/app.py`, `tools/dayvault_sync/main.py`

**Interfaces:**
- Consumes: PySide6 (`pip install PySide6` first), `dio`, `engine`, `store`, `proto`.
- Produces:
  - `main()` in `main.py`: `QApplication` + `MainWindow` + `DeviceMonitor` + tray.
  - `class MainWindow(QMainWindow)`: device combo, `QTableWidget` file list (name/size/time/status), sync-folder picker, tray integration (close → hide, tray click → show).
  - `class DeviceMonitor(QObject)`: QTimer polling `list_dayvault_ports`, diff, emits `device_added(port, serial)` / `device_removed(serial)`; on add, starts a `SyncThread`.
  - `class SyncThread(QThread)`: runs `sync_device`; signals `device_files(serial, files)`, `download_progress(serial, name, pct)`, `sync_finished(serial, result)`, `sync_error(serial, msg)`.

- [ ] **Step 1: Install PySide6**

```powershell
pip install PySide6
```

- [ ] **Step 2: Write `app.py`** — the window, monitor, tray, and thread wiring. Key behaviors:
  - On device add: start `SyncThread` (clock sync → list → diff → download), update the file table with "已下载/未下载" per file.
  - Close event: hide to tray instead of quitting (`event.ignore(); self.hide()`).
  - Tray icon: left-click → show window; menu "打开主界面" → show; "退出" → `QApplication.quit()`.
  - Settings: folder picker updates `config["sync_folder"]` and persists.
  - A "立即同步" button to re-run sync for the selected device.

- [ ] **Step 3: Write `main.py`**

```python
import sys
from PySide6.QtWidgets import QApplication
from dayvault.app import MainWindow, DeviceMonitor, TrayIcon


def main() -> int:
    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)   # stay resident when window closes
    win = MainWindow()
    mon = DeviceMonitor(win)
    win.monitor = mon
    tray = TrayIcon(win)
    win.tray = tray
    win.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the app**

```powershell
cd tools/dayvault_sync
python main.py
```

Manually verify (device plugged in):
1. On launch, the board is detected and the file list populates; new files auto-download to the sync folder.
2. Window shows 已下载/未下载 status.
3. Close window → app stays in tray; background detection continues.
4. Tray click reopens the window.
5. Tray → 退出 quits.
6. `INFO` on the board shows the synced local time (clock was set).

- [ ] **Step 5: Commit**

```bash
git add tools/dayvault_sync
git commit -m "feat(host): PySide6 main window, device monitor, tray residence, sync thread wiring"
```

---

### Task 6: Packaging + docs

**Files:**
- Create: `tools/dayvault_sync/requirements.txt`, `tools/dayvault_sync/build_exe.bat`, `tools/dayvault_sync/README.md`

**Interfaces:** none (packaging + docs).

- [ ] **Step 1: `requirements.txt`**

```
PySide6
pyserial
```

- [ ] **Step 2: `build_exe.bat`** (run from `tools/dayvault_sync`)

```bat
@echo off
pip install -r requirements.txt
pyinstaller --noconfirm --onefile --windowed --name DayVaultSync ^
  --hidden-import PySide6.QtWidgets ^
  main.py
echo EXE: dist\DayVaultSync.exe
```

- [ ] **Step 3: Run the build and smoke-test the EXE**

```powershell
cd tools/dayvault_sync
build_exe.bat
```

Verify `dist/DayVaultSync.exe` launches, shows the tray icon, detects the device, and the window opens. If PySide6 packaging needs extra hidden imports, add them and rebuild.

- [ ] **Step 4: `README.md`** — what the tool does, install/build/run, config/state locations, sync behavior, tray usage, troubleshooting (serial port busy, device unrecognized).

- [ ] **Step 5: Commit**

```bash
git add tools/dayvault_sync
git commit -m "feat(host): package tool as single EXE (PyInstaller), requirements, README"
```

---

## Self-Review

- Spec §1 (architecture/stack) → Task 5 (+ Task 2 serial layer). §2 (auto flow) → Task 4 + Task 5 monitor wiring. §3 (window/tray/settings) → Task 5. §4 (storage) → Task 3. §5 (edge cases) → Task 4 (failures) + Task 5 (UI). §6 (testing/packaging) → Tasks 1-4 unit tests + Task 5 integration + Task 6 EXE.
- No placeholders: every code step has full code; every verify step has commands.
- Type consistency: `list_dayvault_ports`/`DeviceConnection` (Task 2) used by Task 4/5; `parse_list_output`/`parse_rec_name`/`local_tz_offset_minutes` (Task 1) used by Task 4; `load_config`/`load_state`/`save_state` (Task 3) used by Task 4/5; `sync_device` (Task 4) used by Task 5.
- Environment dependencies: PySide6 must be installed (Task 5 Step 1); pytest for tests.
