"""Parsing for the DayVault serial protocol and recording names."""
from __future__ import annotations
import re
from datetime import datetime, timedelta, timezone

_TIMESTAMP_RE = re.compile(
    r"^REC-(\d{4})(\d{2})(\d{2})-(\d{2})(\d{2})(?:_(\d+))?"
    r"(?:_(?:(\d+)h)?(\d+)m(\d+)s)?\.(OPUS|WAV)$", re.IGNORECASE
)
_SEQ_RE = re.compile(r"^REC(\d{3})\.(OPUS|WAV)$", re.IGNORECASE)
_INFO_BATTERY_RE = re.compile(r"\bbat=(\d+)mV\b.*?\bpct=(\d+)\b")
_INFO_USB_RE = re.compile(r"\busb_detect=(\d+)\b")


def parse_list_output(text: str) -> list[tuple[str, int]]:
    """Parse LIST output: each line '  <name> <size>' -> [(name, size)]."""
    out: list[tuple[str, int]] = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("LIST"):
            continue
        parts = line.rsplit(" ", 1)
        if len(parts) != 2:
            continue
        name, size = parts[0], parts[1]
        try:
            out.append((name, int(size)))
        except ValueError:
            continue
    return out


def parse_rec_name(name: str) -> dict | None:
    """Parse a recording filename into metadata, or None if not a REC file."""
    m = _TIMESTAMP_RE.match(name)
    if m:
        y, mo, d, h, mi = (int(g) for g in m.groups()[:5])
        dur_h = int(m.group(7)) if m.group(7) else 0
        dur_m = int(m.group(8)) if m.group(8) else 0
        dur_s = int(m.group(9)) if m.group(9) else 0
        return {
            "kind": "timestamp",
            "timestamp": f"{y:04d}-{mo:02d}-{d:02d} {h:02d}:{mi:02d}",
            "collision": int(m.group(6)) if m.group(6) else 0,
            "duration_secs": dur_h * 3600 + dur_m * 60 + dur_s if (m.group(7) or m.group(8) or m.group(9)) else None,
            "format": m.group(10).lower(),
        }
    m = _SEQ_RE.match(name)
    if m:
        return {
            "kind": "seq",
            "seq": int(m.group(1)),
            "duration_secs": None,
            "format": m.group(2).lower(),
        }
    return None


def parse_device_info(text: str) -> dict | None:
    """Extract battery and USB charging state from an INFO response."""
    battery = _INFO_BATTERY_RE.search(text)
    if battery is None:
        return None
    usb = _INFO_USB_RE.search(text)
    return {
        "millivolts": int(battery.group(1)),
        "percent": max(0, min(100, int(battery.group(2)))),
        "usb_connected": bool(int(usb.group(1))) if usb else False,
    }


def local_tz_offset_minutes(now: datetime | None = None) -> int:
    """Local UTC offset in minutes (e.g. +480 for UTC+8)."""
    now = now or datetime.now().astimezone()
    off = now.utcoffset() or timedelta(0)
    return int(off.total_seconds() // 60)

def format_file_size(size: int) -> str:
    """Format a byte count using binary 1024-based display units."""
    value = float(max(0, size))
    units = ("B", "KB", "MB", "GB", "TB")
    unit_index = 0
    while value >= 1024.0 and unit_index < len(units) - 1:
        value /= 1024.0
        unit_index += 1
    if unit_index == 0:
        return f"{int(value):,} B"
    number = f"{value:.2f}".rstrip("0").rstrip(".")
    return f"{number} {units[unit_index]}"


def format_duration(seconds: int | None) -> str:
    """Format recording duration as MM:SS or H:MM:SS."""
    if seconds is None or seconds < 0:
        return "\u2014"
    hours, remainder = divmod(seconds, 3600)
    minutes, secs = divmod(remainder, 60)
    if hours:
        return f"{hours}:{minutes:02d}:{secs:02d}"
    return f"{minutes:02d}:{secs:02d}"
