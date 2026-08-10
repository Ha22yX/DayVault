# DayVault 同步工具（Windows）

DayVault Sync 会在设备通过 USB-C 接入后校准时间、读取录音列表，并把尚未同步的文件自动导出到电脑。下载过程支持 CRC32 校验、断点续传和自动重试。

## 传输方式

同步工具按以下顺序自动选择协议：

1. `BULK2`：专用 WinUSB 批量传输，默认首选。
2. `GET2`：保持 USB CDC 串口的连续传输后备方案。
3. `DL2`：兼容旧固件的传统分块协议。

`BULK2` 开始时，设备会从 CDC PID `0483:5740` 临时切换为 WinUSB PID `0483:5741`。文件校验完成并收到主机确认后，设备会自动恢复 CDC，继续同步下一个文件。

临时文件使用 `.part` 后缀。连接中断后不会删除已经接收的数据，下次从当前文件长度继续；只有在完整载荷的 CRC 校验失败时，才会回退到本次传输开始前的长度。

## 功能

- 自动发现 VID/PID `0483:5740` 的 DayVault 设备。
- 每次同步都更新 RTC 的 UTC 时间和本地时区偏移。
- 只下载新增或大小变化的录音。
- WinUSB 数据直接流式写入磁盘，不把完整录音加载到内存。
- 每个文件最多自动重试 3 次。
- 窗口关闭后驻留系统托盘，继续监听设备。
- 支持中文与英文界面。

## 源码运行

建议使用 Python 3.12 或更高版本：

```powershell
cd tools/dayvault_sync
python -m pip install -r requirements.txt
python main.py
```

`requirements.txt` 包含 PyUSB 与 `libusb-package`。后者会随应用提供 WinUSB 所需的 libusb 后端，不依赖用户手动把 DLL 放入 Python 或系统目录。

## 构建 EXE

先安装 PyInstaller，然后执行：

```bat
cd tools\dayvault_sync
build_exe.bat
```

生成文件位于 `dist\DayVaultSync.exe`。构建脚本会收集 PyUSB 后端和 `libusb-package` 中的原生库。

## 数据位置

| 路径 | 内容 |
|---|---|
| `%APPDATA%\DayVault\config.json` | 同步目录、轮询间隔、语言等配置 |
| `%APPDATA%\DayVault\state\<序列号>.json` | 每台设备已同步文件的名称与大小 |
| `%APPDATA%\DayVault\logs\app.log` | 运行和故障日志 |

录音默认下载到 `<同步目录>\<设备序列号>\`。

## 测试

```powershell
python -m pytest tools/dayvault_sync/tests -q
python tools/benchmark_winusb.py --port COM9 --timeout 45
```

当前原型的 WinUSB 实测约为 `225-227 KiB/s`。三次连续 2 MiB 压测、一个 4,508,018 字节真实录音，以及从 1,000,000 字节位置恢复的续传均通过字节数和校验验证。

## 常见问题

- **串口被占用**：关闭串口助手、其他同步工具或仍占用 COM 口的程序。
- **WinUSB 未出现**：确认固件支持 `BULK2`。工具会自动回退到 `GET2`，日志中会记录原因。
- **设备停留在 DFU**：复位设备进入正常固件；DFU 模式不会显示为 CDC 串口。
- **同步失败**：先查看 `%APPDATA%\DayVault\logs\app.log`，保留 `.part` 文件以便续传。
