import sys

from PySide6.QtWidgets import QApplication

from dayvault import store
from dayvault.app import MainWindow, DeviceMonitor, TrayIcon


def main() -> int:
    store.setup_logging()
    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)   # stay resident when window closes
    win = MainWindow()
    mon = DeviceMonitor(win)
    win.monitor = mon
    tray = TrayIcon(win)
    win.tray = tray
    app.aboutToQuit.connect(mon.stop)
    win.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
