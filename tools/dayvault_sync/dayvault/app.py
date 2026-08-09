"""PySide6 GUI: main window, device monitor, tray icon, sync thread wiring."""
from __future__ import annotations

import logging
from datetime import datetime
from pathlib import Path

from PySide6.QtCore import QObject, Qt, QThread, QTimer, Signal
from PySide6.QtGui import QColor, QFont, QIcon, QPainter, QPixmap
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMenu,
    QPushButton,
    QSystemTrayIcon,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from . import dio, engine, proto, store

log = logging.getLogger("dayvault")


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


class DeviceMonitor(QObject):
    """Polls COM ports for DayVault boards and drives one SyncThread per device."""

    device_added = Signal(str, str)      # port, serial
    device_removed = Signal(str)         # serial
    device_files = Signal(str, list)
    download_progress = Signal(str, str, int)
    sync_finished = Signal(str, dict)
    sync_error = Signal(str, str)

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
                del self._known[serial]
                self.device_removed.emit(serial)
        for serial, port in current.items():
            if serial not in self._known:
                self._known[serial] = port
                self.device_added.emit(port, serial)
                self.start_sync(port, serial)

    def start_sync(self, port: str, serial: str):
        if serial in self._threads:
            return
        t = SyncThread(port, serial, self.config, parent=self)
        t.device_files.connect(self.device_files)
        t.download_progress.connect(self.download_progress)
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
    _COL_STATUS = 3

    def __init__(self, config: dict | None = None):
        super().__init__()
        self.config = config if config is not None else store.load_config()
        self._monitor: DeviceMonitor | None = None
        self._tray: TrayIcon | None = None
        self._files: dict[str, list[dict]] = {}  # serial -> rows
        self._build_ui()
        self.setWindowTitle("DayVault 同步")
        self.setWindowIcon(_make_icon())

    # ------------------------------------------------------------------ UI

    def _build_ui(self):
        central = QWidget(self)
        v = QVBoxLayout(central)

        top = QHBoxLayout()
        top.addWidget(QLabel("设备:"))
        self.combo = QComboBox()
        self.combo.setMinimumWidth(240)
        top.addWidget(self.combo)
        self.sync_btn = QPushButton("立即同步")
        top.addWidget(self.sync_btn)
        top.addStretch(1)
        v.addLayout(top)

        self.table = QTableWidget(0, 4)
        self.table.setHorizontalHeaderLabels(["文件名", "大小", "时间", "状态"])
        self.table.setColumnWidth(self._COL_NAME, 270)
        self.table.setColumnWidth(self._COL_SIZE, 100)
        self.table.setColumnWidth(self._COL_TIME, 150)
        self.table.setColumnWidth(self._COL_STATUS, 110)
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.verticalHeader().setVisible(False)
        v.addWidget(self.table, 1)

        bot = QHBoxLayout()
        bot.addWidget(QLabel("同步文件夹:"))
        self.folder_edit = QLineEdit(self.config.get("sync_folder", ""))
        self.folder_edit.setReadOnly(True)
        bot.addWidget(self.folder_edit, 1)
        self.folder_btn = QPushButton("选择…")
        bot.addWidget(self.folder_btn)
        v.addLayout(bot)

        self.setCentralWidget(central)
        self.sync_btn.clicked.connect(self.on_sync_now)
        self.folder_btn.clicked.connect(self.on_choose_folder)
        self.combo.currentIndexChanged.connect(self.on_serial_changed)
        self.statusBar().showMessage("等待设备…")

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

    def on_device_added(self, port: str, serial: str):
        if self.combo.findData(serial) < 0:
            self.combo.addItem(f"{serial} ({port})", serial)
        self.combo.setCurrentIndex(self.combo.findData(serial))
        self.statusBar().showMessage(f"检测到设备 {serial}，正在同步…", 6000)

    def on_device_removed(self, serial: str):
        idx = self.combo.findData(serial)
        if idx >= 0:
            self.combo.removeItem(idx)
        self._files.pop(serial, None)
        self.statusBar().showMessage(f"设备已断开 {serial}", 6000)

    def on_device_files(self, serial: str, files: list[tuple[str, int]]):
        state = store.load_state(serial)
        rows = []
        for name, size in files:
            rows.append({
                "name": name,
                "size": size,
                "time": self._display_time(name, serial),
                "status": "已下载" if state.get(name) == size else "未下载",
            })
        self._files[serial] = rows
        if serial == self.current_serial():
            self._refresh_table()

    def on_download_progress(self, serial: str, name: str, pct: int):
        if serial != self.current_serial():
            return
        for row in self._files.get(serial, []):
            if row["name"] == name:
                row["status"] = f"下载中 {pct}%"
                break
        self._refresh_table()

    def on_sync_finished(self, serial: str, result: dict):
        rows = self._files.setdefault(serial, [])
        downloaded = set(result.get("downloaded", []))
        failed = set(result.get("failed", []))
        for row in rows:
            if row["name"] in downloaded:
                row["status"] = "已下载"
            elif row["name"] in failed:
                row["status"] = "下载失败"
        if serial == self.current_serial():
            self._refresh_table()
        parts = [f"{serial} 同步完成"]
        if downloaded:
            parts.append(f"新下载 {len(downloaded)} 个")
        if failed:
            parts.append(f"失败 {len(failed)} 个")
        self.statusBar().showMessage("，".join(parts), 8000)

    def on_sync_error(self, serial: str, msg: str):
        self.statusBar().showMessage(f"{serial} 同步出错: {msg}", 10000)

    def on_sync_now(self):
        serial = self.current_serial()
        if not serial:
            self.statusBar().showMessage("没有选中的设备", 4000)
            return
        if self._monitor is None:
            self.statusBar().showMessage("设备监测未启动", 4000)
            return
        self._monitor.sync_now(serial)
        self.statusBar().showMessage(f"正在同步 {serial}…", 4000)

    def on_choose_folder(self):
        start = self.folder_edit.text() or str(Path.home() / "Documents")
        d = QFileDialog.getExistingDirectory(self, "选择同步文件夹", start)
        if not d:
            return
        self.config["sync_folder"] = d
        store.save_config(self.config)
        self.folder_edit.setText(d)
        self.statusBar().showMessage(f"同步文件夹已设置为 {d}", 8000)

    # ------------------------------------------------------------ helpers

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

    def _refresh_table(self):
        rows = self._files.get(self.current_serial()) or []
        self.table.setRowCount(len(rows))
        for i, row in enumerate(rows):
            name_item = QTableWidgetItem(row["name"])
            size_item = QTableWidgetItem(f"{row['size']:,}")
            size_item.setTextAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            time_item = QTableWidgetItem(row["time"])
            status_item = QTableWidgetItem(row["status"])
            if row["status"] == "已下载":
                status_item.setForeground(QColor(0, 130, 0))
            elif row["status"].startswith("下载中"):
                status_item.setForeground(QColor(190, 120, 0))
            elif row["status"] == "下载失败":
                status_item.setForeground(QColor(190, 0, 0))
            self.table.setItem(i, self._COL_NAME, name_item)
            self.table.setItem(i, self._COL_SIZE, size_item)
            self.table.setItem(i, self._COL_TIME, time_item)
            self.table.setItem(i, self._COL_STATUS, status_item)

    def closeEvent(self, event):
        if self._tray is not None and self._tray.isVisible():
            event.ignore()
            self.hide()
            self._tray.showMessage(
                "DayVault", "已最小化到系统托盘，点击托盘图标可重新打开",
                QSystemTrayIcon.MessageIcon.Information, 2500)
        else:
            event.accept()


class TrayIcon(QSystemTrayIcon):
    def __init__(self, window: MainWindow):
        super().__init__(_make_icon(), window)
        self._window = window
        self.setToolTip("DayVault 同步")
        menu = QMenu()
        act_show = menu.addAction("打开主界面")
        act_show.triggered.connect(self.show_window)
        menu.addSeparator()
        act_quit = menu.addAction("退出")
        act_quit.triggered.connect(QApplication.instance().quit)
        self.setContextMenu(menu)
        self.activated.connect(self.on_activated)
        self.setVisible(True)

    def on_activated(self, reason: QSystemTrayIcon.ActivationReason):
        if reason == QSystemTrayIcon.ActivationReason.Trigger:
            self.show_window()

    def show_window(self):
        w = self._window
        w.showNormal()
        w.raise_()
        w.activateWindow()
