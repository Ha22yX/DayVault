# DayVault Development Documentation

This directory describes the current DayVault hardware and serial protocol documentation.
The machine-readable hardware review artifact is `../EDA/DayVault.netlist.json`; the
EasyEDA source snapshot is `../EDA/DayVault.eprj2`.

## Reading order

1. [01-Hardware-Overview.md](01-Hardware-Overview.md) - architecture and power domains.
2. [02-MCU-Pinout.md](02-MCU-Pinout.md) - complete STM32L452RCT6 64-pin assignment.
3. [03-Component-Pinout.md](03-Component-Pinout.md) - every major component connection.
4. [07-BOM.md](07-BOM.md) - primary parts and passive values.
5. [08-Net-Map.md](08-Net-Map.md) - net-centric endpoint map for debugging and review.
6. [Serial-Command-Reference.md](Serial-Command-Reference.md) - host/device serial protocol used by firmware and the Windows sync app.
7. [High-Speed-Transfer.md](High-Speed-Transfer.md) - WinUSB protocol, fallback order, measured throughput, CRC, and resume behavior.

## Documentation rules

- `Current` means the connection exists in the exported netlist.
- `Required revision` means the EasyEDA design still needs to be changed.
- Empty MCU pins are intentionally documented as `NC`; do not connect them to ground.
- After any schematic change, regenerate the netlist and update these documents in the
  same commit.
