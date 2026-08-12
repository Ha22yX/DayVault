"""PySide6 GUI: main window, device monitor, tray icon, sync thread wiring."""
from __future__ import annotations

import logging
import shutil
from datetime import datetime
from pathlib import Path

from PySide6.QtCore import QObject, QPointF, QProcess, Qt, QThread, QTimer, QUrl, Signal
from PySide6.QtGui import (
    QColor, QDesktopServices, QFont, QIcon, QPainter, QPen, QPixmap, QPolygonF,
)
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMenu,
    QMessageBox,
    QPushButton,
    QSystemTrayIcon,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from . import dio, engine, lang, proto, store

log = logging.getLogger("dayvault")
LANG = lang.Lang()   # configured from config["language"] in MainWindow.__init__


def _make_icon() -> QIcon:
    """Small rounded-square 'DV' icon drawn at runtime (no binary asset needed)."""
    pix = QPixmap(64, 64)
    pix.fill(Qt.GlobalColor.transparent)
    p = QPainter(pix)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    p.setBrush(QColor(23, 42, 88))
    p.setPen(Qt.PenStyle.NoPen)
    p.drawRoundedRect(3, 3, 58, 58, 13, 13)
    p.setPen(QColor(245, 220, 120))
    f = QFont()
    f.setBold(True)
    f.setPointSize(15)
    p.setFont(f)
    p.drawText(pix.rect().adjusted(0, -2, 0, 2), Qt.AlignmentFlag.AlignCenter, "DV")
    p.end()
    return QIcon(pix)


