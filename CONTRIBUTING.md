# Contributing to DayVault

DayVault is an early hardware design. Contributions are most useful when they make the
saved revision easier to verify, reproduce, or safely bring up.

## Good Contributions

- Schematic and PCB reviews tied to exact nets, pins, coordinates, or reference designators.
- Datasheet-backed corrections to pin functions, power requirements, or component values.
- Reproducible DRC findings and manufacturing-rule fixes.
- Firmware experiments that preserve the documented hardware contract.
- Bench measurements, scope captures, current profiles, storage tests, and acoustic results.
- Documentation corrections that keep English and Chinese entry points aligned.

## Before Opening A Change

1. Read `Docs/README.md` and `Docs/06-Known-Issues.md`.
2. Confirm whether your observation applies to the saved EasyEDA snapshot or to a proposed
   revision.
3. Open an issue before changing pin assignments, power architecture, footprints, or major
   component selections.
4. Never remove a known issue without a matching design change and test evidence.

## Hardware Change Workflow

1. Edit the canonical EasyEDA project in `EDA/DayVault.eprj2`.
2. Synchronize schematic and PCB before exporting review artifacts.
3. Regenerate `EDA/DayVault.netlist.json` through the EasyEDA API.
4. Update the MCU pin map, component map, net map, BOM, and known-issues register in the same
   change.
5. Run electrical and manufacturing DRC and attach the result to the pull request.
6. Do not commit manufacturing files until the revision has a clean, reviewed release gate.

## Pull Requests

Keep each pull request focused. Explain the problem, the exact change, and how it was
verified. For hardware changes, include before/after net or layout evidence. For measured
results, record instruments, supply conditions, firmware revision, test duration, and pass
criteria.

By contributing, you confirm that you have the right to submit the material. No project-wide
open-source license has been selected yet, so discuss licensing with the owner before making
a substantial contribution.
