# AGENTS.md

## 绝对禁止的操作（HARD RULES）

### 永不修改会阻止进入 DFU 模式的任何参数

> **这是本项目的最高优先级禁令。违反 = 板子变砖（无法再通过 BOOT 进入 DFU）。**

1. **永不修改 FLASH 选项字节（option bytes）中的 boot 配置位**，特别是：
   - `nBOOT1`（FLASH_OPTR bit 23）：出厂值 `1`。改成 `0` 后，按住 BOOT（BOOT0=1）会从空 SRAM 启动导致死机，**无法进入 DFU**。
   - `nBOOT0`（bit 27）、`nSWBOOT0`（bit 26）同理，除非有 100% 把握并保留恢复途径。
2. **永不使用 dfu-util 向 `0x1FFF7800` 选项字节区写入任何内容**，除非：
   - 已经验证过写入值正确
   - 且板子当前**已在 DFU 模式**
   - 且明确知道如何从最坏情况恢复（SWD/ST-Link 或备用板）
3. **烧录固件前，永远先确认当前 DFU 可用**（`dfu-util -l` 能看到设备），烧录后确认能正常复位。

### 为什么

DayVault 硬件（STM32L452）出厂选项字节 `FLASH_OPTR = 0xFFFFF8AA`（nBOOT1=1）。
实测 boot 行为：
- **nBOOT1=1**：按住 BOOT + 复位 → System memory → **DFU 可用**（正常）
- **nBOOT1=0**：按住 BOOT + 复位 → 空 SRAM → **死机，永远无法再进 DFU**（灾难）

2026-08-08 曾因修改 nBOOT1=0 导致 4 块板子失去 DFU 入口，只能靠 SWD/ST-Link 恢复。**此教训必须永不再犯。**

## 硬件关键事实（调试时必须遵守）

- BOOT0 引脚（PH3，60脚）：R3 10k 下拉，SW2 按钮拉高。**没有 MCU GPIO 连接**，固件无法驱动它。
- 软件进入 DFU 的正确方式：**固件内 `dfu_enter` 软件跳转到 `0x1FFF0000`**（System memory），不依赖 BOOT0/nBOOT1。
- 新增固件触发 DFU 的正确模式：启动时读 PH3 为高则软件跳转（见 `app.c` 的 `PIN_DFU_TRIGGER`）。
- USB 枚举必须用 ST 官方 VID `0x0483`（`0x0083` 会导致 VID_0000 枚举失败）。
