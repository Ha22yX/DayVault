from pathlib import Path

from PySide6.QtWidgets import QApplication

from dayvault import app


class FakeDeleteConnection:
    response = "DELETE OK name=REC-20260812-1827_3h15m55s.OPUS"
    commands = []
    closed = False

    def __init__(self, port):
        self.port = port

    def send_command(self, command):
        self.commands.append(command)
        return self.response

    def close(self):
        type(self).closed = True


def test_delete_thread_sends_named_delete_and_emits_success(monkeypatch):
    FakeDeleteConnection.commands = []
    FakeDeleteConnection.closed = False
    monkeypatch.setattr(app.dio, "DeviceConnection", FakeDeleteConnection)
    deleted = []
    errors = []
    thread = app.DeleteThread(
        "COM9", "206C36943831", "REC-20260812-1827_3h15m55s.OPUS"
    )
    thread.file_deleted.connect(lambda serial, name: deleted.append((serial, name)))
    thread.delete_error.connect(lambda *args: errors.append(args))

    thread.run()

    assert FakeDeleteConnection.commands == [
        "DELETE REC-20260812-1827_3h15m55s.OPUS"
    ]
    assert deleted == [
        ("206C36943831", "REC-20260812-1827_3h15m55s.OPUS")
    ]
    assert errors == []
    assert FakeDeleteConnection.closed


def test_delete_thread_rejects_non_recording_name_before_connect(monkeypatch):
    def fail_if_connected(_port):
        raise AssertionError("invalid names must not open the device")

    monkeypatch.setattr(app.dio, "DeviceConnection", fail_if_connected)
    errors = []
    thread = app.DeleteThread("COM9", "206C36943831", "../config.txt")
    thread.delete_error.connect(lambda *args: errors.append(args))

    thread.run()

    assert errors
    assert "invalid recording filename" in errors[0][2]


def test_recording_table_layout_and_human_readable_values(monkeypatch, tmp_path):
    qt_app = QApplication.instance() or QApplication([])
    monkeypatch.setattr(app.store, "load_state", lambda _serial: {})
    window = app.MainWindow({
        "language": "zh",
        "sync_folder": str(tmp_path),
    })
    serial = "206C36943831"
    window.combo.addItem(f"{serial} (COM9)", serial)
    window.on_device_files(serial, [
        ("REC-20260812-1827_3h15m55s.OPUS", 34_034_119),
    ])
    window.on_device_status(serial, {
        "millivolts": 4130,
        "percent": 91,
        "usb_connected": True,
    })

    assert window.table.columnCount() == 5
    assert [window.table.horizontalHeaderItem(index).text() for index in range(5)] == [
        "\u6587\u4ef6\u540d", "\u5927\u5c0f", "\u5f00\u59cb\u65f6\u95f4", "\u5f55\u5236\u65f6\u957f", "\u72b6\u6001",
    ]
    assert window.table.columnWidth(window._COL_NAME) == 300
    assert window.table.columnWidth(window._COL_STATUS) == 90
    assert window.table.item(0, window._COL_SIZE).text() == "32.46 MB"
    assert window.table.item(0, window._COL_TIME).text() == "2026-08-12 18:27"
    assert window.table.item(0, window._COL_DURATION).text() == "3:15:55"
    assert window.battery_lbl.text() == "91% \u00b7 4.13 V \u00b7 \u5145\u7535\u4e2d"
    assert window.battery_icon._charging is True
    window.close()
    assert qt_app is not None

def test_open_folder_selects_the_recording_in_explorer(monkeypatch, tmp_path):
    qt_app = QApplication.instance() or QApplication([])
    window = app.MainWindow({"language": "en", "sync_folder": str(tmp_path)})
    serial = "206C36943831"
    name = "REC-20260812-1827_3h15m55s.OPUS"
    window.combo.addItem(f"{serial} (COM9)", serial)
    window._files[serial] = [{"name": name}]
    recording = tmp_path / serial / name
    recording.parent.mkdir(parents=True)
    recording.write_bytes(b"opus")
    launches = []
    monkeypatch.setattr(
        app.QProcess,
        "startDetached",
        lambda program, args: (launches.append((program, args)) or True, 42),
    )

    window._open_recording_folder(0)

    assert launches == [("explorer.exe", ["/select,", str(recording)])]
    window.close()
    assert qt_app is not None
