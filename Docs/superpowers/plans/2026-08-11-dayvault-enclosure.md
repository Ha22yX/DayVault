# DayVault Enclosure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generate, validate, and export a compact two-part FDM-printable DayVault chest-clip enclosure with correctly oriented front and rear microphone paths.

**Architecture:** A pure-Python geometry contract owns all verified PCB coordinates, enclosure dimensions, offsets, and clearance checks. A Blender Python generator consumes that contract to build named reference objects, the front cover, and the rear shell with integrated clip using deterministic primitive and boolean operations. A separate validation script checks the contract without Blender, while Blender-side checks verify watertight printable meshes before saving the `.blend`, STL files, and preview renders.

**Tech Stack:** Python 3 standard library, Blender Python API (`bpy`, `bmesh`, `mathutils`), Blender MCP, `unittest`, binary STL.

## Global Constraints

- Do not modify STM32 firmware, option bytes, boot configuration, DFU behavior, or USB identifiers.
- PCB is exactly 30.0 x 30.0 mm.
- Battery reference is exactly 42.0 x 30.0 x 5.0 mm with at least 0.25 mm XY and 0.5 mm Z clearance.
- Body target is 46.0 x 40.0 x 15.2 mm excluding clip projection.
- U1 at PCB `(2.667, 27.305)` mm is the lower-left forward microphone and opens through the front cover.
- U2 at PCB `(27.534, 2.617)` mm is the upper-right rear microphone and opens through the rear shell.
- Existing 3.4 mm PCB hole at `(3.048, 22.606)` mm receives a 3.0 mm front-cover locator.
- FDM baseline is a 0.4 mm nozzle, 1.2 mm nominal walls, 0.30 mm mating clearance per side, and PETG final material.
- External openings are limited to U1, U2, and USB-C; microSD, RESET, and BOOT require opening the enclosure.
- Printable output consists of exactly two parts: `DV_FrontCover` and `DV_RearShell_Clip`.

---

### Task 1: Geometry Contract and Coordinate Tests

**Files:**
- Create: `Mechanical/enclosure_config.py`
- Create: `Mechanical/tests/test_enclosure_config.py`

**Interfaces:**
- Produces: `Point2`, `Rect2`, `EnclosureConfig`, `DEFAULT_CONFIG`, `pcb_to_case(x_mm, y_mm)`, and `validate_config(config)`.
- Consumes: Exact measurements from `Docs/superpowers/specs/2026-08-11-dayvault-enclosure-design.md`.

- [ ] **Step 1: Write coordinate and clearance tests**

```python
import math
import unittest

from Mechanical.enclosure_config import DEFAULT_CONFIG, pcb_to_case, validate_config


class EnclosureConfigTests(unittest.TestCase):
    def test_verified_features_map_without_mirroring(self):
        self.assertEqual(pcb_to_case(2.667, 27.305), (10.667, 29.305))
        self.assertEqual(pcb_to_case(27.534, 2.617), (35.534, 4.617))
        self.assertEqual(pcb_to_case(3.048, 22.606), (11.048, 24.606))

    def test_battery_stays_inside_nominal_walls(self):
        c = DEFAULT_CONFIG
        self.assertGreaterEqual(c.battery.x0 - c.wall, 0.25)
        self.assertGreaterEqual(c.body_w - c.wall - c.battery.x1, 0.25)
        self.assertGreaterEqual(c.body_h - c.wall - c.battery.y1, 0.25)

    def test_u2_funnel_clears_battery(self):
        c = DEFAULT_CONFIG
        gap = c.battery.y0 - (c.u2_case.y + c.acoustic_funnel_d / 2)
        self.assertGreaterEqual(gap, 1.0)

    def test_u1_funnel_clears_locator(self):
        c = DEFAULT_CONFIG
        d = math.hypot(c.u1_case.x - c.locator_case.x,
                       c.u1_case.y - c.locator_case.y)
        self.assertGreaterEqual(
            d - c.acoustic_funnel_d / 2 - c.locator_d / 2, 0.8)

    def test_complete_configuration_is_valid(self):
        self.assertEqual(validate_config(DEFAULT_CONFIG), [])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify the module is missing**

Run: `python -m unittest Mechanical.tests.test_enclosure_config -v`

Expected: FAIL with `ModuleNotFoundError: No module named 'Mechanical.enclosure_config'`.

- [ ] **Step 3: Implement the immutable geometry contract**

```python
from dataclasses import dataclass
from typing import List, NamedTuple


