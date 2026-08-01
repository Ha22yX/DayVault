# EasyEDA Project Snapshot

## Files

- `DayVault.eprj2`: primary EasyEDA Pro project snapshot.
- `DayVault.netlist.json`: schematic netlist exported through the EasyEDA Pro API.
- `Backups/`: four EasyEDA automatic backups retained for recovery.

Snapshot date: 2026-08-01. The netlist uses format version 2.0.0 and contains 50 schematic
components. At inspection time, the PCB contained 47 components because R9, R10, and C26
had not yet been synchronized from schematic to PCB.

Do not edit the JSON as the design source. Make changes in `DayVault.eprj2`, synchronize
the PCB, rerun ERC/DRC, export a fresh JSON netlist through the API, and update `Docs/` in
the same Git commit.

## Snapshot SHA-256

```text
6B8D02C10ADA896E2BE33905E8473886486CCA425D56564623408D675A1C7A65  DayVault.eprj2
94F32555638693B1C584335770AD46079FFA7F2CCB59252D48991BE628205498  DayVault.netlist.json
```

Checksums identify this initial archive only and must be updated whenever either file is
regenerated.
