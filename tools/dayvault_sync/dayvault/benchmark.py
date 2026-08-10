"""Parsing for machine-readable firmware benchmark results."""
from __future__ import annotations

from dataclasses import dataclass
import re


_BENCHMARK_RE = re.compile(
    r"^BENCH (usb|bulk|sd|e2e) "
    r"bytes=(\d+) ms=([1-9]\d*) kib_s=(\d+) crc32=([0-9A-Fa-f]{8})$"
)


@dataclass(frozen=True)
class BenchmarkResult:
    layer: str
    byte_count: int
    duration_ms: int
    kib_per_second: int
    crc32: int


def parse_benchmark_line(line: str) -> BenchmarkResult:
    match = _BENCHMARK_RE.fullmatch(line.strip())
    if match is None:
        raise ValueError(f"malformed benchmark result: {line!r}")
    return BenchmarkResult(
        layer=match.group(1),
        byte_count=int(match.group(2)),
        duration_ms=int(match.group(3)),
        kib_per_second=int(match.group(4)),
        crc32=int(match.group(5), 16),
    )