class Point2(NamedTuple):
    x: float
    y: float


@dataclass(frozen=True)
class Rect2:
    x0: float
    y0: float
    x1: float
    y1: float


@dataclass(frozen=True)
class EnclosureConfig:
    body_w: float = 46.0
    body_h: float = 40.0
    body_d: float = 15.2
    wall: float = 1.2
    corner_r: float = 3.0
    pcb_x: float = 8.0
    pcb_y: float = 2.0
    pcb_w: float = 30.0
    pcb_h: float = 30.0
    pcb_t: float = 1.6
    battery: Rect2 = Rect2(2.0, 8.0, 44.0, 38.0)
    battery_t: float = 5.0
    battery_z_clearance: float = 0.5
    u1_case: Point2 = Point2(10.667, 29.305)
    u2_case: Point2 = Point2(35.534, 4.617)
    locator_case: Point2 = Point2(11.048, 24.606)
    usb_case: Point2 = Point2(23.000, 28.162)
    card_case: Point2 = Point2(17.017, 9.112)
    reset_case: Point2 = Point2(24.510, 22.193)
    boot_case: Point2 = Point2(15.493, 12.287)
    acoustic_external_d: float = 2.5
    acoustic_funnel_d: float = 4.5
    acoustic_mesh_d: float = 6.0
    locator_d: float = 3.0


DEFAULT_CONFIG = EnclosureConfig()


def pcb_to_case(x_mm: float, y_mm: float) -> Point2:
    return Point2(round(DEFAULT_CONFIG.pcb_x + x_mm, 3),
                  round(DEFAULT_CONFIG.pcb_y + y_mm, 3))


def validate_config(c: EnclosureConfig) -> List[str]:
    errors = []
    if c.battery.x0 - c.wall < 0.25:
        errors.append("battery left clearance below 0.25 mm")
    if c.body_w - c.wall - c.battery.x1 < 0.25:
        errors.append("battery right clearance below 0.25 mm")
    if c.body_h - c.wall - c.battery.y1 < 0.25:
        errors.append("battery bottom clearance below 0.25 mm")
    u2_gap = c.battery.y0 - (c.u2_case.y + c.acoustic_funnel_d / 2)
    if u2_gap < 1.0:
        errors.append("U2 acoustic funnel too close to battery")
    return errors
```

- [ ] **Step 4: Run the geometry tests**

Run: `python -m unittest Mechanical.tests.test_enclosure_config -v`

Expected: 5 tests, all PASS.

- [ ] **Step 5: Commit the geometry contract**

```bash
git add Mechanical/enclosure_config.py Mechanical/tests/test_enclosure_config.py
git commit -m "feat(mechanical): define enclosure geometry contract"
```

### Task 2: Blender Generator Foundation and Reference Assembly

**Files:**
- Create: `Mechanical/dayvault_enclosure.py`
- Create: `Mechanical/tests/test_generated_manifest.py`

**Interfaces:**
- Consumes: `DEFAULT_CONFIG` from `Mechanical.enclosure_config`.
- Produces: `case_xy_to_blender(point)`, `rounded_box(...)`, `boolean_apply(...)`, `build_references(config)`, and object names `REF_PCB_30x30`, `REF_Battery_503040`, `REF_U1_Forward`, `REF_U2_Rear`, `REF_USB1`, `REF_CARD1`, `REF_SW1_RESET`, `REF_SW2_BOOT`, `REF_PCB_Hole_3p4`.

- [ ] **Step 1: Write a manifest test for required outputs and reference names**

```python
import unittest

from Mechanical.dayvault_enclosure import REQUIRED_OBJECTS, PRINT_OBJECTS


