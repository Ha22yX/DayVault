from pathlib import Path
import zlib

import pytest

from dayvault.dio import DeviceConnection, list_dayvault_ports, _VIDPID_RE, _SER_RE


class FakeGet2Serial:
    def __init__(self, response_factory, max_chunk=7):
        self.timeout = 2.0
        self.response_factory = response_factory
        self.max_chunk = max_chunk
        self.pending = bytearray()
        self.writes = []
        self.raw_reads = 0

    def reset_input_buffer(self):
        self.pending.clear()

    def write(self, data):
        value = bytes(data)
        self.writes.append(value)
        if value.startswith(b"GET2 "):
            self.pending.extend(self.response_factory(value))
        return len(value)

    def read(self, size):
        self.raw_reads += 1
        count = min(size, self.max_chunk, len(self.pending))
        if count == 0:
            return b""
        value = bytes(self.pending[:count])
        del self.pending[:count]
        return value

    def readline(self):
        try:
            end = self.pending.index(ord("\n")) + 1
        except ValueError:
            return b""
        value = bytes(self.pending[:end])
        del self.pending[:end]
        return value


class FakeBulkSerial:
    def __init__(self, ready_line: bytes):
        self.timeout = 2.0
        self.pending = bytearray(ready_line)
        self.writes = []
        self.closed = False

    def reset_input_buffer(self):
        self.pending.clear()

    def write(self, data):
        self.writes.append(bytes(data))
        self.pending.extend(b"BULK2READY size=11 offset=4 length=7\n")
        return len(data)

    def flush(self):
        pass

    def readline(self):
        try:
            end = self.pending.index(ord("\n")) + 1
        except ValueError:
            return b""
        value = bytes(self.pending[:end])
        del self.pending[:end]
        return value

    def close(self):
        self.closed = True


class FakeBulkTransport:
    def __init__(self, payload: bytes, crc: int | None = None, *, offset: int = 0):
        checksum = zlib.crc32(payload) if crc is None else crc
        total_size = offset + len(payload)
        self.payload = bytearray(payload)
        self.lines = [
            f"GET2START size={total_size} offset={offset} length={len(payload)}",
            f"GET2END sent={len(payload)} crc32={checksum:08X}",
            (
                f"BENCH bulk bytes={len(payload)} ms=10 "
                f"kib_s=1 crc32={checksum:08X}"
            ),
        ]
        self.acknowledged = False

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        pass

    def readline(self, *, deadline):
        return self.lines.pop(0)

    def read_exactly(self, size, *, deadline):
        value = bytes(self.payload[:size])
        del self.payload[:size]
        return value

    def acknowledge(self):
        self.acknowledged = True


def make_connection(fake):
    conn = DeviceConnection.__new__(DeviceConnection)
    conn._port = "FAKE"
    conn._ser = fake
    return conn


def test_device_connection_expands_serial_driver_buffers(monkeypatch):
    class FakeSerialPort:
        def __init__(self, *args, **kwargs):
            self.buffer_sizes = []

        def set_buffer_size(self, **kwargs):
            self.buffer_sizes.append(kwargs)

    fake = FakeSerialPort()
    monkeypatch.setattr("dayvault.dio.serial.Serial", lambda *args, **kwargs: fake)

    conn = DeviceConnection("COM_TEST")

    assert conn._ser is fake
    assert fake.buffer_sizes == [{"rx_size": 1024 * 1024, "tx_size": 64 * 1024}]


def get2_response(total_size, offset, payload, crc=None):
    checksum = zlib.crc32(payload) if crc is None else crc
    header = (
        f"GET2START size={total_size} offset={offset} length={len(payload)}\n"
    ).encode()
    trailer = f"GET2END sent={len(payload)} crc32={checksum:08X}\n".encode()
    return header + payload + trailer


def test_vidpid_regex():
    hw = "USB VID:PID=0483:5740 SER=206C36943831"
    m = _VIDPID_RE.search(hw)
    assert m and int(m.group(1), 16) == 0x0483 and int(m.group(2), 16) == 0x5740
    assert _SER_RE.search(hw).group(1) == "206C36943831"


def test_foreign_vidpid_regex_match():
    m = _VIDPID_RE.search("USB VID:PID=1D6B:0002")
    assert m
    assert int(m.group(2), 16) != 0x5740


