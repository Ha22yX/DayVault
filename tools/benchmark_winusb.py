"""Measure the dedicated DayVault WinUSB bulk-IN path on real hardware."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import time
import zlib

import serial
import serial.tools.list_ports


SYNC_ROOT = Path(__file__).resolve().parent / "dayvault_sync"
if str(SYNC_ROOT) not in sys.path:
    sys.path.insert(0, str(SYNC_ROOT))

from dayvault.benchmark import parse_benchmark_line  # noqa: E402
from dayvault.export_protocol import parse_get2_end, parse_get2_start  # noqa: E402
from dayvault.winusb import WinUsbTransport  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM9")
    parser.add_argument("--serial", help="USB serial number; auto-detected from --port")
    parser.add_argument("--timeout", type=float, default=25.0)
    return parser.parse_args()


def serial_number_for_port(port: str) -> str:
    for candidate in serial.tools.list_ports.comports():
        if candidate.device.casefold() == port.casefold() and candidate.serial_number:
            return candidate.serial_number
    raise RuntimeError(f"cannot determine USB serial number for {port}")


def wait_for_ready(device: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            line = device.readline().decode("ascii", errors="replace").strip()
        except (OSError, serial.SerialException):
            return "BULK2READY (CDC switched before status was drained)"
        if line.startswith("BULK2READY "):
            return line
        if line.startswith("BULK2FAIL "):
            raise RuntimeError(line)
    raise TimeoutError("firmware did not acknowledge BULKSPEED")


def main() -> int:
    args = parse_args()
    serial_number = args.serial or serial_number_for_port(args.port)

    with serial.Serial(args.port, 115200, timeout=0.25, write_timeout=2.0) as device:
        device.dtr = True
        time.sleep(0.25)
        device.reset_input_buffer()
        device.write(b"BULKSPEED\r\n")
        device.flush()
        ready = wait_for_ready(device, 5.0)
        print(ready)

    deadline = time.monotonic() + args.timeout
    transport = WinUsbTransport.wait_for_device(serial_number, deadline=deadline)
    with transport:
        start = parse_get2_start(transport.readline(deadline=deadline))
        if start.offset != 0 or start.length != start.total_size:
            raise RuntimeError(f"unexpected benchmark header: {start}")

        transfer_started = time.perf_counter()
        try:
            payload = transport.read_exactly(start.length, deadline=deadline)
        except Exception:
            print(
                f"received_before_error={len(transport._buffer)} "
                f"elapsed_s={time.perf_counter() - transfer_started:.3f}"
            )
            raise
        elapsed = time.perf_counter() - transfer_started
        end_line = transport.readline(deadline=deadline)
        benchmark_line = transport.readline(deadline=deadline)
        transport.acknowledge()

    checksum = zlib.crc32(payload) & 0xFFFFFFFF
    end = parse_get2_end(end_line)
    benchmark = parse_benchmark_line(benchmark_line)
    pattern = bytes(range(256))
    expected_crc = 0
    remaining = start.length
    while remaining:
        block_length = min(16 * 1024 - 192, remaining)
        block = pattern * (block_length // len(pattern))
        block += pattern[:block_length % len(pattern)]
        expected_crc = zlib.crc32(block, expected_crc)
        remaining -= block_length
    expected_crc &= 0xFFFFFFFF
    host_kib_s = start.length / 1024.0 / elapsed if elapsed else 0.0

    print(
        f"received={len(payload)} elapsed_s={elapsed:.3f} "
        f"host_kib_s={host_kib_s:.1f} crc32={checksum:08X}"
    )
    print(
        f"device_kib_s={benchmark.kib_per_second} "
        f"device_ms={benchmark.duration_ms}"
    )

    valid = (
        len(payload) == start.length
        and end.sent == len(payload)
        and end.crc32 == checksum
        and benchmark.layer == "bulk"
        and benchmark.byte_count == len(payload)
        and benchmark.crc32 == checksum
        and checksum == expected_crc
    )
    if not valid:
        print(
            f"ERROR validation failed: end={end} benchmark={benchmark} "
            f"expected_crc={expected_crc:08X}"
        )
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
