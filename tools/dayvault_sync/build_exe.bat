@echo off
pip install -r requirements.txt
pyinstaller --noconfirm --onefile --windowed --name DayVaultSync ^
  --hidden-import PySide6.QtWidgets ^
  main.py
echo EXE: dist\DayVaultSync.exe
