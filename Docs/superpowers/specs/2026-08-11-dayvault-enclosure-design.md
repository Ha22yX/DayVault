# DayVault Chest-Clip Enclosure Design

**Date:** 2026-08-11
**Status:** Approved for implementation planning
**Manufacturing process:** FDM, 0.4 mm nozzle
**Assembly:** Two printed parts: front cover and rear shell with integrated clip

## 1. Objective

Create a compact, wearable enclosure for the 30 x 30 mm DayVault PCB and a protected
503040 LiPo battery. The enclosure must preserve the opposite-facing acoustic paths of
the two SPH0655 microphones, expose USB-C, protect the electronics from clothing contact,
and print reliably on a typical FDM printer. The microSD card and both service buttons
remain internal.

The selected mechanical arrangement is an offset stack. The PCB remains upright while
the 42 x 30 x 5 mm battery is rotated horizontally and shifted downward behind the PCB.
This gives the rear microphone a clear acoustic path at the PCB's upper-right corner.

## 2. Coordinate System and Verified PCB Geometry

All XY dimensions use the PCB top-left corner as `(0, 0)` when looking at the PCB top
component layer. X increases to the right and Y increases downward. EasyEDA PCB units
were read through the live API and converted from mil to millimetres.

| Feature | PCB side | PCB coordinate | Mechanical role |
|---|---|---:|---|
| PCB outline | - | 30.000 x 30.000 mm | Primary board envelope |
| U1 SPH0655 | Top | `(2.667, 27.305)` mm | Forward/outward microphone |
| U2 SPH0655 | Bottom | `(27.534, 2.617)` mm | Rear/inward microphone |
| Existing mounting hole | Through | `(3.048, 22.606)` mm, diameter 3.4 mm | Front-cover locating pin |
| USB1 centre | Top | `(15.000, 26.162)` mm | Bottom-edge USB-C opening |
| SW1 RESET | Top | `(16.510, 20.193)` mm | Internal service control |
| SW2 BOOT | Top | `(7.493, 10.287)` mm | Internal service control |
| CARD1 microSD socket | Bottom | `(9.017, 7.112)` mm, rotation 180 degrees | Internal service access |

The SPH0655 is a bottom-port microphone. U1 is mounted on the PCB top side, so its
acoustic path exits through the PCB toward the PCB bottom side. U2 is mounted on the PCB
bottom side, so its acoustic path exits toward the PCB top side. Therefore:

- the PCB bottom side faces outward, away from clothing;
- the PCB top side faces rearward, toward the wearer and clip;
- U1 receives a front-cover sound opening at the lower-left corner;
- U2 receives a rear-shell sound channel at the upper-right corner.

This orientation is a mechanical requirement and must not be mirrored during modelling.

## 3. Overall Envelope and Stack

The nominal body envelope is **46.0 mm wide x 40.0 mm high x 15.2 mm deep**, excluding
the clip projection. External corner radius is 3.0 mm. Enclosure XY coordinates use the
body's upper-left corner as `(0, 0)`, with X right and Y down, matching the PCB coordinate
directions. The Blender modelling origin is the centre of the front outer face; positive Z
points toward the wearer. Convert enclosure XY to Blender XY only after placing all
features in the unambiguous upper-left coordinate system.

The initial stack from the outward face to the clothing side is:

| Layer | Nominal allowance |
|---|---:|
| Front wall | 1.2 mm |
| Front component and acoustic clearance | 2.0 mm |
| PCB | 1.6 mm |
| Rear component clearance | 2.8 mm minimum |
| Battery plus compression-free clearance | 5.5 mm |
| Rear wall | 1.2 mm |
| Local structural ribs | 1.2 mm minimum |

Local ribs may occupy unused XY regions, but no printed feature may press on the LiPo
pouch. The battery cavity must provide at least 0.25 mm clearance on every side and
0.5 mm across thickness. A thin removable foam pad may restrain the battery; rigid
preload against the pouch is forbidden.

## 4. Internal XY Placement

Inside the 46 x 40 mm body:

- place the PCB at enclosure coordinates `x = 8.0..38.0 mm`, `y = 2.0..32.0 mm`;
- place the battery at `x = 2.0..44.0 mm`, `y = 8.0..38.0 mm`;
- keep the U2 acoustic corridor between its global centre `(35.534, 4.617) mm` and the
  rear-shell opening free of the battery, foam, ribs, adhesive, and clip-root material;
- keep wiring in a rounded channel along the lower-left battery edge, with a minimum
  channel cross-section of 2.5 x 2.0 mm.

The battery overlaps the PCB in XY but sits behind the rear component-clearance plane.
It must rest on enclosure rails or a tray, not directly on MCU, connector, or passive
component bodies.

After applying the PCB offset, use these enclosure coordinates for modelling:

| Feature | Enclosure coordinate |
|---|---:|
| U1 forward microphone | `(10.667, 29.305)` mm |
| U2 rear microphone | `(35.534, 4.617)` mm |
| 3.4 mm PCB locating hole | `(11.048, 24.606)` mm |
| USB1 centre | `(23.000, 28.162)` mm |
| SW1 RESET | `(24.510, 22.193)` mm |
| SW2 BOOT | `(15.493, 12.287)` mm |
| CARD1 centre | `(17.017, 9.112)` mm |

## 5. PCB Retention and Enclosure Closure

Use the existing 3.4 mm PCB hole as a locating feature rather than a through-case screw
boss. A 3.0 mm diameter pin rises from the front cover and engages the PCB hole with
0.2 mm radial clearance. Engagement into the board hole is 0.8 to 1.0 mm above the PCB
bottom surface. The pin tip receives a 0.4 mm chamfer.