def test_get2_downloads_new_file_and_validates_crc(tmp_path):
    payload = bytes(range(64)) * 4
    fake = FakeGet2Serial(lambda command: get2_response(len(payload), 0, payload))
    conn = make_connection(fake)
    part = tmp_path / "REC001.WAV.part"
    progress = []

    result = conn.download_get2(
        "REC001.WAV", str(part), expected_size=len(payload),
        progress_cb=lambda done, total: progress.append((done, total)),
    )

    assert result == len(payload)
    assert part.read_bytes() == payload
    assert fake.writes == [b"GET2 0 REC001.WAV\r\n"]
    assert progress[-1] == (len(payload), len(payload))


def test_get2_resumes_from_existing_partial_file(tmp_path):
    full = b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    prefix = full[:11]
    suffix = full[len(prefix):]
    part = tmp_path / "REC002.WAV.part"
    part.write_bytes(prefix)
    fake = FakeGet2Serial(
        lambda command: get2_response(len(full), len(prefix), suffix), max_chunk=5
    )
    conn = make_connection(fake)

    result = conn.download_get2("REC002.WAV", str(part), expected_size=len(full))

    assert result == len(full)
    assert part.read_bytes() == full
    assert fake.writes == [f"GET2 {len(prefix)} REC002.WAV\r\n".encode()]


def test_get2_crc_failure_discards_only_new_range(tmp_path):
    prefix = b"verified-prefix"
    suffix = b"corrupt-range"
    part = tmp_path / "REC003.WAV.part"
    part.write_bytes(prefix)
    fake = FakeGet2Serial(
        lambda command: get2_response(
            len(prefix) + len(suffix), len(prefix), suffix, crc=0x12345678
        )
    )
    conn = make_connection(fake)

    with pytest.raises(IOError, match="CRC mismatch"):
        conn.download_get2(
            "REC003.WAV", str(part), expected_size=len(prefix) + len(suffix)
        )

    assert part.read_bytes() == prefix


def test_get2_interruption_preserves_received_partial_data(tmp_path):
    payload = b"abcdefghijklmnopqrstuvwxyz"
    part = tmp_path / "REC004.WAV.part"
    fake = FakeGet2Serial(
        lambda command: get2_response(len(payload), 0, payload), max_chunk=4
    )
    conn = make_connection(fake)

    with pytest.raises(InterruptedError):
        conn.download_get2(
            "REC004.WAV", str(part), expected_size=len(payload),
            interrupt=lambda: fake.raw_reads >= 2,
        )

    assert 0 < part.stat().st_size < len(payload)


def test_bulk2_resumes_streams_validates_and_restores_serial(monkeypatch, tmp_path):
    prefix = b"four"
    suffix = b"-seven!"
    part = tmp_path / "REC005.WAV.part"
    part.write_bytes(prefix)
    serial_port = FakeBulkSerial(b"")
    transport = FakeBulkTransport(suffix, offset=len(prefix))
    conn = make_connection(serial_port)
    conn._serial_number = "SERIAL"
    restored = []
    conn._reopen_serial = lambda timeout_s=20.0: restored.append(timeout_s)
    monkeypatch.setattr(
        "dayvault.dio.WinUsbTransport.wait_for_device",
        lambda serial_number, deadline: transport,
    )

    result = conn.download_bulk2(
        "REC005.WAV", str(part), expected_size=len(prefix) + len(suffix)
    )

    assert result == len(prefix) + len(suffix)
    assert part.read_bytes() == prefix + suffix
    assert serial_port.writes == [b"BULK2 4 REC005.WAV\r\n"]
    assert serial_port.closed
    assert transport.acknowledged
    assert restored == [20.0]


def test_bulk2_crc_failure_discards_only_new_range(monkeypatch, tmp_path):
    prefix = b"verified"
    suffix = b"bad-range"
    part = tmp_path / "REC006.WAV.part"
    part.write_bytes(prefix)
    serial_port = FakeBulkSerial(b"")
    transport = FakeBulkTransport(
        suffix, crc=0x12345678, offset=len(prefix)
    )
    conn = make_connection(serial_port)
    conn._serial_number = "SERIAL"
    conn._reopen_serial = lambda timeout_s=20.0: None
    monkeypatch.setattr(
        "dayvault.dio.WinUsbTransport.wait_for_device",
        lambda serial_number, deadline: transport,
    )

    with pytest.raises(IOError, match="CRC mismatch"):
        conn.download_bulk2(
            "REC006.WAV", str(part), expected_size=len(prefix) + len(suffix)
        )

    assert part.read_bytes() == prefix
