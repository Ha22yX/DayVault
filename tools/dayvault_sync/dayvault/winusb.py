"""Lazy PyUSB transport for the DayVault WinUSB GET2 endpoint."""
from __future__ import annotations

import time
from collections.abc import Callable, Iterable
from typing import Any


TARGET_VID = 0x0483
TARGET_PID = 0x5741
INTERFACE = 0
BULK_IN_ENDPOINT = 0x81
BULK_OUT_ENDPOINT = 0x02
USB_PACKET_BYTES = 64


class WinUsbTransport:
    """A claimed DayVault WinUSB device with buffered bulk reads.

    ``deadline`` arguments are absolute values from the injected monotonic
    clock. This makes a GET2 payload read leave any following trailer bytes
    ready for ``readline``.
    """

    def __init__(
        self,
        device: Any,
        *,
        usb_util: Any | None = None,
        timeout_error: type[BaseException] | tuple[type[BaseException], ...] = (),
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self.device = device
        self._usb_util = usb_util
        self._timeout_error = timeout_error
        self._clock = clock
        self._buffer = bytearray()
        self._claimed = False

    @classmethod
    def wait_for_device(
        cls,
        serial_number: str,
        *,
        deadline: float,
        discover: Callable[[], Iterable[Any]] | None = None,
        clock: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
        poll_interval: float = 0.1,
        usb_util: Any | None = None,
    ) -> "WinUsbTransport":
        """Return the matching device before an absolute monotonic deadline."""
        discover = discover or cls._discover_devices
        while clock() < deadline:
            for device in discover():
                try:
                    candidate_serial = getattr(device, "serial_number", None)
                except Exception:
                    continue
                if candidate_serial == serial_number:
                    return cls(device, usb_util=usb_util, clock=clock)
            sleep(min(poll_interval, max(0.0, deadline - clock())))
        raise TimeoutError(f"DayVault WinUSB device {serial_number!r} did not appear")

    @staticmethod
    def _discover_devices() -> Iterable[Any]:
        """Discover only the target VID/PID, importing PyUSB on demand."""
        try:
            import libusb_package
        except ImportError:
            import usb.core

            return usb.core.find(
                find_all=True,
                idVendor=TARGET_VID,
                idProduct=TARGET_PID,
            ) or ()
        return libusb_package.find(
            find_all=True, idVendor=TARGET_VID, idProduct=TARGET_PID
        ) or ()

    def claim(self) -> None:
        """Configure and claim interface 0 once."""
        if self._claimed:
            return
        util = self._get_usb_util()
        try:
            active = self.device.get_active_configuration()
        except Exception:
            active = None
        if active is None or getattr(active, "bConfigurationValue", None) != 1:
            self.device.set_configuration()
        util.claim_interface(self.device, INTERFACE)
        self._claimed = True

    def close(self) -> None:
        """Release the claimed interface."""
        if self._claimed:
            util = self._get_usb_util()
            util.release_interface(self.device, INTERFACE)
            dispose = getattr(util, "dispose_resources", None)
            if dispose is not None:
                dispose(self.device)
            self._claimed = False

    def __enter__(self) -> "WinUsbTransport":
        self.claim()
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.close()

    def readline(self, *, deadline: float) -> str:
        """Read and decode one ASCII line terminated by LF."""
        while True:
            newline = self._buffer.find(b"\n")
            if newline >= 0:
                line = bytes(self._buffer[:newline])
                del self._buffer[:newline + 1]
                return line.rstrip(b"\r").decode("ascii")
            self._buffer.extend(self._read_packet(deadline))

    def read_exactly(self, size: int, *, deadline: float) -> bytes:
        """Read exactly ``size`` raw bytes, retaining any bulk-packet excess."""
        if size < 0:
            raise ValueError("size must not be negative")
        while len(self._buffer) < size:
            remaining = min(64 * 1024, size - len(self._buffer))
            request_bytes = (
                (remaining + USB_PACKET_BYTES - 1) // USB_PACKET_BYTES
            ) * USB_PACKET_BYTES
            self._buffer.extend(self._read_packet(deadline, request_bytes))
        value = bytes(self._buffer[:size])
        del self._buffer[:size]
        return value

    def acknowledge(self, *, timeout_ms: int = 1000) -> None:
        """Confirm that all payload and trailer bytes reached the host."""
        written = self.device.write(BULK_OUT_ENDPOINT, b"DONE\n", timeout=timeout_ms)
        if written != 5:
            raise IOError(f"short WinUSB acknowledgement: {written}/5")

    def _read_packet(self, deadline: float, max_bytes: int = 64 * 1024) -> bytes:
        remaining = deadline - self._clock()
        if remaining <= 0:
            raise TimeoutError("timed out waiting for DayVault WinUSB data")
        timeout_ms = max(1, int(remaining * 1000))
        try:
            return bytes(self.device.read(BULK_IN_ENDPOINT, max_bytes, timeout=timeout_ms))
        except Exception as error:
            if self._timeout_error and isinstance(error, self._timeout_error):
                return b""
            raise

    def _get_usb_util(self) -> Any:
        if self._usb_util is None:
            import usb.core
            import usb.util

            self._usb_util = usb.util
            self._timeout_error = usb.core.USBTimeoutError
        return self._usb_util