class GeneratedManifestTests(unittest.TestCase):
    def test_required_reference_objects_are_declared(self):
        self.assertEqual(
            set(REQUIRED_OBJECTS),
            {
                "REF_PCB_30x30", "REF_Battery_503040", "REF_U1_Forward",
                "REF_U2_Rear", "REF_USB1", "REF_CARD1", "REF_SW1_RESET",
                "REF_SW2_BOOT", "REF_PCB_Hole_3p4",
                "DV_FrontCover", "DV_RearShell_Clip",
            },
        )

    def test_only_two_objects_are_exported_for_printing(self):
        self.assertEqual(PRINT_OBJECTS, ("DV_FrontCover", "DV_RearShell_Clip"))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the manifest test to verify failure**

Run: `python -m unittest Mechanical.tests.test_generated_manifest -v`

Expected: FAIL because `Mechanical.dayvault_enclosure` does not exist.

- [ ] **Step 3: Implement import-safe declarations and Blender-only helpers**

The module must import without `bpy` so host-side tests work. Use this boundary:

```python
try:
    import bpy
    import bmesh
    from mathutils import Vector
except ImportError:
    bpy = None
    bmesh = None
    Vector = None

PRINT_OBJECTS = ("DV_FrontCover", "DV_RearShell_Clip")
REQUIRED_OBJECTS = (
    "REF_PCB_30x30", "REF_Battery_503040", "REF_U1_Forward",
    "REF_U2_Rear", "REF_USB1", "REF_CARD1", "REF_SW1_RESET",
    "REF_SW2_BOOT", "REF_PCB_Hole_3p4", *PRINT_OBJECTS,
)


def require_blender():
    if bpy is None:
        raise RuntimeError("This operation requires Blender's Python runtime")
```

Implement `case_xy_to_blender()` as `x = case_x - body_w / 2` and
`y = body_h / 2 - case_y`. Build reference boxes with translucent materials and place
the PCB from Z = 3.6 to 5.2 mm, the battery from Z = 8.3 to 13.3 mm, and cylindrical
keep-outs through the full 16 mm body depth.

- [ ] **Step 4: Run host-side tests**

Run: `python -m unittest Mechanical.tests.test_generated_manifest Mechanical.tests.test_enclosure_config -v`

Expected: 7 tests, all PASS without importing Blender.

- [ ] **Step 5: Run the generator in Blender and inspect reference placement**

Run in Blender MCP:

```python
import importlib.util
spec = importlib.util.spec_from_file_location(
    "dayvault_enclosure",
    r"C:\Users\Administrator\Desktop\DayVault\Mechanical\dayvault_enclosure.py",
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.reset_scene()
module.build_references(module.DEFAULT_CONFIG)
```

Expected: Nine `REF_*` objects appear; U1 is lower-left on the outward side and U2 is
upper-right on the clothing side.

- [ ] **Step 6: Commit the generator foundation**

```bash
git add Mechanical/dayvault_enclosure.py Mechanical/tests/test_generated_manifest.py
git commit -m "feat(mechanical): add Blender reference assembly"
```

### Task 3: Front Cover Geometry

**Files:**
- Modify: `Mechanical/dayvault_enclosure.py`
- Create: `Mechanical/tests/test_part_contract.py`

**Interfaces:**
- Consumes: `rounded_box`, `boolean_apply`, `case_xy_to_blender`, and `DEFAULT_CONFIG`.
- Produces: `build_front_cover(config) -> bpy.types.Object` named `DV_FrontCover`.

- [ ] **Step 1: Write host-side dimensional contract tests**

