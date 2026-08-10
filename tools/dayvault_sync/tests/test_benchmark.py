import pytest

from dayvault.benchmark import BenchmarkResult, parse_benchmark_line


def test_parse_benchmark_line():
    result = parse_benchmark_line(
        "BENCH usb bytes=2097152 ms=2048 kib_s=1000 crc32=9A7C2D11"
    )
    assert result == BenchmarkResult(
        layer="usb",
        byte_count=2_097_152,
        duration_ms=2_048,
        kib_per_second=1_000,
        crc32=0x9A7C2D11,
    )


def test_parse_benchmark_accepts_known_layers():
    for layer in ("usb", "bulk", "sd", "e2e"):
        result = parse_benchmark_line(
            f"BENCH {layer} bytes=1024 ms=1 kib_s=1000 crc32=00000000"
        )
        assert result.layer == layer


@pytest.mark.parametrize(
    "line",
    [
        "SPEED bytes=1024 ms=1",
        "BENCH network bytes=1024 ms=1 kib_s=1000 crc32=00000000",
        "BENCH usb bytes=x ms=1 kib_s=1000 crc32=00000000",
        "BENCH usb bytes=1024 ms=0 kib_s=1000 crc32=00000000",
        "BENCH usb bytes=1024 ms=1 kib_s=1000",
        "BENCH usb bytes=1024 ms=1 kib_s=1000 crc32=XYZ",
    ],
)
def test_parse_benchmark_rejects_malformed_lines(line):
    with pytest.raises(ValueError):
        parse_benchmark_line(line)
