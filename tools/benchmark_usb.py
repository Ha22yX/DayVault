"""Run a byte-accurate SPEED benchmark against a connected DayVault."""

from __future__ import annotations

import argparse
import time
import zlib

import serial


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM9")
    parser.add_argument("--timeout", type=float, default=2.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    with serial.Serial(args.port, 115200, timeout=args.timeout) as device:
        try:
            device.set_buffer_size(rx_size=1024 * 1024, tx_size=64 * 1024)
        except (AttributeError, OSError, serial.SerialException):
            pass
        device.dtr = True
        time.sleep(0.25)
        device.reset_input_buffer()
        device.write(b"SPEED\r\n")
        device.flush()

        prefix = device.read_until(b"SPEEDSTART ")
        if not prefix.endswith(b"SPEEDSTART "):
            print(f"ERROR missing SPEEDSTART: {prefix!r}")
            return 2

        expected = int(device.readline().strip())
        payload = bytearray()
        transfer_error: Exception | None = None
        started = time.perf_counter()
        try:
            while len(payload) < expected:
                chunk = device.read(min(64 * 1024, expected - len(payload)))
                if not chunk:
                    break
                payload.extend(chunk)
        except Exception as exc:  # Preserve partial-transfer evidence.
            transfer_error = exc
        elapsed = time.perf_counter() - started

        crc32 = zlib.crc32(payload) & 0xFFFFFFFF
        rate = len(payload) / 1024 / elapsed if elapsed else 0.0
        print(
            f"expected={expected} received={len(payload)} elapsed_s={elapsed:.3f} "
            f"host_kib_s={rate:.1f} crc32={crc32:08X}"
        )
        if transfer_error is not None:
            print(f"ERROR transfer: {transfer_error!r}")
            return 3
        if len(payload) != expected:
            print("ERROR short payload")
            return 4

        end_line = device.readline().decode("ascii", errors="replace").strip()
        bench_line = device.readline().decode("ascii", errors="replace").strip()
        print(end_line)
        print(bench_line)
        return 0 if end_line.startswith("SPEEDEND ") else 5


if __name__ == "__main__":
    raise SystemExit(main())