```python
import unittest

from Mechanical.dayvault_enclosure import front_cover_contract


class PartContractTests(unittest.TestCase):
    def test_front_cover_contract(self):
        c = front_cover_contract()
        self.assertEqual(c["outer_size"], (46.0, 40.0, 3.5))
        self.assertEqual(c["wall"], 1.6)
        self.assertEqual(c["lip_depth"], 2.5)
        self.assertEqual(c["locator_d"], 3.0)
        self.assertEqual(c["u1"], (10.667, 29.305))
        self.assertEqual(c["u1_external_d"], 2.5)
        self.assertEqual(c["u1_funnel_d"], 4.5)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the contract test to verify failure**

Run: `python -m unittest Mechanical.tests.test_part_contract -v`

Expected: FAIL because `front_cover_contract` is missing.

- [ ] **Step 3: Build the front plate, registration lip, locator, and cut-outs**

Implement a 46 x 40 x 1.2 mm rounded front plate. Add a hollow
registration lip from Z = 1.6 to 4.1 mm with 0.30 mm per-side clearance inside the rear
shell. Add the 3.0 mm locator at enclosure `(11.048, 24.606)` with a 0.4 mm tip chamfer.

Subtract:

- U1 2.5 mm external passage at `(10.667, 29.305)`;
- U1 4.5 mm internal funnel and 6.0 x 0.35 mm mesh seat;
- the front half of the 12.0 x 5.5 mm USB-C bottom tunnel centred at X = 23.0 mm;

Add two snap receivers per long side. Apply all booleans and join positive geometry into
one object named `DV_FrontCover`.

- [ ] **Step 4: Run contract and geometry tests**

Run: `python -m unittest discover -s Mechanical/tests -v`

Expected: all tests PASS.

- [ ] **Step 5: Build in Blender and section through U1 and the locator**

Run `build_front_cover(DEFAULT_CONFIG)`, then use Blender MCP screenshots to verify U1 is
lower-left, the funnel reaches the PCB acoustic-hole reference, and the locator does not
intersect the U1 funnel.

- [ ] **Step 6: Commit the front cover**

```bash
git add Mechanical/dayvault_enclosure.py Mechanical/tests/test_part_contract.py
git commit -m "feat(mechanical): model DayVault front cover"
```

### Task 4: Rear Shell, Battery Tray, Acoustic Channel, and Clip

**Files:**
- Modify: `Mechanical/dayvault_enclosure.py`
- Modify: `Mechanical/tests/test_part_contract.py`

**Interfaces:**
- Consumes: `DEFAULT_CONFIG`, reference keep-outs, and front-cover mating dimensions.
- Produces: `build_rear_shell(config) -> bpy.types.Object` named `DV_RearShell_Clip` and `rear_shell_contract()`.

- [ ] **Step 1: Extend the dimensional contract test**

```python
    def test_rear_shell_contract(self):
        c = rear_shell_contract()
        self.assertEqual(c["outer_size"], (46.0, 40.0, 11.9))
        self.assertEqual(c["rear_wall"], 1.2)
        self.assertEqual(c["battery_rect"], (2.0, 8.0, 44.0, 38.0))
        self.assertEqual(c["u2"], (35.534, 4.617))
        self.assertEqual(c["u2_rim_h"], 0.8)
        self.assertEqual(c["clip_size"], (12.0, 30.0, 1.6))
        self.assertEqual(c["clip_gap"], 1.0)
