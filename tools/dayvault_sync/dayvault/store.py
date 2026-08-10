"""Config, per-device sync state, and logging under %APPDATA%/DayVault."""
from __future__ import annotations
import json
import logging
import os
from logging.handlers import RotatingFileHandler
from pathlib import Path


def app_data_dir() -> Path:
    base = os.environ.get("APPDATA") or str(Path.home())
    d = Path(base) / "DayVault"
    d.mkdir(parents=True, exist_ok=True)
    return d


def _cfg_path() -> Path:
    return app_data_dir() / "config.json"


def load_config() -> dict:
    default = {
        "sync_folder": str(Path.home() / "Documents" / "DayVault"),
        "poll_interval_ms": 1500,
        "autostart": False,
        "language": "en",
    }
    p = _cfg_path()
    if p.exists():
        try:
            cfg = json.loads(p.read_text(encoding="utf-8"))
            default.update(cfg)
        except Exception:
            pass
    return default


def save_config(cfg: dict) -> None:
    _cfg_path().write_text(json.dumps(cfg, ensure_ascii=False, indent=2), encoding="utf-8")


def _state_path(serial: str) -> Path:
    d = app_data_dir() / "state"
    d.mkdir(parents=True, exist_ok=True)
    return d / f"{serial}.json"


def load_state(serial: str) -> dict[str, int]:
    p = _state_path(serial)
    if p.exists():
        try:
            return json.loads(p.read_text(encoding="utf-8"))
        except Exception:
            return {}
    return {}


def save_state(serial: str, files: dict[str, int]) -> None:
    _state_path(serial).write_text(json.dumps(files, ensure_ascii=False, indent=2), encoding="utf-8")


def setup_logging() -> None:
    d = app_data_dir() / "logs"
    d.mkdir(parents=True, exist_ok=True)
    handler = RotatingFileHandler(d / "app.log", maxBytes=512 * 1024, backupCount=2, encoding="utf-8")
    handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s"))
    root = logging.getLogger()
    root.setLevel(logging.INFO)
    if not any(isinstance(h, RotatingFileHandler) for h in root.handlers):
        root.addHandler(handler)
