from PySide6.QtCore import QCoreApplication

from dayvault import app, dio


class _ActiveSync:
    def isRunning(self):
        return True


def test_active_sync_hides_temporary_cdc_disconnect(monkeypatch):
    qt_app = QCoreApplication.instance() or QCoreApplication([])
    monitor = app.DeviceMonitor(config={"poll_interval_ms": 60_000})
    monitor._timer.stop()
    monitor._known = {"206C36943831": "COM9"}
    monitor._threads = {"206C36943831": _ActiveSync()}
    removed = []
    monitor.device_removed.connect(removed.append)
    monkeypatch.setattr(dio, "list_dayvault_ports", lambda: [])

    monitor.poll()

    assert monitor._known == {"206C36943831": "COM9"}
    assert removed == []
    assert qt_app is not None
