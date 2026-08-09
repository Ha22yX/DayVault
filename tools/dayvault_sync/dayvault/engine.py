"""Per-device sync orchestration (clock sync, list, diff, download)."""
from __future__ import annotations
import logging
import time
from datetime import datetime
from pathlib import Path

from . import proto, store


def plan_downloads(remote: list[tuple[str, int]], state: dict[str, int]) -> list[str]:
    """Remote files not in state (or with a changed size) that need downloading."""
    to_get = []
    for name, size in remote:
        if state.get(name) != size:
            to_get.append(name)
    return to_get


def sync_device(conn, serial: str, config: dict,
                progress_cb=None, log: logging.Logger | None = None,
                interrupt=None) -> dict:
    """Sync one device: set clock, list files, download unsynced, update state.

    `interrupt` may be a zero-arg callable; when it returns truthy the sync
    aborts with InterruptedError instead of continuing.
    """
    def _check():
        if interrupt and interrupt():
            raise InterruptedError("sync interrupted")

    log = log or logging.getLogger("dayvault")
    result = {"synced": 0, "downloaded": [], "failed": []}

    # 1. clock + timezone (every sync overwrites the offset, per design)
    _check()
    now = int(time.time())
    tz = proto.local_tz_offset_minutes()
    conn.send_command(f"SETTIME {now} {tz}")
    result["synced"] = 1

    # 2. list
    _check()
    lines = conn.send_command("LIST")
    remote = proto.parse_list_output(lines)
    # LIST prints many lines; drain the rest
    try:
        while True:
            _check()
            extra = conn.readline(timeout_s=1.5)
            if not extra or "LIST done" in extra:
                break
            remote.extend(proto.parse_list_output(extra))
    except TimeoutError:
        pass

    # 3. diff against state
    _check()
    state = store.load_state(serial)
    to_get = plan_downloads(remote, state)

    # 4. download each to <sync_folder>/<serial>/
    folder = Path(config["sync_folder"]) / serial
    folder.mkdir(parents=True, exist_ok=True)
    for name in to_get:
        _check()
        size = dict(remote).get(name, 0)
        part = folder / (name + ".part")
        final = folder / name
        ok = False
        for attempt in range(3):
            if attempt:
                time.sleep(0.25)
            try:
                n = conn.download_dl2(name, str(part), progress_cb=progress_cb,
                                      interrupt=interrupt)
                if n != size:
                    raise IOError(f"size mismatch {n} != {size}")
                part.replace(final)
                ok = True
                break
            except InterruptedError:
                raise
            except Exception as e:
                log.warning("download %s attempt %d failed: %s", name, attempt + 1, e)
                try:
                    part.unlink(missing_ok=True)
                except Exception:
                    pass
        if ok:
            state[name] = size
            store.save_state(serial, state)
            result["downloaded"].append(name)
        else:
            result["failed"].append(name)
    return result
