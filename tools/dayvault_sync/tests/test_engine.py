from pathlib import Path

from dayvault import engine
from dayvault.dio import Bulk2Unsupported, Get2Unsupported
from dayvault.engine import plan_downloads


def test_plan_downloads_new_and_changed():
    remote = [("A.WAV", 100), ("B.WAV", 200), ("C.WAV", 300)]
    state = {"A.WAV": 100, "B.WAV": 999}
    assert plan_downloads(remote, state) == ["B.WAV", "C.WAV"]


def test_plan_downloads_empty():
    remote = [("A.WAV", 100)]
    state = {"A.WAV": 100}
    assert plan_downloads(remote, state) == []


class SyncConnection:
    def __init__(self, payload: bytes, bulk_failures=0, bulk_unsupported=False,
                 get2_failures=0, unsupported=False):
        self.payload = payload
        self.bulk_failures = bulk_failures
        self.bulk_unsupported = bulk_unsupported
        self.get2_failures = get2_failures
        self.unsupported = unsupported
        self.bulk_part_sizes = []
        self.get2_part_sizes = []
        self.dl2_calls = 0

    def send_command(self, command, timeout_s=10.0):
        if command.startswith("SETTIME "):
            return "TIME set"
        if command == "LIST":
            return f"  REC001.WAV {len(self.payload)}\nLIST done"
        raise AssertionError(command)

    def readline(self, timeout_s=5.0):
        raise TimeoutError

    def download_get2(self, name, dest_path, expected_size=None,
                      progress_cb=None, interrupt=None):
        path = Path(dest_path)
        self.get2_part_sizes.append(path.stat().st_size if path.exists() else 0)
        if self.unsupported:
            raise Get2Unsupported("old firmware")
        if self.get2_failures:
            self.get2_failures -= 1
            path.write_bytes(self.payload[:3])
            raise IOError("temporary disconnect")
        prefix = path.read_bytes() if path.exists() else b""
        path.write_bytes(prefix + self.payload[len(prefix):])
        return len(self.payload)

    def download_bulk2(self, name, dest_path, expected_size=None,
                       progress_cb=None, interrupt=None):
        path = Path(dest_path)
        self.bulk_part_sizes.append(path.stat().st_size if path.exists() else 0)
        if self.bulk_unsupported:
            raise Bulk2Unsupported("old firmware")
        if self.bulk_failures:
            self.bulk_failures -= 1
            path.write_bytes(self.payload[:4])
            raise IOError("temporary WinUSB disconnect")
        prefix = path.read_bytes() if path.exists() else b""
        path.write_bytes(prefix + self.payload[len(prefix):])
        return len(self.payload)

    def download_dl2(self, name, dest_path, progress_cb=None, interrupt=None):
        self.dl2_calls += 1
        Path(dest_path).write_bytes(self.payload)
        return len(self.payload)


def configure_sync_test(monkeypatch):
    saved = {}
    monkeypatch.setattr(engine.store, "load_state", lambda serial: {})
    monkeypatch.setattr(
        engine.store, "save_state", lambda serial, state: saved.update(state)
    )
    monkeypatch.setattr(engine.time, "sleep", lambda seconds: None)
    return saved


def test_sync_prefers_bulk2(monkeypatch, tmp_path):
    saved = configure_sync_test(monkeypatch)
    conn = SyncConnection(b"complete payload")

    result = engine.sync_device(
        conn, "SERIAL", {"sync_folder": str(tmp_path)}
    )

    assert result["downloaded"] == ["REC001.WAV"]
    assert conn.bulk_part_sizes == [0]
    assert conn.get2_part_sizes == []
    assert conn.dl2_calls == 0
    assert (tmp_path / "SERIAL" / "REC001.WAV").read_bytes() == b"complete payload"
    assert saved == {"REC001.WAV": len(b"complete payload")}


def test_sync_retries_get2_without_deleting_partial(monkeypatch, tmp_path):
    configure_sync_test(monkeypatch)
    conn = SyncConnection(
        b"resume me", bulk_unsupported=True, get2_failures=1
    )

    result = engine.sync_device(
        conn, "SERIAL", {"sync_folder": str(tmp_path)}
    )

    assert result["downloaded"] == ["REC001.WAV"]
    assert conn.get2_part_sizes == [0, 3]


def test_sync_retries_bulk2_without_deleting_partial(monkeypatch, tmp_path):
    configure_sync_test(monkeypatch)
    conn = SyncConnection(b"resume over bulk", bulk_failures=1)

    result = engine.sync_device(
        conn, "SERIAL", {"sync_folder": str(tmp_path)}
    )

    assert result["downloaded"] == ["REC001.WAV"]
    assert conn.bulk_part_sizes == [0, 4]
    assert conn.get2_part_sizes == []


def test_sync_falls_back_to_get2_when_bulk2_is_unavailable(monkeypatch, tmp_path):
    configure_sync_test(monkeypatch)
    conn = SyncConnection(b"serial fallback", bulk_unsupported=True)

    result = engine.sync_device(
        conn, "SERIAL", {"sync_folder": str(tmp_path)}
    )

    assert result["downloaded"] == ["REC001.WAV"]
    assert conn.bulk_part_sizes == [0]
    assert conn.get2_part_sizes == [0]
    assert conn.dl2_calls == 0


def test_sync_falls_back_to_dl2_for_old_firmware(monkeypatch, tmp_path):
    configure_sync_test(monkeypatch)
    conn = SyncConnection(
        b"legacy payload", bulk_unsupported=True, unsupported=True
    )

    result = engine.sync_device(
        conn, "SERIAL", {"sync_folder": str(tmp_path)}
    )

    assert result["downloaded"] == ["REC001.WAV"]
    assert conn.dl2_calls == 1
