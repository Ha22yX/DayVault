from PySide6.QtCore import QCoreApplication

from dayvault import app, dio


class _ActiveSync:
    def isRunning(self):
        return True


def test_active_sync_hides_temporary_cdc_disconnect(monkeypatch):
    qt_app = QCoreApplication.instance() or QCoreApplication([])
    monitor = app.DeviceMonitor(config={"poll_interval_ms": 60_000})
    monitor._timer.stop()
    monitor._status_timer.stop()
    monitor._known = {"206C36943831": "COM9"}
    monitor._threads = {"206C36943831": _ActiveSync()}
    removed = []
    monitor.device_removed.connect(removed.append)
    monkeypatch.setattr(dio, "list_dayvault_ports", lambda: [])

    monitor.poll()

    assert monitor._known == {"206C36943831": "COM9"}
    assert removed == []
    assert qt_app is not None


def test_battery_status_timer_defaults_to_20_seconds():
    qt_app = QCoreApplication.instance() or QCoreApplication([])
    monitor = app.DeviceMonitor(config={"poll_interval_ms": 60_000})
    monitor._timer.stop()
    monitor._status_timer.stop()

    assert monitor._status_timer.interval() == 20_000
    assert qt_app is not None


def test_status_poll_starts_only_for_idle_devices(monkeypatch):
    qt_app = QCoreApplication.instance() or QCoreApplication([])
    monitor = app.DeviceMonitor(config={
        "poll_interval_ms": 60_000,
        "battery_poll_interval_ms": 60_000,
    })
    monitor._timer.stop()
    monitor._status_timer.stop()
    monitor._known = {
        "busy": "COM8",
        "idle": "COM9",
    }
    monitor._threads = {"busy": _ActiveSync()}
    started = []
    monkeypatch.setattr(
        monitor,
        "start_status",
        lambda port, serial: started.append((port, serial)),
    )

    monitor.poll_status()

    assert started == [("COM9", "idle")]
