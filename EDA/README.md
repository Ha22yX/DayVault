# EasyEDA Project Snapshot

## Files

- `DayVault.eprj2`: primary EasyEDA Pro project snapshot.
- `DayVault.netlist.json`: schematic netlist exported through the EasyEDA Pro API.
- `Backups/`: four EasyEDA automatic backups retained for recovery.

Snapshot date: 2026-08-01. The netlist uses format version 2.0.0 and contains 50 schematic
components. The API also finds 50 PCB components. The saved schematic/netlist and PCB pad
data place `PDM_DATA_3V3` on U5 PB12, while PB1 is NC; R9, R10, and C26 are present and
routed for `USB_DETECT`. Strict DRC still reports 58 clearance errors and one generic
schematic/PCB netlist mismatch, so this snapshot is not manufacturing-ready.

Do not edit the JSON as the design source. Make changes in `DayVault.eprj2`, synchronize
the PCB, rerun ERC/DRC, export a fresh JSON netlist through the API, and update `Docs/` in
the same Git commit.

## Snapshot SHA-256

```text
4365C5A7B50C0CD75C8DA6A10FFDC611223F76D3A9925CD758B8EED07B94D089  DayVault.eprj2
1E25BA79AAF0E169B6A0A0578B8C0372CAD703C3872673F63359A3E4C23DD294  DayVault.netlist.json
```

Checksums identify the current archived snapshot and must be updated whenever either file is
regenerated.
