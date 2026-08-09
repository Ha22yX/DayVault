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
                     progress_cb=None, ack_byte: bytes = b"G", idle_ms: int = 60,
                     interrupt=None) -> int:
        """DL2 chunked-ACK download of <name> to <dest_path>. Returns bytes written.

        `interrupt` may be a zero-arg callable; when it returns truthy the
        download raises InterruptedError immediately (checked in the DLSTART
        wait loop and the data/ACK loop).
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
            chunk = self._ser.read(4096)
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
                if interrupt and interrupt():
                    raise InterruptedError("download interrupted")
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
