import pytest

from dayvault.winusb import WinUsbTransport


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakeUtil:
    def __init__(self):
        self.claimed = []
        self.released = []

    def claim_interface(self, device, interface):
        self.claimed.append((device, interface))

    def release_interface(self, device, interface):
        self.released.append((device, interface))


class FakeDevice:
    def __init__(self, serial_number, packets=(), active_configuration=None):
        self.serial_number = serial_number
        self.packets = list(packets)
        self.active_configuration = active_configuration
        self.configured = 0
        self.read_calls = []
        self.write_calls = []

    def get_active_configuration(self):
        if self.active_configuration is None:
            raise RuntimeError("not configured")
        return self.active_configuration

    def set_configuration(self):
        self.configured += 1

    def read(self, endpoint, size, timeout):
        self.read_calls.append((endpoint, size, timeout))
        return self.packets.pop(0) if self.packets else b""

    def write(self, endpoint, data, timeout):
        self.write_calls.append((endpoint, bytes(data), timeout))
        return len(data)


class TransientSerialDevice(FakeDevice):
    def __init__(self, serial_number):
        super().__init__(serial_number)
        self.serial_reads = 0
        self._target_serial = serial_number

    @property
    def serial_number(self):
        self.serial_reads += 1
        if self.serial_reads == 1:
            raise NotImplementedError("USB interface is not ready")
        return self._target_serial

    @serial_number.setter
    def serial_number(self, value):
        self._target_serial = value


def make_transport(packets):
    device = FakeDevice("target", packets)
    util = FakeUtil()
    clock = FakeClock()
    transport = WinUsbTransport(device, usb_util=util, clock=clock.monotonic)
    transport.claim()
    return transport, device, util, clock


def test_wait_for_device_matches_serial_and_releases_claimed_interface():
    clock = FakeClock()
    foreign = FakeDevice("other")
    target = FakeDevice("wanted")
    util = FakeUtil()

    transport = WinUsbTransport.wait_for_device(
        "wanted",
        deadline=1.0,
        discover=lambda: [foreign, target],
        clock=clock.monotonic,
        sleep=clock.sleep,
        usb_util=util,
    )
    transport.claim()
    transport.close()

    assert transport.device is target
    assert target.configured == 1
    assert util.claimed == [(target, 0)]
    assert util.released == [(target, 0)]


def test_claim_does_not_reset_an_already_active_configuration():
    active = type("Configuration", (), {"bConfigurationValue": 1})()
    device = FakeDevice("wanted", active_configuration=active)
    util = FakeUtil()
    transport = WinUsbTransport(device, usb_util=util)

    transport.claim()

    assert device.configured == 0
    assert util.claimed == [(device, 0)]


def test_wait_for_device_times_out_when_matching_serial_never_appears():
    clock = FakeClock()

    with pytest.raises(TimeoutError, match="wanted"):
        WinUsbTransport.wait_for_device(
            "wanted",
            deadline=0.3,
            discover=lambda: [FakeDevice("other")],
            clock=clock.monotonic,
            sleep=clock.sleep,
            poll_interval=0.1,
            usb_util=FakeUtil(),
        )


def test_wait_for_device_retries_transient_serial_descriptor_failure():
    clock = FakeClock()
    target = TransientSerialDevice("wanted")

    transport = WinUsbTransport.wait_for_device(
        "wanted",
        deadline=0.3,
        discover=lambda: [target],
        clock=clock.monotonic,
        sleep=clock.sleep,
        poll_interval=0.1,
        usb_util=FakeUtil(),
    )

    assert transport.device is target
    assert target.serial_reads == 2


def test_readline_joins_fragmented_lf_terminated_ascii_line():
    transport, device, _, _ = make_transport([b"GET2ST", b"ART\n"])

    assert transport.readline(deadline=1.0) == "GET2START"
    assert [call[0] for call in device.read_calls] == [0x81, 0x81]


def test_read_exactly_returns_raw_get2_payload_bytes():
    transport, _, _, _ = make_transport([b"GET2START size=4\n", b"\x00A\xffB"])

    assert transport.readline(deadline=1.0) == "GET2START size=4"
    assert transport.read_exactly(4, deadline=1.0) == bytes([0, 65, 255, 66])


def test_read_exactly_rounds_remaining_bytes_to_usb_packet_boundary():
    transport, device, _, _ = make_transport([b"A" * 64, b"B" * 6])

    assert transport.read_exactly(70, deadline=1.0) == b"A" * 64 + b"B" * 6
    assert [call[1] for call in device.read_calls] == [128, 64]


def test_read_exactly_preserves_packet_trailer_for_next_line():
    transport, _, _, _ = make_transport(
        [b"GET2START size=7\n", b"PAYLOADGET2END sent=7\n"]
    )

    assert transport.readline(deadline=1.0) == "GET2START size=7"
    assert transport.read_exactly(7, deadline=1.0) == b"PAYLOAD"
    assert transport.readline(deadline=1.0) == "GET2END sent=7"


def test_acknowledge_writes_done_to_bulk_out_endpoint():
    transport, device, _, _ = make_transport([])

    transport.acknowledge(timeout_ms=750)

    assert device.write_calls == [(0x02, b"DONE\n", 750)]
