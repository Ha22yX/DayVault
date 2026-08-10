"""Serial device detection and high-throughput resumable file export."""
from __future__ import annotations
import re
import time
from pathlib import Path
import zlib
import serial
import serial.tools.list_ports as list_ports

from .benchmark import parse_benchmark_line
from .export_protocol import parse_get2_end, parse_get2_start
from .winusb import WinUsbTransport

TARGET_VID = 0x0483
TARGET_PID = 0x5740
_VIDPID_RE = re.compile(r"VID:PID=([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})")
_SER_RE = re.compile(r"SER=(\S+)")


class Get2Unsupported(IOError):
    """The connected firmware does not implement the GET2 protocol."""


class Bulk2Unsupported(IOError):
    """The connected firmware does not expose the WinUSB BULK2 protocol."""


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
        self._baud = baud
        self._timeout = timeout
        self._serial_number = next(
            (
                serial_number
                for candidate, serial_number in list_dayvault_ports()
                if candidate.casefold() == port.casefold()
            ),
            None,
        )
        self._ser = self._open_serial(port)

    def _open_serial(self, port: str):
        device = serial.Serial(
            port, self._baud, timeout=self._timeout, write_timeout=2.0
        )
        self._configure_serial_buffers(device)
        return device

    @staticmethod
    def _configure_serial_buffers(device) -> None:
        set_buffer_size = getattr(device, "set_buffer_size", None)
        if set_buffer_size is not None:
            try:
                set_buffer_size(rx_size=1024 * 1024, tx_size=64 * 1024)
            except (OSError, serial.SerialException):
                pass

    def close(self):
        try:
            if self._ser is not None:
                self._ser.close()
        except Exception:
            pass

    def _reopen_serial(self, timeout_s: float = 20.0) -> None:
        deadline = time.monotonic() + timeout_s
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            candidates = list_dayvault_ports()
            if self._serial_number:
                candidates = [
                    item for item in candidates if item[1] == self._serial_number
                ]
            elif not candidates:
                candidates = [(self._port, "unknown")]
            for port, _ in candidates:
                try:
                    self._ser = self._open_serial(port)
                    self._port = port
                    return
                except (OSError, serial.SerialException) as error:
                    last_error = error
            time.sleep(0.1)
        raise TimeoutError(
            f"DayVault CDC serial did not return after WinUSB transfer: {last_error}"
        )

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

    def download_get2(self, name: str, dest_path: str, expected_size: int | None = None,
                      progress_cb=None, interrupt=None) -> int:
        """Resume and verify a recording with the GET2 streaming protocol.

        The existing destination length is used as the requested offset. A CRC
        failure removes only the newly appended range; transport interruption
        keeps received bytes so the next connection can continue from there.
        Returns the complete partial-file size after a verified transfer.
        """
        path = Path(dest_path)
        path.parent.mkdir(parents=True, exist_ok=True)
        offset = path.stat().st_size if path.exists() else 0
        if expected_size is not None and offset > expected_size:
            with path.open("wb"):
                pass
            offset = 0

        self._ser.reset_input_buffer()
        self._ser.write(f"GET2 {offset} {name}\r\n".encode())

        deadline = time.monotonic() + 15.0
        start = None
        while time.monotonic() < deadline:
            if interrupt and interrupt():
                raise InterruptedError("download interrupted")
            try:
                line = self.readline(timeout_s=min(2.0, deadline - time.monotonic()))
            except TimeoutError:
                continue
            if line.startswith("GET2START "):
                start = parse_get2_start(line)
                break
            if line.startswith("GET2FAIL "):
                raise IOError(f"GET2 rejected for {name}: {line}")
            if line.startswith("? ") or line.startswith("DL2 "):
                raise Get2Unsupported(f"GET2 is not supported by {self._port}")
        if start is None:
            raise Get2Unsupported(f"no GET2START from {self._port}")
        if start.offset != offset:
            raise IOError(f"GET2 offset mismatch {start.offset} != {offset}")
        if expected_size is not None and start.total_size != expected_size:
            raise IOError(f"GET2 size mismatch {start.total_size} != {expected_size}")

        previous_timeout = self._ser.timeout
        self._ser.timeout = 0.1
        received = 0
        checksum = 0
        last_data = time.monotonic()
        last_progress = 0.0
        mode = "r+b" if offset else "wb"
        try:
            with path.open(mode, buffering=1024 * 1024) as output:
                output.seek(offset)
                while received < start.length:
                    if interrupt and interrupt():
                        raise InterruptedError("download interrupted")
                    wanted = min(256 * 1024, start.length - received)
                    chunk = self._ser.read(wanted)
                    if not chunk:
                        if time.monotonic() - last_data >= 15.0:
                            raise IOError(
                                f"GET2 stalled for {name}: {received}/{start.length}"
                            )
                        continue
                    output.write(chunk)
                    checksum = zlib.crc32(chunk, checksum)
                    received += len(chunk)
                    last_data = time.monotonic()
                    if progress_cb and (
                        last_data - last_progress >= 0.1 or received == start.length
                    ):
                        progress_cb(offset + received, start.total_size)
                        last_progress = last_data

            trailer_line = self.readline(timeout_s=10.0)
            trailer = parse_get2_end(trailer_line)
            if trailer.sent != received:
                self._truncate_to(path, offset)
                raise IOError(f"GET2 sent mismatch {trailer.sent} != {received}")
            if trailer.crc32 != checksum:
                self._truncate_to(path, offset)
                raise IOError(
                    f"GET2 CRC mismatch {trailer.crc32:08X} != {checksum:08X}"
                )
            if offset + received != start.total_size:
                self._truncate_to(path, offset)
                raise IOError(
                    f"GET2 incomplete file {offset + received}/{start.total_size}"
                )
            if progress_cb:
                progress_cb(start.total_size, start.total_size)
            return start.total_size
        finally:
            self._ser.timeout = previous_timeout

    def download_bulk2(self, name: str, dest_path: str,
                       expected_size: int | None = None,
                       progress_cb=None, interrupt=None) -> int:
        """Resume a recording through the dedicated CRC-verified WinUSB pipe."""
        if not self._serial_number:
            raise Bulk2Unsupported(f"USB serial number is unavailable for {self._port}")

        path = Path(dest_path)
        path.parent.mkdir(parents=True, exist_ok=True)
        offset = path.stat().st_size if path.exists() else 0
        if expected_size is not None and offset > expected_size:
            path.write_bytes(b"")
            offset = 0

        self._ser.reset_input_buffer()
        self._ser.write(f"BULK2 {offset} {name}\r\n".encode())
        self._ser.flush()
        try:
            response = self.readline(timeout_s=2.0)
        except (TimeoutError, OSError, serial.SerialException):
            response = ""
        if response.startswith("BULK2FAIL "):
            raise IOError(f"BULK2 rejected for {name}: {response}")
        if response.startswith("? "):
            raise Bulk2Unsupported(f"BULK2 is not supported by {self._port}")

        self.close()
        self._ser = None
        payload_complete = False
        received = 0
        try:
            try:
                transport = WinUsbTransport.wait_for_device(
                    self._serial_number,
                    deadline=time.monotonic() + 20.0,
                )
            except TimeoutError as error:
                raise Bulk2Unsupported(
                    f"DayVault WinUSB interface did not appear: {error}"
                ) from error

            with transport:
                start = parse_get2_start(
                    transport.readline(deadline=time.monotonic() + 10.0)
                )
                if start.offset != offset:
                    raise IOError(f"BULK2 offset mismatch {start.offset} != {offset}")
                if expected_size is not None and start.total_size != expected_size:
                    raise IOError(
                        f"BULK2 size mismatch {start.total_size} != {expected_size}"
                    )

                checksum = 0
                last_progress = 0.0
                mode = "r+b" if offset else "wb"
                with path.open(mode, buffering=1024 * 1024) as output:
                    output.seek(offset)
                    while received < start.length:
                        if interrupt and interrupt():
                            raise InterruptedError("download interrupted")
                        wanted = min(256 * 1024, start.length - received)
                        chunk = transport.read_exactly(
                            wanted, deadline=time.monotonic() + 15.0
                        )
                        output.write(chunk)
                        checksum = zlib.crc32(chunk, checksum)
                        received += len(chunk)
                        now = time.monotonic()
                        if progress_cb and (
                            now - last_progress >= 0.1 or received == start.length
                        ):
                            progress_cb(offset + received, start.total_size)
                            last_progress = now
                payload_complete = True

                trailer = parse_get2_end(
                    transport.readline(deadline=time.monotonic() + 10.0)
                )
                benchmark = parse_benchmark_line(
                    transport.readline(deadline=time.monotonic() + 10.0)
                )
                checksum &= 0xFFFFFFFF
                if trailer.sent != received:
                    raise IOError(f"BULK2 sent mismatch {trailer.sent} != {received}")
                if trailer.crc32 != checksum:
                    raise IOError(
                        f"BULK2 CRC mismatch {trailer.crc32:08X} != {checksum:08X}"
                    )
                if benchmark.layer != "bulk" or benchmark.byte_count != received:
                    raise IOError(f"BULK2 benchmark mismatch: {benchmark}")
                if benchmark.crc32 != checksum:
                    raise IOError(
                        f"BULK2 benchmark CRC mismatch "
                        f"{benchmark.crc32:08X} != {checksum:08X}"
                    )
                if offset + received != start.total_size:
                    raise IOError(
                        f"BULK2 incomplete file "
                        f"{offset + received}/{start.total_size}"
                    )
                transport.acknowledge()
                if progress_cb:
                    progress_cb(start.total_size, start.total_size)
                return start.total_size
        except Exception:
            if payload_complete:
                self._truncate_to(path, offset)
            raise
        finally:
            self._reopen_serial(timeout_s=20.0)

    @staticmethod
    def _truncate_to(path: Path, size: int) -> None:
        with path.open("r+b") as output:
            output.truncate(size)

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