class BatteryIndicator(QWidget):
    """Compact battery glyph with an in-battery charging bolt."""

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self._percent: int | None = None
        self._charging = False
        self.setFixedSize(27, 16)

    def set_status(self, percent: int | None, charging: bool = False) -> None:
        self._percent = percent
        self._charging = charging
        self.update()

    def paintEvent(self, _event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        color = QColor(125, 125, 125)
        if self._percent is not None:
            color = QColor(45, 160, 85) if self._percent > 20 else QColor(220, 145, 35)
            if self._percent <= 10:
                color = QColor(210, 65, 65)
        painter.setPen(QPen(color, 1.5))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawRoundedRect(1, 2, 22, 12, 2, 2)
        painter.fillRect(23, 6, 3, 4, color)
        if self._percent is not None:
            fill_width = max(1, round(18 * self._percent / 100))
            painter.fillRect(3, 4, fill_width, 8, color)
        if self._charging:
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(QColor(255, 255, 255))
            painter.drawPolygon(QPolygonF([
                QPointF(13, 2.5), QPointF(8.5, 8), QPointF(12, 8),
                QPointF(10.5, 13.5), QPointF(16.5, 6.5), QPointF(13, 6.5),
            ]))
        painter.end()


class _TrackingConn:
    """Wraps a DeviceConnection so the sync thread can report which file is
    being downloaded and forward the parsed LIST result to the UI."""

    def __init__(self, conn, thread: "SyncThread"):
        self._conn = conn
        self._t = thread
        self._current = ""
        self._list_buf: list[tuple[str, int]] = []
        self._in_list = False
        self._files_sent = False

    def send_command(self, cmd: str, timeout_s: float = 10.0) -> str:
        line = self._conn.send_command(cmd, timeout_s)
        if cmd.startswith("LIST"):
            self._in_list = True
            self._list_buf = list(proto.parse_list_output(line))
        return line

    def readline(self, timeout_s: float = 5.0) -> str:
        line = self._conn.readline(timeout_s)
        if self._in_list:
            self._list_buf.extend(proto.parse_list_output(line))
            if "LIST done" in line:
                self._in_list = False
                self._flush()
        return line

    def download_dl2(self, name: str, dest_path: str,
                     progress_cb=None, ack_byte: bytes = b"G", idle_ms: int = 60,
                     interrupt=None) -> int:
        self._current = name
        if self._in_list:
            self._in_list = False
            self._flush()
        return self._conn.download_dl2(name, dest_path, progress_cb, ack_byte, idle_ms,
                                       interrupt)

    def download_get2(self, name: str, dest_path: str, expected_size: int | None = None,
                      progress_cb=None, interrupt=None) -> int:
        self._current = name
        if self._in_list:
            self._in_list = False
            self._flush()
        return self._conn.download_get2(
            name, dest_path, expected_size=expected_size,
            progress_cb=progress_cb, interrupt=interrupt,
        )

    def download_bulk2(self, name: str, dest_path: str,
                       expected_size: int | None = None,
                       progress_cb=None, interrupt=None) -> int:
        self._current = name
        if self._in_list:
            self._in_list = False
            self._flush()
        return self._conn.download_bulk2(
            name, dest_path, expected_size=expected_size,
            progress_cb=progress_cb, interrupt=interrupt,
        )

    def close(self):
        self._conn.close()

    def current_name(self) -> str:
        return self._current

    def flush(self):
        self._flush()

    def _flush(self):
        if not self._files_sent:
            self._files_sent = True
            self._t.device_files.emit(self._t.serial, list(self._list_buf))


class SyncThread(QThread):
    """Runs sync_device for one device; emits UI-safe signals (cross-thread)."""

    device_files = Signal(str, list)          # serial, [(name, size)]
    download_progress = Signal(str, str, int)  # serial, name, pct
    sync_finished = Signal(str, dict)          # serial, result
    sync_error = Signal(str, str)              # serial, message
    device_status = Signal(str, dict)           # serial, battery/USB status

    def __init__(self, port: str, serial: str, config: dict, parent: QObject | None = None):
        super().__init__(parent)
        self._port = port
        self._serial = serial
        self._config = config

    @property
    def serial(self) -> str:
        return self._serial

    def run(self):
        conn = None
        try:
            conn = dio.DeviceConnection(self._port)
            tracked = _TrackingConn(conn, self)
            try:
                info = proto.parse_device_info(tracked.send_command("INFO"))
                if info is not None:
                    self.device_status.emit(self._serial, info)
            except Exception as error:
                log.warning("device status unavailable for %s: %s", self._serial, error)

            def progress_cb(done: int, total: int):
                pct = int(done * 100 / total) if total else 0
                self.download_progress.emit(self._serial, tracked.current_name(), pct)

            result = engine.sync_device(tracked, self._serial, self._config,
                                        progress_cb=progress_cb, log=log,
                                        interrupt=lambda: self.isInterruptionRequested())
            tracked.flush()
            self.sync_finished.emit(self._serial, result)
        except InterruptedError:
            log.info("sync %s interrupted", self._serial)
        except Exception as e:
            log.error("sync %s failed: %s", self._serial, e)
            self.sync_error.emit(self._serial, str(e))
        finally:
            if conn is not None:
                conn.close()


class DeleteThread(QThread):
    """Delete one recording from the device without blocking the UI."""

    file_deleted = Signal(str, str)       # serial, name
    delete_error = Signal(str, str, str)  # serial, name, message

    def __init__(self, port: str, serial: str, name: str,
                 parent: QObject | None = None):
        super().__init__(parent)
        self._port = port
        self._serial = serial
        self._name = name

    def run(self):
        conn = None
        try:
            if proto.parse_rec_name(self._name) is None:
                raise ValueError("invalid recording filename")
            conn = dio.DeviceConnection(self._port)
            response = conn.send_command(f"DELETE {self._name}")
            if not response.startswith("DELETE OK "):
                raise IOError(response or "device did not confirm deletion")
            self.file_deleted.emit(self._serial, self._name)
        except Exception as error:
            log.error("delete failed: serial=%s name=%s error=%s",
                      self._serial, self._name, error)
            self.delete_error.emit(self._serial, self._name, str(error))
        finally:
            if conn is not None:
                conn.close()


class DeviceMonitor(QObject):
    """Polls COM ports for DayVault boards and drives one SyncThread per device."""

    device_added = Signal(str, str)      # port, serial
    device_removed = Signal(str)         # serial
    device_files = Signal(str, list)
    download_progress = Signal(str, str, int)
    sync_finished = Signal(str, dict)
    sync_error = Signal(str, str)
    file_deleted = Signal(str, str)
    file_action_error = Signal(str, str, str)
    device_status = Signal(str, dict)

    def __init__(self, parent: QObject | None = None, config: dict | None = None):
        super().__init__(parent)
        if config is not None:
            self.config = config
        elif parent is not None and hasattr(parent, "config"):
            self.config = parent.config
        else:
            self.config = store.load_config()
        self._known: dict[str, str] = {}           # serial -> port
        self._threads: dict[str, SyncThread] = {}  # active sync threads by serial
        self._timer = QTimer(self)
        self._timer.timeout.connect(self.poll)
        self._timer.start(int(self.config.get("poll_interval_ms", 1500)))

    def poll(self):
        try:
            ports = dio.list_dayvault_ports()
        except Exception as e:
            log.error("port scan failed: %s", e)
            return
        current = {serial: port for port, serial in ports}
        for serial in list(self._known):
            if serial not in current:
                sync_thread = self._threads.get(serial)
                if sync_thread is not None and sync_thread.isRunning():
                    continue
                del self._known[serial]
                log.info("device removed: serial=%s", serial)
                self.device_removed.emit(serial)
        for serial, port in current.items():
            if serial not in self._known:
                self._known[serial] = port
                log.info("device added: port=%s serial=%s", port, serial)
                self.device_added.emit(port, serial)
                self.start_sync(port, serial)
            elif self._known[serial] != port:
                log.info("device port changed: serial=%s old=%s new=%s", serial, self._known[serial], port)
                self._known[serial] = port
                if serial not in self._threads:
                    self.start_sync(port, serial)

    def start_sync(self, port: str, serial: str):
        if serial in self._threads:
            return
        t = SyncThread(port, serial, self.config, parent=self)
        t.device_files.connect(self.device_files)
        t.download_progress.connect(self.download_progress)
        t.device_status.connect(self.device_status)
        t.sync_finished.connect(self._relay_finished)
        t.sync_error.connect(self._relay_error)
        self._threads[serial] = t
        t.finished.connect(lambda s=serial: self._threads.pop(s, None))
        t.start()

    def _relay_finished(self, serial: str, result: dict):
        self.sync_finished.emit(serial, result)

    def _relay_error(self, serial: str, msg: str):
        self.sync_error.emit(serial, msg)

    def sync_now(self, serial: str | None = None):
        for ser, port in list(self._known.items()):
            if serial is None or ser == serial:
                self.start_sync(port, ser)

    def delete_file(self, serial: str, name: str) -> bool:
        port = self._known.get(serial)
        if port is None:
            self.file_action_error.emit(serial, name, "device is disconnected")
            return False
        active = self._threads.get(serial)
        if active is not None and active.isRunning():
            self.file_action_error.emit(serial, name, "device is busy syncing")
            return False
        thread = DeleteThread(port, serial, name, parent=self)
        thread.file_deleted.connect(self.file_deleted)
        thread.delete_error.connect(self.file_action_error)
        self._threads[serial] = thread
        thread.finished.connect(lambda s=serial: self._threads.pop(s, None))
        thread.start()
        return True

    def stop(self):
        self._timer.stop()
        for t in list(self._threads.values()):
            t.requestInterruption()
        for serial, t in list(self._threads.items()):
            if not t.wait(5000):
                log.warning("sync thread %s did not stop in 5s", serial)
            if t.isFinished():
                self._threads.pop(serial, None)
            else:
                log.warning("sync thread %s still running; keeping reference", serial)


class MainWindow(QMainWindow):
    _COL_NAME = 0
    _COL_SIZE = 1
    _COL_TIME = 2
    _COL_DURATION = 3
    _COL_STATUS = 4

    def __init__(self, config: dict | None = None):
        super().__init__()
        self.config = config if config is not None else store.load_config()
        LANG.lang = self.config.get("language", "en")
        self._monitor: DeviceMonitor | None = None
        self._tray: TrayIcon | None = None
        self._files: dict[str, list[dict]] = {}  # serial -> rows
        self._device_status: dict[str, dict] = {}
        self._pending_deletes: set[tuple[str, str]] = set()
        self._build_ui()
        self._retranslate()
        self.setWindowIcon(_make_icon())
        self.resize(760, 480)
        self.setMinimumWidth(720)

    # ------------------------------------------------------------------ UI

    def _build_ui(self):
        central = QWidget(self)
        v = QVBoxLayout(central)

        top = QHBoxLayout()
        self.device_lbl = QLabel("")
        top.addWidget(self.device_lbl)
        self.combo = QComboBox()
        self.combo.setMinimumWidth(240)
        top.addWidget(self.combo)
        self.sync_btn = QPushButton("")
        top.addWidget(self.sync_btn)
        self.battery_icon = BatteryIndicator()
        top.addWidget(self.battery_icon)
        self.battery_lbl = QLabel("")
        top.addWidget(self.battery_lbl)
        top.addStretch(1)
        self.lang_lbl = QLabel("")
        top.addWidget(self.lang_lbl)
        self.lang_combo = QComboBox()
        self.lang_combo.addItem("English", "en")
        self.lang_combo.addItem("\u4e2d\u6587", "zh")
        self.lang_combo.setCurrentIndex(0 if LANG.lang == "en" else 1)
        top.addWidget(self.lang_combo)
        v.addLayout(top)

        self.table = QTableWidget(0, 5)
        self.table.setHorizontalHeaderLabels(["", "", "", "", ""] )
        self.table.setColumnWidth(self._COL_NAME, 300)
        self.table.setColumnWidth(self._COL_SIZE, 90)
        self.table.setColumnWidth(self._COL_TIME, 145)
        self.table.setColumnWidth(self._COL_DURATION, 85)
        self.table.setColumnWidth(self._COL_STATUS, 90)
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.table.cellDoubleClicked.connect(self.on_file_double_clicked)
        self.table.customContextMenuRequested.connect(self.on_file_context_menu)
        self.table.verticalHeader().setVisible(False)
        self.table.horizontalHeader().setStretchLastSection(False)
        v.addWidget(self.table, 1)

        bot = QHBoxLayout()
        self.folder_lbl = QLabel("")
        bot.addWidget(self.folder_lbl)
        self.folder_edit = QLineEdit(self.config.get("sync_folder", ""))
        self.folder_edit.setReadOnly(True)
        bot.addWidget(self.folder_edit, 1)
        self.folder_btn = QPushButton("")
        bot.addWidget(self.folder_btn)
        v.addLayout(bot)

        self.setCentralWidget(central)
        self.sync_btn.clicked.connect(self.on_sync_now)
        self.folder_btn.clicked.connect(self.on_choose_folder)
        self.combo.currentIndexChanged.connect(self.on_serial_changed)
        self.lang_combo.currentIndexChanged.connect(self.on_language_changed)
        self.statusBar().showMessage("")

    def _retranslate(self):
        """Update all static UI text for the current language."""
        self.setWindowTitle(LANG.tr("title"))
        self.device_lbl.setText(LANG.tr("device"))
        self.sync_btn.setText(LANG.tr("sync_btn"))
        self._refresh_battery_status()
        self.lang_lbl.setText(LANG.tr("lang_label"))
        self.table.setHorizontalHeaderLabels([
            LANG.tr("col_name"), LANG.tr("col_size"),
            LANG.tr("col_time"), LANG.tr("col_duration"), LANG.tr("col_status"),
        ])
        self.folder_lbl.setText(LANG.tr("folder"))
        self.folder_btn.setText(LANG.tr("choose_folder"))
        if self._tray is not None:
            self._tray.retranslate()
        self.statusBar().showMessage(LANG.tr("waiting"))
        self._refresh_table()

    def on_language_changed(self):
        LANG.lang = self.lang_combo.currentData() or "en"
        self.config["language"] = LANG.lang
        store.save_config(self.config)
        self._retranslate()

    # ------------------------------------------------------------ wiring

    @property
    def monitor(self) -> DeviceMonitor | None:
        return self._monitor

    @monitor.setter
    def monitor(self, mon: DeviceMonitor | None):
        self._monitor = mon
        if mon is None:
            return
        mon.device_added.connect(self.on_device_added)
        mon.device_removed.connect(self.on_device_removed)
        mon.device_files.connect(self.on_device_files)
        mon.download_progress.connect(self.on_download_progress)
        mon.sync_finished.connect(self.on_sync_finished)
        mon.sync_error.connect(self.on_sync_error)
        mon.file_deleted.connect(self.on_file_deleted)
        mon.file_action_error.connect(self.on_file_action_error)
        mon.device_status.connect(self.on_device_status)

    @property
    def tray(self) -> "TrayIcon | None":
        return self._tray

    @tray.setter
    def tray(self, t: "TrayIcon | None"):
        self._tray = t

    # ---------------------------------------------------------- handlers

    def current_serial(self) -> str | None:
        return self.combo.currentData()

    def on_serial_changed(self):
        self._refresh_table()
        self._refresh_battery_status()

    def on_device_status(self, serial: str, status: dict):
        self._device_status[serial] = status
        if serial == self.current_serial():
            self._refresh_battery_status()

    def on_device_added(self, port: str, serial: str):
        log.info("sync start: port=%s serial=%s", port, serial)
        if self.combo.findData(serial) < 0:
            self.combo.addItem(f"{serial} ({port})", serial)
        self.combo.setCurrentIndex(self.combo.findData(serial))
        self.statusBar().showMessage(LANG.tr("device_detected", serial), 6000)

    def on_device_removed(self, serial: str):
        idx = self.combo.findData(serial)
        if idx >= 0:
            self.combo.removeItem(idx)
        self._files.pop(serial, None)
        self._device_status.pop(serial, None)
        self._refresh_battery_status()
        self.statusBar().showMessage(LANG.tr("device_disconnected", serial), 6000)

    def on_device_files(self, serial: str, files: list[tuple[str, int]]):
        state = store.load_state(serial)
        rows = []
        for name, size in files:
            rows.append({
                "name": name,
                "size": size,
                "time": self._display_time(name, serial),
                "duration": self._display_duration(name),
                "status_key": "downloaded" if state.get(name) == size else "not_downloaded",
                "pct": 0,
            })
        rows.sort(key=lambda r: self._sort_key(r["name"]), reverse=True)   # newest first
        self._files[serial] = rows
        if serial == self.current_serial():
            self._refresh_table()

    def on_download_progress(self, serial: str, name: str, pct: int):
        if serial != self.current_serial():
            return
        for row in self._files.get(serial, []):
            if row["name"] == name:
                row["status_key"] = "downloading"
                row["pct"] = pct
                break
        self._refresh_table()

    def on_sync_finished(self, serial: str, result: dict):
        rows = self._files.setdefault(serial, [])
        downloaded = set(result.get("downloaded", []))
        failed = set(result.get("failed", []))
        state = store.load_state(serial)
        for row in rows:
            if row["name"] in downloaded:
                row["status_key"] = "downloaded"
            elif row["name"] in failed:
                row["status_key"] = "failed"
            elif row["status_key"] == "downloading":
                row["status_key"] = "downloaded" if state.get(row["name"]) == row["size"] else "not_downloaded"
        if serial == self.current_serial():
            self._refresh_table()
        parts = [LANG.tr("sync_finished", serial)]
        if downloaded:
            parts.append(LANG.tr("new_downloads", len(downloaded)))
        if failed:
            parts.append(LANG.tr("failures", len(failed)))
        log.info("sync finished: serial=%s downloaded=%d failed=%d", serial, len(downloaded), len(failed))
        self.statusBar().showMessage("\uff0c".join(parts) if LANG.lang == "zh" else ", ".join(parts), 8000)

    def on_sync_error(self, serial: str, msg: str):
        log.error("sync error: serial=%s msg=%s", serial, msg)
        for row in self._files.get(serial, []):
            if row["status_key"] == "downloading":
                row["status_key"] = "failed"
        if serial == self.current_serial():
            self._refresh_table()
        self.statusBar().showMessage(LANG.tr("sync_error", serial, msg), 10000)

    def on_sync_now(self):
        serial = self.current_serial()
        if not serial:
            self.statusBar().showMessage(LANG.tr("no_device"), 4000)
            return
        if self._monitor is None:
            self.statusBar().showMessage(LANG.tr("device_idle"), 4000)
            return
        self._monitor.sync_now(serial)
        self.statusBar().showMessage(LANG.tr("manual_sync", serial), 4000)

    def on_choose_folder(self):
        start = self.folder_edit.text() or str(Path.home() / "Documents")
        d = QFileDialog.getExistingDirectory(self, LANG.tr("choose_folder_title"), start)
        if not d:
            return
        self.config["sync_folder"] = d
        store.save_config(self.config)
        self.folder_edit.setText(d)
        self.statusBar().showMessage(LANG.tr("folder_changed", d), 8000)

    # ------------------------------------------------------------ helpers

    @staticmethod
    def _sort_key(name: str):
        """Sort key: timestamp recordings (newest first), then legacy seq files, then others."""
        meta = proto.parse_rec_name(name)
        if meta and meta["kind"] == "timestamp":
            return (1, meta.get("timestamp", ""), meta.get("collision", 0), name.casefold())
        if meta and meta["kind"] == "seq":
            return (0, "", meta.get("seq", 0), name.casefold())
        return (-1, "", 0, name.casefold())

    def _display_time(self, name: str, serial: str) -> str:
        meta = proto.parse_rec_name(name)
        if meta and meta.get("timestamp"):
            return meta["timestamp"]
        f = Path(self.config.get("sync_folder", "")) / serial / name
        if f.exists():
            try:
                return datetime.fromtimestamp(f.stat().st_mtime).strftime("%Y-%m-%d %H:%M")
            except Exception:
                pass
        return ""

    @staticmethod
    def _display_duration(name: str) -> str:
        meta = proto.parse_rec_name(name)
        seconds = meta.get("duration_secs") if meta else None
        return proto.format_duration(seconds)

    def _refresh_table(self):
        rows = self._files.get(self.current_serial()) or []
        self.table.setRowCount(len(rows))
        for i, row in enumerate(rows):
            name_item = QTableWidgetItem(row["name"])
            name_item.setToolTip(row["name"])
            size_item = QTableWidgetItem(proto.format_file_size(row["size"]))
            size_item.setTextAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            time_item = QTableWidgetItem(row["time"])
            duration_item = QTableWidgetItem(row["duration"])
            duration_item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
            key = row.get("status_key", "not_downloaded")
            text = {
                "downloaded": LANG.tr("status_downloaded"),
                "not_downloaded": LANG.tr("status_not_downloaded"),
                "downloading": LANG.tr("status_downloading", row.get("pct", 0)),
                "failed": LANG.tr("status_failed"),
            }.get(key, key)
            status_item = QTableWidgetItem(text)
            if key == "downloaded":
                status_item.setForeground(QColor(0, 130, 0))
            elif key == "downloading":
                status_item.setForeground(QColor(190, 120, 0))
            elif key == "failed":
                status_item.setForeground(QColor(190, 0, 0))
            self.table.setItem(i, self._COL_NAME, name_item)
            self.table.setItem(i, self._COL_SIZE, size_item)
            self.table.setItem(i, self._COL_TIME, time_item)
            self.table.setItem(i, self._COL_DURATION, duration_item)
            self.table.setItem(i, self._COL_STATUS, status_item)

    def _refresh_battery_status(self):
        status = self._device_status.get(self.current_serial())
        if status is None:
            self.battery_icon.set_status(None)
            self.battery_lbl.setText(LANG.tr("battery_unknown"))
            self.battery_lbl.setStyleSheet("color: #7d7d7d;")
            return
        percent = int(status.get("percent", 0))
        millivolts = int(status.get("millivolts", 0))
        charging = bool(status.get("usb_connected", False))
        self.battery_icon.set_status(percent, charging)
        key = "battery_charging" if charging else "battery_status"
        self.battery_lbl.setText(LANG.tr(key, percent, millivolts / 1000.0))
        color = "#2da055" if percent > 20 else "#dc9123"
        if percent <= 10:
            color = "#d24141"
        self.battery_lbl.setStyleSheet(f"color: {color};")

    def _row_data(self, row_index: int) -> dict | None:
        rows = self._files.get(self.current_serial()) or []
        if row_index < 0 or row_index >= len(rows):
            return None
        return rows[row_index]

    def _local_file(self, name: str, serial: str | None = None) -> Path:
        serial = serial or self.current_serial()
        return Path(self.config.get("sync_folder", "")) / serial / name

    def _warn(self, title_key: str, message_key: str, *args) -> None:
        QMessageBox.warning(self, LANG.tr(title_key), LANG.tr(message_key, *args))

    def _open_recording(self, row_index: int) -> None:
        row = self._row_data(row_index)
        if row is None:
            return
        path = self._local_file(row["name"])
        if not path.is_file():
            self._warn("file_unavailable_title", "file_unavailable", row["name"])
            return
        if not QDesktopServices.openUrl(QUrl.fromLocalFile(str(path))):
            self._warn("file_open_failed_title", "file_open_failed", row["name"])

    def on_file_double_clicked(self, row: int, _column: int) -> None:
        self._open_recording(row)

    def on_file_context_menu(self, position) -> None:
        row_index = self.table.indexAt(position).row()
        row = self._row_data(row_index)
        if row is None:
            return
        self.table.selectRow(row_index)
        menu = QMenu(self)
        open_action = menu.addAction(LANG.tr("action_open"))
        delete_action = menu.addAction(LANG.tr("action_delete"))
        folder_action = menu.addAction(LANG.tr("action_open_folder"))
        save_as_action = menu.addAction(LANG.tr("action_save_as"))
        chosen = menu.exec(self.table.viewport().mapToGlobal(position))
        if chosen == open_action:
            self._open_recording(row_index)
        elif chosen == delete_action:
            self._delete_recording(row_index)
        elif chosen == folder_action:
            self._open_recording_folder(row_index)
        elif chosen == save_as_action:
            self._save_recording_as(row_index)

    def _open_recording_folder(self, row_index: int) -> None:
        row = self._row_data(row_index)
        if row is None:
            return
        recording = self._local_file(row["name"])
        folder = recording.parent
        folder.mkdir(parents=True, exist_ok=True)
        if recording.is_file():
            started, _pid = QProcess.startDetached(
                "explorer.exe", ["/select,", str(recording)]
            )
            if started:
                return
        if not QDesktopServices.openUrl(QUrl.fromLocalFile(str(folder))):
            self._warn("file_open_failed_title", "folder_open_failed", str(folder))

    def _save_recording_as(self, row_index: int) -> None:
        row = self._row_data(row_index)
        if row is None:
            return
        source = self._local_file(row["name"])
        if not source.is_file():
            self._warn("file_unavailable_title", "file_unavailable", row["name"])
            return
        destination, _ = QFileDialog.getSaveFileName(
            self, LANG.tr("save_as_title"), str(Path.home() / row["name"]),
            LANG.tr("audio_file_filter"),
        )
        if not destination:
            return
        try:
            shutil.copy2(source, destination)
        except Exception as error:
            self._warn("save_as_failed_title", "save_as_failed", str(error))
            return
        self.statusBar().showMessage(LANG.tr("save_as_complete", destination), 8000)

    def _delete_recording(self, row_index: int) -> None:
        row = self._row_data(row_index)
        serial = self.current_serial()
        if row is None or not serial:
            return
        name = row["name"]
        if (serial, name) in self._pending_deletes:
            return
        answer = QMessageBox.question(
            self,
            LANG.tr("delete_confirm_title"),
            LANG.tr("delete_confirm", name),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        if self._monitor is None:
            self._warn("delete_failed_title", "device_unavailable")
            return
        self._pending_deletes.add((serial, name))
        if not self._monitor.delete_file(serial, name):
            self._pending_deletes.discard((serial, name))
            return
        self.statusBar().showMessage(LANG.tr("deleting_file", name), 8000)

    def on_file_deleted(self, serial: str, name: str) -> None:
        self._pending_deletes.discard((serial, name))
        local_errors = []
        for path in (self._local_file(name, serial), self._local_file(name + ".part", serial)):
            try:
                path.unlink(missing_ok=True)
            except Exception as error:
                local_errors.append(str(error))
        state = store.load_state(serial)
        state.pop(name, None)
        store.save_state(serial, state)
        self._files[serial] = [
            row for row in self._files.get(serial, []) if row["name"] != name
        ]
        if serial == self.current_serial():
            self._refresh_table()
        if local_errors:
            self._warn("delete_partial_title", "delete_partial", name, "; ".join(local_errors))
        else:
            self.statusBar().showMessage(LANG.tr("delete_complete", name), 8000)

    def on_file_action_error(self, serial: str, name: str, message: str) -> None:
        self._pending_deletes.discard((serial, name))
        self._warn("delete_failed_title", "delete_failed", name, message)

    def closeEvent(self, event):
        tray_ok = (self._tray is not None and self._tray.isVisible()
                   and QSystemTrayIcon.isSystemTrayAvailable())
        if tray_ok:
            event.ignore()
            self.hide()
            self._tray.showMessage(
                LANG.tr("title"), LANG.tr("tray_hide"),
                QSystemTrayIcon.MessageIcon.Information, 2500)
        else:
            event.accept()


class TrayIcon(QSystemTrayIcon):
    def __init__(self, window: MainWindow):
        super().__init__(_make_icon(), window)
        self._window = window
        self.menu = QMenu()
        self.act_show = self.menu.addAction("")
        self.act_show.triggered.connect(self.show_window)
        self.menu.addSeparator()
        self.act_quit = self.menu.addAction("")
        self.act_quit.triggered.connect(QApplication.instance().quit)
        self.setContextMenu(self.menu)
        self.activated.connect(self.on_activated)
        self.retranslate()
        self.setVisible(True)

    def retranslate(self):
        self.setToolTip(LANG.tr("tray_tip"))
        self.act_show.setText(LANG.tr("tray_show"))
        self.act_quit.setText(LANG.tr("tray_quit"))

    def on_activated(self, reason: QSystemTrayIcon.ActivationReason):
        if reason == QSystemTrayIcon.ActivationReason.Trigger:
            self.show_window()

    def show_window(self):
        w = self._window
        w.showNormal()
        w.raise_()
        w.activateWindow()