```

- [ ] **Step 2: Run the rear contract test to verify failure**

Run: `python -m unittest Mechanical.tests.test_part_contract.PartContractTests.test_rear_shell_contract -v`

Expected: FAIL because `rear_shell_contract` is missing.

- [ ] **Step 3: Build the hollow rear shell and mating features**

Create a rounded rear shell spanning Z = 3.3 to 15.2 mm. Retain a 1.2 mm rear wall and
1.2 mm perimeter walls. Add the matching registration groove, four snap recesses,
three PCB edge pads, and battery support rails at Z = 8.0 mm. Rails support only the pouch
edges and preserve 0.25 mm XY and 0.5 mm Z clearance.

Subtract the complete USB-C bottom tunnel through all blocking rear walls. Do not create
external microSD, RESET, or BOOT holes.

- [ ] **Step 4: Add the U2 channel and clothing stand-off**

At `(35.534, 4.617)`, subtract the 4.5 mm internal funnel and 2.5 mm rear opening. Add a
6.0 x 0.35 mm mesh seat. Union a 0.8 mm-high annular rim with two 1.2 mm radial notches.
Verify the battery starts at Y = 8.0 mm and does not touch the funnel.

- [ ] **Step 5: Add the integrated clip**

Create a centred 12.0 x 30.0 x 1.6 mm clip on the rear face with a 1.0 mm shell gap,
2.5 mm root fillet, 3.0 mm rounded end, and 1.5 mm lower retention lip. Keep the clip root
outside the 7.0 mm U2 keep-out. Union the clip root to the shell and keep the flexible arm
separated from the shell over its free length.

- [ ] **Step 6: Run tests and inspect assembly in Blender**

Run: `python -m unittest discover -s Mechanical/tests -v`

Expected: all host tests PASS. In Blender, show front, rear, right-side, and section views.
The battery reference must not intersect the U2 channel, rails, PCB, or clip root.

- [ ] **Step 7: Commit the rear shell**

```bash
git add Mechanical/dayvault_enclosure.py Mechanical/tests/test_part_contract.py
git commit -m "feat(mechanical): model rear shell and chest clip"
```

### Task 5: Mesh Validation, Export, and Preview Assets

**Files:**
- Modify: `Mechanical/dayvault_enclosure.py`
- Create: `Mechanical/README.md`
- Create: `Mechanical/exports/.gitkeep`

**Interfaces:**
- Consumes: `build_front_cover`, `build_rear_shell`, `build_references`.
- Produces: `validate_print_mesh(obj)`, `build_all(output_dir)`,
  `Mechanical/DayVault_Enclosure.blend`, `Mechanical/exports/DayVault_FrontCover.stl`,
  `Mechanical/exports/DayVault_RearShell_Clip.stl`, and PNG assembly views.

- [ ] **Step 1: Implement watertight and object-manifest validation**

`validate_print_mesh(obj)` must evaluate the mesh, build a `bmesh`, and return errors when:

- an edge has other than exactly two linked faces;
- the object has zero polygons or non-positive volume;
- dimensions exceed the allowed part envelope;
- the object name is not one of `PRINT_OBJECTS`.

`build_all()` must refuse export unless `validate_config(DEFAULT_CONFIG)` and both mesh
validations return no errors.

- [ ] **Step 2: Implement deterministic STL and Blender export**

Delete prior generated objects by name, rebuild the full assembly, save the Blender source
to `Mechanical/DayVault_Enclosure.blend`, and export only one selected print object per STL.
Use Blender 4.x `bpy.ops.wm.stl_export` when available and fall back to
`bpy.ops.export_mesh.stl` for older Blender versions.

- [ ] **Step 3: Add camera, section, and exploded preview rendering**

Create neutral materials, an orthographic camera, and area lights. Render at least:

- `Mechanical/exports/assembly_front.png`;
- `Mechanical/exports/assembly_rear.png`;
- `Mechanical/exports/assembly_side.png`;
- `Mechanical/exports/assembly_exploded.png`;
- `Mechanical/exports/assembly_section.png`.

The section view must cross both microphone centres and show U1 exiting front-left and U2
exiting rear-right.

- [ ] **Step 4: Document regeneration and print settings**

`Mechanical/README.md` must state the Blender script command, object names, dimensions,
PETG baseline, 0.20 mm layer height, 3 walls, 25-35% infill, support guidance for the clip,
and first-print fit checks for PCB, battery, USB-C, microphone paths, and snaps.

- [ ] **Step 5: Generate and verify deliverables in Blender**

Run `build_all(r"C:\Users\Administrator\Desktop\DayVault\Mechanical")` through Blender
MCP. Confirm both mesh validators return empty lists, inspect viewport screenshots, inspect
rendered PNGs, and verify both STL files are non-empty.

- [ ] **Step 6: Run final host checks**

Run:

```bash
python -m unittest discover -s Mechanical/tests -v
git diff --check
```

Expected: all tests PASS and `git diff --check` prints nothing.

- [ ] **Step 7: Commit generated mechanical deliverables**

```bash
git add Mechanical Docs/superpowers/plans/2026-08-11-dayvault-enclosure.md
git commit -m "feat(mechanical): add printable DayVault enclosure"
```
