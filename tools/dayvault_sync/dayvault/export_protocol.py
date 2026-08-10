"""Pure parsing for the resumable GET2 file-export protocol."""
from __future__ import annotations

from dataclasses import dataclass
import re


_START_RE = re.compile(r"^GET2START size=(\d+) offset=(\d+) length=(\d+)$")
_END_RE = re.compile(r"^GET2END sent=(\d+) crc32=([0-9A-Fa-f]{8})$")


@dataclass(frozen=True)
class Get2Start:
    total_size: int
    offset: int
    length: int


@dataclass(frozen=True)
class Get2End:
    sent: int
    crc32: int


def parse_get2_start(line: str) -> Get2Start:
    match = _START_RE.fullmatch(line.strip())
    if match is None:
        raise ValueError(f"malformed GET2START line: {line!r}")
    total_size, offset, length = (int(value) for value in match.groups())
    if offset > total_size or length != total_size - offset:
        raise ValueError(f"inconsistent GET2START metadata: {line!r}")
    return Get2Start(total_size=total_size, offset=offset, length=length)


def parse_get2_end(line: str) -> Get2End:
    match = _END_RE.fullmatch(line.strip())
    if match is None:
        raise ValueError(f"malformed GET2END line: {line!r}")
    return Get2End(sent=int(match.group(1)), crc32=int(match.group(2), 16))
