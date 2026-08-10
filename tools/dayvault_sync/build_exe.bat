@echo off
pip install -r requirements.txt
pyinstaller --noconfirm --onefile --windowed --name DayVaultSync ^
  --hidden-import PySide6.QtWidgets ^
  --hidden-import usb.core ^
  --hidden-import usb.util ^
  --hidden-import usb.backend.libusb1 ^
  --collect-all libusb_package ^
  main.py
echo EXE: dist\DayVaultSync.exe
