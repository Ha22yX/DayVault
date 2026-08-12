<h1 align="center">DayVault</h1>

<p align="center">
  一台可随身佩戴的录音设备，把日常对话保存为 Ogg Opus，并同步到电脑形成可检索的人生档案。
</p>

<p align="center">
  <a href="README.md">English</a> ·
  <a href="https://github.com/Ha22yX/DayVault/releases/latest">最新发布</a> ·
  <a href="Docs/README.md">硬件文档</a> ·
  <a href="tools/dayvault_sync/README.md">同步程序</a> ·
  <a href="Docs/Serial-Command-Reference.md">通信协议</a>
</p>

<p align="center">
  <img alt="Release" src="https://img.shields.io/github/v/release/Ha22yX/DayVault?style=flat-square&color=2563EB" />
  <img alt="MCU" src="https://img.shields.io/badge/MCU-STM32L452-205A4B?style=flat-square&logo=stmicroelectronics&logoColor=white" />
  <img alt="Audio" src="https://img.shields.io/badge/audio-Ogg%20Opus-5F7F73?style=flat-square" />
  <img alt="Firmware" src="https://img.shields.io/badge/firmware-PlatformIO-F5822A?style=flat-square" />
  <img alt="Desktop" src="https://img.shields.io/badge/desktop-PySide6-41CD52?style=flat-square&logo=qt&logoColor=white" />
</p>

<table>
  <tr>
    <td width="50%" align="center">
      <img src=".github/assets/dayvault-mic-module.jpg" alt="装有 PCB、电池和打印外壳的 DayVault 实机原型" />
      <br />
      <strong>已装机的硬件原型</strong><br />
      STM32 主板、电池、USB-C、microSD 和轻量化 3D 打印外壳。
    </td>
    <td width="50%" align="center">
      <img src=".github/assets/dayvault-sync-app.png" alt="显示录音、电池电压和充电状态的 DayVault 同步程序" />
      <br />
      <strong>Windows 同步程序</strong><br />
      最新录音优先，并显示文件大小、时长、电量、电压和充电状态。
    </td>
  </tr>
</table>

## DayVault 能做什么

DayVault 已经形成一条完整的个人录音链路：

1. 两颗朝向相反的 PDM 麦克风采集胸前和身体方向的人声。
2. STM32L452 固件自适应融合双麦克风，得到 16 kHz 单声道音频。
3. 音频直接编码成目标 24 kbit/s 的 Ogg Opus 文件并写入 microSD。
4. Windows 程序通过 USB 自动识别设备，把录音同步到电脑，供后续转写和归档。

正常录音不会同时保留 WAV 或原始 PCM。按 24 kbit/s 计算，连续录制一天约占 260 MB，未计文件系统开销。

## 为什么开发它

我想要一个能全天跟着我的小设备，把容易被忘记的对话和经历记录下来。每天晚上把录音导出、转成文字，就能留下一个可以搜索和回看的生活档案。

## 当前功能

| 部分 | 已实现 |
| --- | --- |
| 录音 | 双 SPH0655 PDM 采集、自适应融合、16 kHz 单声道 Ogg Opus、20 ms 帧、目标 24 kbit/s。 |
| 存储 | microSD 时间戳命名、安全结束录音、断点续传、CRC32 校验和循环清理支持。 |
| USB | USB-C CDC 控制、WinUSB 批量传输、时间同步、充电检测以及软件或 ROM DFU。 |
| 电源 | PA0 电池采样、低电压保护、STOP2 休眠、RTC 定时检查和插入 USB 后恢复。 |
| 同步程序 | 自动同步、最新优先、人性化文件大小、开始时间、录制时长、电量和充电显示、打开、定位、另存为及二次确认删除。 |
| 外壳 | 可分成两部分 FDM 打印的胸前背夹外壳，只保留麦克风和 USB 开孔。 |

## 硬件组成

| 子系统 | 器件 | 作用 |
| --- | --- | --- |
| 主控 | STM32L452RCT6 | 音频采集、Opus 编码、存储、USB、RTC 和功耗管理。 |
| 麦克风 | 2 × SPH0655LM4H-1-8 | 朝向相反的数字 PDM 人声采集。 |
| 存储 | SPI1 microSD | 保存本地 Ogg Opus 录音。 |
| USB | USB-C Full Speed | 同步、控制协议、充电输入和 DFU。 |
| 3.3 V 电源 | TPS63031 | 单节锂电池升降压供电。 |
| 充电 | MCP73831T-2ACI/OT | 单节锂电池 4.20 V 恒压充电。 |
| 计时 | STM32 RTC + 32.768 kHz 晶振 | 录音时间戳和低功耗唤醒。 |

<p align="center">
  <img width="49%" src=".github/assets/dayvault-schematic.png" alt="DayVault 原理图" />
  <img width="49%" src=".github/assets/dayvault-pcb-layout.png" alt="DayVault PCB 布局" />
</p>

## 下载

[最新 GitHub Release](https://github.com/Ha22yX/DayVault/releases/latest)包含：

- <code>DayVaultSync-v1.1.0.exe</code>：打包好的 Windows 同步程序。
- <code>DayVault-firmware-v1.1.0.bin</code>：STM32L452 固件。
- Release 说明中的 SHA-256 校验值。

固件更新必须写入 DFU 的备用接口 0、地址 <code>0x08000000</code>。严禁写入备用接口 1 或 <code>0x1FFF7800</code> 选项字节区域。

## 本地开发

从源码运行同步程序：

    cd tools/dayvault_sync
    pip install -r requirements.txt
    python main.py

构建 Windows EXE：

    cd tools\dayvault_sync
    build_exe.bat

构建固件：

    cd firmware
    platformio run -e dayvault

运行维护中的测试：

    cd tools/dayvault_sync
    python -m pytest -q

    cd ../../
    python -m pytest -q Mechanical/tests

## 仓库结构

| 路径 | 内容 |
| --- | --- |
| <code>EDA/</code> | EasyEDA 工程和导出的硬件数据。 |
| <code>Docs/</code> | 硬件、引脚、电池、录音和串口协议文档。 |
| <code>firmware/</code> | PlatformIO STM32 固件和原生测试。 |
| <code>Mechanical/</code> | Blender 外壳源文件、生成脚本、测试、STL 和渲染图。 |
| <code>tools/dayvault_sync/</code> | PySide6 Windows 同步程序。 |

## 文档入口

- [硬件概览](Docs/01-Hardware-Overview.md)
- [MCU 引脚表](Docs/02-MCU-Pinout.md)
- [器件连接关系](Docs/03-Component-Pinout.md)
- [BOM](Docs/07-BOM.md)
- [Opus 录音说明](Docs/09-Opus-Recording.md)
- [高速传输](Docs/High-Speed-Transfer.md)
- [串口命令参考](Docs/Serial-Command-Reference.md)
- [机械外壳](Mechanical/README.md)

## 项目状态

DayVault 目前是一个持续迭代的硬件原型。当前主板已经能够完成录音、Opus 存储、USB 同步、电池状态读取、低电压休眠，并装入打印外壳。长时间可靠性、更多佩戴场景下的声学调校、充电温升和外壳细节仍需要继续实测。

## 隐私与安全

在不同地区和场景中，录制他人可能需要明确告知或获得同意。请把录音和转写内容作为敏感个人数据保护。

这是一套使用锂电池的可穿戴原型。必须使用带保护电芯、确认极性、做好导线应力释放并测试充电温度；早期硬件充电时不要无人看管。

## 许可证

项目目前尚未选择开源许可证。添加许可证之前，所有权利仍归仓库所有者。