Three additional perimeter ledges locate the remaining PCB edges. When the enclosure is
closed, rear-shell pads clamp only bare PCB edge regions. The pads must not load solder
joints or components.

The front cover uses a continuous 2.5 mm-deep registration lip and four short snap hooks,
two on each long side. FDM assembly clearance is 0.30 mm per mating side. Hooks are
1.2 mm thick at the flexible section with a 0.6 mm retention undercut and at least 2.5 mm
root fillets. The default material is PETG; PLA is acceptable for dimensional prototypes
but not preferred for the final clip or repeated snap opening.

## 6. Acoustic Features

Each microphone uses a short, isolated acoustic passage:

- external sound opening: 2.5 mm diameter;
- internal funnel around the PCB acoustic-hole cluster: 4.5 mm diameter;
- mesh seat: 6.0 mm diameter x 0.35 mm deep;
- minimum wall around the passage: 1.2 mm.

The U1 passage is normal to the front cover at global enclosure coordinate
`(10.667, 29.305) mm`. It opens toward the room and must not be obstructed by the USB
tunnel or locating pin. Its exterior remains flush so the front cover can print flat on
its outer face.

The U2 passage starts at global enclosure coordinate `(35.534, 4.617) mm`, crosses the
rear component space without touching the battery, and exits the rear shell at the same
XY location. A 0.8 mm-high anti-blocking rim with two opposing radial air notches maintains
an air gap against clothing. The clip root must remain outside a 7.0 mm diameter keep-out
around this passage.

The two acoustic cavities must not share a printed internal chamber. A continuous wall
or the PCB itself separates them to reduce acoustic leakage between front and rear.

## 7. Connector and Service Access

### USB-C

Provide a bottom-edge tunnel aligned with USB1. The external opening is 12.0 mm wide x
5.5 mm high with 0.8 mm corner radius. The tunnel reaches the receptacle shell without a
step that can stop a USB-C plug. Maintain at least 0.5 mm clearance around the connector
metal shell and verify insertion with a plug body at least 10 mm wide.

### microSD

Do not create an external microSD slot. Card replacement requires opening the enclosure.
This leaves exactly three external passages: U1, U2, and USB-C.

### RESET and BOOT

SW1 and SW2 are service controls, not daily controls. They remain internal to avoid
accidental reset, false DFU entry, and additional clothing-facing openings. Access
requires opening the enclosure and lifting the battery from its removable restraint.
The enclosure must remain reopenable without destroying snap hooks. The physical BOOT
and RESET recovery path is retained; no firmware or option-byte change is part of this
mechanical design.

## 8. Integrated Chest Clip

The clip is part of the rear shell and is centred horizontally while staying outside the
U2 acoustic keep-out. Nominal clip geometry is:

- width: 12.0 mm;
- free length from root: 30.0 mm;
- flexible thickness: 1.6 mm;
- initial clothing gap: 1.0 mm;
- lower retention lip: 1.5 mm;
- root fillet radius: 2.5 mm;
- end radius: 3.0 mm.

The clip bends away from the rear shell when attaching the device. Its lower end must not
cover either microphone opening. Print-orientation tests must confirm that layer lines run
along the clip length rather than across the flexible root. Local support beneath the clip
is permitted and must be removable without thinning the flexible section.

## 9. FDM Rules

- Nominal wall thickness: 1.2 mm, equivalent to three 0.4 mm extrusion widths.
- Minimum structural rib: 1.2 mm.
- Minimum free-standing pin diameter: 2.0 mm; the PCB locator is 3.0 mm.
- Mating clearance: 0.30 mm per side for the first prototype.
- Unsupported overhang target: 45 degrees or less, except the supported clip underside.
- Internal corners receive at least 0.8 mm fillets where they affect stress or printing.
- No printed support may be trapped inside the acoustic channels or USB tunnel.

The front cover prints with its outer face on the build plate. The rear shell prints in
the orientation that gives the clip root continuous longitudinal layers; use local tree
supports under the clip if required. PETG is the final-material baseline.

## 10. Deliverables

The Blender source will contain named, dimensioned objects for:

1. `DV_FrontCover`
2. `DV_RearShell_Clip`
3. `REF_PCB_30x30`
4. `REF_Battery_503040`
5. reference keep-outs for U1, U2, USB1, CARD1, SW1, SW2, and the 3.4 mm PCB hole

Export separate binary STL files for the two printed parts and keep reference objects out
of STL export. Also export a dimensioned assembly preview image.

## 11. Verification and Acceptance

Before declaring the model complete:

- confirm the front U1 and rear U2 openings are not mirrored;
- verify the 30 x 30 mm PCB and 42 x 30 x 5 mm battery fit with the stated clearances;
- run Blender non-manifold checks on both printable parts;
- confirm each STL is one watertight body with outward normals;
- confirm minimum wall and snap-hook thicknesses;
- section the assembly through both microphones, USB-C, battery, and clip root;
- verify the USB plug insertion path is unobstructed and the microSD card is reachable
  after opening the enclosure;
- verify the battery does not intersect components, locator features, or the U2 channel;
- render front, rear, side, exploded, and section views for review;
- print a first prototype at 0.20 mm layer height and validate fit before tightening
  tolerances.

The first prototype is accepted when the PCB and battery assemble without force, both
sound passages remain open, USB-C is usable, the internal microSD card remains serviceable,
the case survives repeated opening, and the clip holds the device on normal shirt fabric
without sealing the rear microphone.
