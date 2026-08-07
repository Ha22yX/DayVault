# DayVault Development Documentation

This directory describes the hardware snapshot exported on 2026-08-01. The machine-
readable source of truth is `../EDA/DayVault.netlist.json`; the EasyEDA source snapshot
is `../EDA/DayVault.eprj2`.

## Reading order

1. [00-开发速查.md](00-开发速查.md) - 中文开发入口与当前最重要的硬件结论。
2. [01-Hardware-Overview.md](01-Hardware-Overview.md) - architecture and power domains.
3. [02-MCU-Pinout.md](02-MCU-Pinout.md) - complete STM32L452RCT6 64-pin assignment.
4. [03-Component-Pinout.md](03-Component-Pinout.md) - every major component connection.
5. [05-Bringup-and-Test.md](05-Bringup-and-Test.md) - safe first-power and validation sequence.
6. [06-Known-Issues.md](06-Known-Issues.md) - blockers and design limitations.
7. [07-BOM.md](07-BOM.md) - primary parts and passive values.
8. [08-Net-Map.md](08-Net-Map.md) - net-centric endpoint map for debugging and review.
9. [09-USB-DFU-Entry-Design.md](09-USB-DFU-Entry-Design.md) - software-triggered USB DFU auto-entry design.

## Documentation rules

- `Current` means the connection exists in the exported netlist.
- `Required revision` means the EasyEDA design still needs to be changed.
- Empty MCU pins are intentionally documented as `NC`; do not connect them to ground.
- After any schematic change, regenerate the netlist and update these documents in the
  same commit.
