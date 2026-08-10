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
                     progress_cb=None, ack_byte: bytes = b"G", idle_ms: int = 5,
                     interrupt=None) -> int:
        """Streaming DL2 download of <name> to <dest_path>. Returns bytes written.

        The firmware streams continuously; the host just reads. `interrupt` may be
        a zero-arg callable; when it returns truthy the download raises
        InterruptedError immediately (checked in the DLSTART wait and read loops).
        """
        self._ser.reset_input_buffer()
        self._ser.write(f"DL2 {name}\r\n".encode())

        # find "DLSTART <size>" line
        buf = b""
        deadline = time.time() + 15.0
        size = -1
        while time.time() < deadline:
            if interrupt and interrupt():
                raise InterruptedError("download interrupted")
            chunk = self._ser.read(8192)
            if chunk:
                buf += chunk
                if b"FAIL" in buf:
                    raise IOError(f"DL2 rejected for {name}")
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
                time.sleep(0.01)
        if size < 0:
            raise IOError(f"no DLSTART for {name}")

        total = 0
        prev_to = self._ser.timeout
        self._ser.timeout = 0.05          # short timeout for fast stalls
        with open(dest_path, "wb") as f:
            if buf:
                take = min(len(buf), size)
                f.write(buf[:take])
                total += take
                if take < len(buf):
                    # trailing bytes beyond size are rare -> drop them
                    pass
            stall = 0
            deadline = time.time() + 600.0
            while total < size and time.time() < deadline:
                if interrupt and interrupt():
                    self._ser.timeout = prev_to
                    raise InterruptedError("download interrupted")
                remain = size - total
                n = min(32768, remain)
                chunk = self._ser.read(n)          # blocking read, large chunk
                if chunk:
                    f.write(chunk)
                    total += len(chunk)
                    stall = 0
                    if progress_cb:
                        progress_cb(total, size)
                else:
                    stall += 1
                    if stall > 100:                # ~5 s with no data -> nudge
                        self._ser.write(ack_byte)
                        stall = 0
        self._ser.timeout = prev_to
        if total != size:
            raise IOError(f"incomplete download {name}: {total}/{size}")
        return total
