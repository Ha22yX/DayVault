import json
from pathlib import Path
from dayvault import store


def test_config_roundtrip(tmp_path, monkeypatch):
    monkeypatch.setenv("APPDATA", str(tmp_path))
    cfg = store.load_config()
    assert "sync_folder" in cfg
    cfg["sync_folder"] = str(tmp_path / "sync")
    store.save_config(cfg)
    assert store.load_config()["sync_folder"] == str(tmp_path / "sync")


def test_state_roundtrip(tmp_path, monkeypatch):
    monkeypatch.setenv("APPDATA", str(tmp_path))
    store.save_state("SERIAL123", {"REC-20260809-1110.WAV": 189332})
    assert store.load_state("SERIAL123") == {"REC-20260809-1110.WAV": 189332}
    assert store.load_state("OTHER") == {}
