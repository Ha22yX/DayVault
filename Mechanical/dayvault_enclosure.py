from __future__ import annotations

import math
from pathlib import Path
import sys
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from Mechanical.enclosure_config import (  # noqa: E402
    DEFAULT_CONFIG,
    EnclosureConfig,
    Point2,
    validate_config,
)

try:
    import bpy
    import bmesh
    from mathutils import Vector
except ImportError:
    bpy = None
    bmesh = None
    Vector = None


PRINT_OBJECTS = ("DV_FrontCover", "DV_RearShell_Clip")
EXTERNAL_OPENINGS = ("U1", "U2", "USB_C")
REQUIRED_OBJECTS = (
    "REF_PCB_30x30",
    "REF_Battery_503040",
    "REF_U1_Forward",
    "REF_U2_Rear",
    "REF_USB1",
    "REF_CARD1",
    "REF_SW1_RESET",
    "REF_SW2_BOOT",
    "REF_PCB_Hole_3p4",
    *PRINT_OBJECTS,
)


def front_cover_contract(config: EnclosureConfig = DEFAULT_CONFIG) -> Dict[str, object]:
    return {
        "outer_size": (
            config.body_w,
            config.body_h,
            config.front_plate_t + config.front_lip_depth,
        ),
        "wall": config.wall,
        "lip_depth": config.front_lip_depth,
        "locator_d": config.locator_d,
        "u1": tuple(config.u1_case),
        "u1_external_d": config.acoustic_external_d,
        "u1_funnel_d": config.acoustic_funnel_d,
        "snap_count": 4,
    }


def rear_shell_contract(config: EnclosureConfig = DEFAULT_CONFIG) -> Dict[str, object]:
    return {
        "outer_size": (
            config.body_w,
            config.body_h,
            round(config.body_d - 3.3, 3),
        ),
        "rear_wall": config.rear_plate_t,
        "battery_rect": (
            config.battery.x0,
            config.battery.y0,
            config.battery.x1,
            config.battery.y1,
        ),
        "u2": tuple(config.u2_case),
        "u2_rim_h": 0.8,
        "clip_size": (config.clip_w, config.clip_length, config.clip_t),
        "clip_gap": config.clip_gap,
        "snap_recess_count": 4,
    }


def require_blender() -> None:
    if bpy is None or bmesh is None:
        raise RuntimeError("This operation requires Blender's Python runtime")


def case_xy_to_blender(
    point: Point2, config: EnclosureConfig = DEFAULT_CONFIG
) -> Tuple[float, float]:
    return (point.x - config.body_w / 2, config.body_h / 2 - point.y)


def _collection(name: str):
    require_blender()
    existing = bpy.data.collections.get(name)
    if existing is not None:
        return existing
    result = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(result)
    return result


def _move_to_collection(obj, collection_name: str) -> None:
    collection = _collection(collection_name)
    for current in tuple(obj.users_collection):
        current.objects.unlink(obj)
    collection.objects.link(obj)


def _material(name: str, rgba: Sequence[float], metallic: float = 0.0):
    require_blender()
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    material.diffuse_color = tuple(rgba)
    material.metallic = metallic
    material.roughness = 0.42
    return material


def _assign_material(obj, material) -> None:
    if obj.data and hasattr(obj.data, "materials"):
        obj.data.materials.clear()
        obj.data.materials.append(material)


def reset_scene() -> None:
    require_blender()
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for datablocks in (
        bpy.data.meshes,
        bpy.data.curves,
        bpy.data.cameras,
        bpy.data.lights,
    ):
        for datablock in tuple(datablocks):
            if datablock.users == 0:
                datablocks.remove(datablock)


def _apply_transform(obj) -> None:
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.select_set(False)


def rounded_box(
    name: str,
    size: Sequence[float],
    location: Sequence[float],
    radius: float = 0.0,
    collection_name: str = "Generated",
):
    require_blender()
    bpy.ops.mesh.primitive_cube_add(location=tuple(location))
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = tuple(size)
    _apply_transform(obj)
    if radius > 0:
        modifier = obj.modifiers.new("EdgeRadius", "BEVEL")
        modifier.width = min(radius, min(size) / 2 - 0.01)
        modifier.segments = 3
        modifier.limit_method = "ANGLE"
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        obj.select_set(False)
    _move_to_collection(obj, collection_name)
    return obj


def cylinder(
    name: str,
    diameter: float,
    depth: float,
    location: Sequence[float],
    vertices: int = 48,
    collection_name: str = "Generated",
):
    require_blender()
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices,
        radius=diameter / 2,
        depth=depth,
        location=tuple(location),
    )
    obj = bpy.context.object
    obj.name = name
    _move_to_collection(obj, collection_name)
    return obj


def cone_cutter(
    name: str,
    bottom_d: float,
    top_d: float,
    depth: float,
    location: Sequence[float],
):
    require_blender()
    bpy.ops.mesh.primitive_cone_add(
        vertices=48,
        radius1=bottom_d / 2,
        radius2=top_d / 2,
        depth=depth,
        location=tuple(location),
    )
    obj = bpy.context.object
    obj.name = name
    _move_to_collection(obj, "Cutters")
    return obj


def boolean_apply(target, tool, operation: str = "DIFFERENCE"):
    require_blender()
    bpy.context.view_layer.objects.active = target
    target.select_set(True)
    modifier = target.modifiers.new(f"{operation}_{tool.name}", "BOOLEAN")
    modifier.operation = operation
    modifier.solver = "EXACT"
    modifier.object = tool
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    target.select_set(False)
    bpy.data.objects.remove(tool, do_unlink=True)
    return target


def union_all(target, additions: Iterable) -> object:
    for addition in additions:
        boolean_apply(target, addition, "UNION")
    return target


def ring_box(
    name: str,
    outer_size: Sequence[float],
    inner_size: Sequence[float],
    location: Sequence[float],
    outer_radius: float,
    collection_name: str = "Generated",
):
    outer = rounded_box(
        name,
        outer_size,
        location,
        outer_radius,
        collection_name,
    )
    inner = rounded_box(
        f"CUT_{name}_Inner",
        inner_size,
        location,
        max(0.2, outer_radius - (outer_size[0] - inner_size[0]) / 2),
        "Cutters",
    )
    return boolean_apply(outer, inner)


def _reference_box(name, size, location, material):
    obj = rounded_box(name, size, location, 0.45, "References")
    _assign_material(obj, material)
    obj.display_type = "WIRE"
    obj.show_in_front = True
    return obj


def _reference_cylinder(name, diameter, depth, location, material):
    obj = cylinder(name, diameter, depth, location, 40, "References")
    _assign_material(obj, material)
    obj.display_type = "WIRE"
    obj.show_in_front = True
    return obj


def build_references(config: EnclosureConfig = DEFAULT_CONFIG) -> Dict[str, object]:
    require_blender()
    pcb_mat = _material("MAT_REF_PCB", (0.08, 0.55, 0.22, 0.40))
    battery_mat = _material("MAT_REF_BATTERY", (0.95, 0.55, 0.12, 0.35))
    front_mat = _material("MAT_REF_FORWARD", (0.10, 0.75, 0.35, 0.55))
    rear_mat = _material("MAT_REF_REAR", (0.90, 0.18, 0.20, 0.55))
    feature_mat = _material("MAT_REF_FEATURE", (0.15, 0.45, 0.90, 0.45))

    pcb_x, pcb_y = case_xy_to_blender(
        Point2(config.pcb_x + config.pcb_w / 2, config.pcb_y + config.pcb_h / 2),
        config,
    )
    battery_center = Point2(
        (config.battery.x0 + config.battery.x1) / 2,
        (config.battery.y0 + config.battery.y1) / 2,
    )
    battery_x, battery_y = case_xy_to_blender(battery_center, config)
    objects = {
        "REF_PCB_30x30": _reference_box(
            "REF_PCB_30x30",
            (config.pcb_w, config.pcb_h, config.pcb_t),
            (pcb_x, pcb_y, config.pcb_bottom_z + config.pcb_t / 2),
            pcb_mat,
        ),
        "REF_Battery_503040": _reference_box(
            "REF_Battery_503040",
            (config.battery.width, config.battery.height, config.battery_t),
            (battery_x, battery_y, config.battery_z + config.battery_t / 2),
            battery_mat,
        ),
    }

    for name, point, material, z_center in (
        ("REF_U1_Forward", config.u1_case, front_mat, 1.8),
        ("REF_U2_Rear", config.u2_case, rear_mat, 10.6),
    ):
        x, y = case_xy_to_blender(point, config)
        objects[name] = _reference_cylinder(
            name, config.acoustic_funnel_d, 4.8, (x, y, z_center), material
        )

    feature_specs = (
        ("REF_USB1", config.usb_case, (10.0, 7.5, 3.4), 6.6),
        ("REF_CARD1", config.card_case, (15.0, 12.0, 1.8), 2.5),
        ("REF_SW1_RESET", config.reset_case, (4.0, 4.0, 2.0), 6.3),
        ("REF_SW2_BOOT", config.boot_case, (4.0, 4.0, 2.0), 6.3),
    )
    for name, point, size, z in feature_specs:
        x, y = case_xy_to_blender(point, config)
        objects[name] = _reference_box(name, size, (x, y, z), feature_mat)

    locator_x, locator_y = case_xy_to_blender(config.locator_case, config)
    objects["REF_PCB_Hole_3p4"] = _reference_cylinder(
        "REF_PCB_Hole_3p4",
        3.4,
        config.pcb_t + 1.0,
        (locator_x, locator_y, config.pcb_bottom_z + config.pcb_t / 2),
        feature_mat,
    )
    return objects


def _cut_edge_openings(part, config: EnclosureConfig) -> None:
    usb_x, _ = case_xy_to_blender(config.usb_case, config)
    usb = rounded_box(
        "CUT_USB_Tunnel",
        (config.usb_opening_w, 10.0, 7.0),
        (usb_x, -config.body_h / 2, 4.6),
        0.8,
        "Cutters",
    )
    boolean_apply(part, usb)


def _front_snap_bumps(config: EnclosureConfig) -> List[object]:
    lip_outer_w = config.body_w - 2 * (config.wall + config.mating_clearance)
    z = config.front_plate_t + config.front_lip_depth - 0.45
    bumps = []
    for side in (-1.0, 1.0):
        for y in (-7.5, 7.5):
            bumps.append(
                rounded_box(
                    f"FrontSnap_{'L' if side < 0 else 'R'}_{y:+g}",
                    (0.9, 4.0, 0.8),
                    (side * (lip_outer_w / 2 - 0.10), y, z),
                    0.30,
                    "Generated",
                )
            )
    return bumps


def _cut_rear_snap_recesses(shell, config: EnclosureConfig) -> None:
    inner_x = config.body_w / 2 - config.wall
    z = config.front_plate_t + config.front_lip_depth - 0.40
    for side in (-1.0, 1.0):
        for y in (-7.5, 7.5):
            recess = rounded_box(
                "CUT_RearSnapRecess",
                (1.2, 4.6, 1.0),
                (side * (inner_x - 0.35), y, z),
                0.30,
                "Cutters",
            )
            boolean_apply(shell, recess)


def build_front_cover(config: EnclosureConfig = DEFAULT_CONFIG):
    require_blender()
    shell_mat = _material("MAT_FrontCover", (0.12, 0.62, 0.34, 1.0))
    plate = rounded_box(
        "DV_FrontCover",
        (config.body_w, config.body_h, config.front_plate_t),
        (0.0, 0.0, config.front_plate_t / 2),
        config.corner_r,
        "PrintParts",
    )

    lip_outer_w = config.body_w - 2 * (config.wall + config.mating_clearance)
    lip_outer_h = config.body_h - 2 * (config.wall + config.mating_clearance)
    lip_wall = 1.2
    lip = ring_box(
        "Front_RegistrationLip",
        (lip_outer_w, lip_outer_h, config.front_lip_depth),
        (
            lip_outer_w - 2 * lip_wall,
            lip_outer_h - 2 * lip_wall,
            config.front_lip_depth + 0.2,
        ),
        (0.0, 0.0, config.front_plate_t + config.front_lip_depth / 2),
        max(0.8, config.corner_r - config.wall - config.mating_clearance),
        "Generated",
    )
    union_all(plate, (lip,))
    union_all(plate, _front_snap_bumps(config))

    locator_x, locator_y = case_xy_to_blender(config.locator_case, config)
    locator = cylinder(
        "Front_PCB_Locator",
        config.locator_d,
        3.1,
        (locator_x, locator_y, config.front_plate_t + 1.35),
        48,
        "Generated",
    )
    bevel = locator.modifiers.new("LocatorTip", "BEVEL")
    bevel.width = 0.35
    bevel.segments = 2
    bpy.context.view_layer.objects.active = locator
    locator.select_set(True)
    bpy.ops.object.modifier_apply(modifier=bevel.name)
    locator.select_set(False)
    union_all(plate, (locator,))

    u1_x, u1_y = case_xy_to_blender(config.u1_case, config)
    external = cylinder(
        "CUT_U1_External",
        config.acoustic_external_d,
        config.front_plate_t + 0.6,
        (u1_x, u1_y, config.front_plate_t / 2),
        48,
        "Cutters",
    )
    boolean_apply(plate, external)
    funnel = cone_cutter(
        "CUT_U1_Funnel",
        config.acoustic_external_d,
        config.acoustic_funnel_d,
        2.2,
        (u1_x, u1_y, config.front_plate_t + 0.9),
    )
    boolean_apply(plate, funnel)
    mesh_seat = cylinder(
        "CUT_U1_MeshSeat",
        config.acoustic_mesh_d,
        0.40,
        (u1_x, u1_y, config.front_plate_t - 0.15),
        48,
        "Cutters",
    )
    boolean_apply(plate, mesh_seat)

    _cut_edge_openings(plate, config)
    plate.name = "DV_FrontCover"
    _assign_material(plate, shell_mat)
    return plate


def _battery_rails(config: EnclosureConfig) -> List[object]:
    rails = []
    for case_y in (9.0, 37.0):
        for case_x in (3.7, 42.3):
            x, y = case_xy_to_blender(Point2(case_x, case_y), config)
            rails.append(
                rounded_box(
                    f"BatteryRail_{case_x:g}_{case_y:g}",
                    (5.2, 1.2, 0.8),
                    (x, y, config.battery_z - 0.4),
                    0.35,
                    "Generated",
                )
            )
    return rails


def _pcb_clamps(config: EnclosureConfig) -> List[object]:
    clamps = []
    specs = (
        ((-18.1, 4.0), (7.8, 5.0, 1.2)),
        ((18.1, 4.0), (7.8, 5.0, 1.2)),
        ((0.0, 17.0), (5.0, 4.0, 1.2)),
    )
    for index, (xy, size) in enumerate(specs, start=1):
        clamps.append(
            rounded_box(
                f"PCBClamp_{index}",
                size,
                (xy[0], xy[1], config.pcb_bottom_z + config.pcb_t + 0.6),
                0.3,
                "Generated",
            )
        )
    return clamps


def _clip_parts(config: EnclosureConfig) -> List[object]:
    clip_inner_z = config.body_d + config.clip_gap
    clip_center_z = clip_inner_z + config.clip_t / 2
    clip_center_y = 1.0
    arm = rounded_box(
        "ChestClip_Arm",
        (config.clip_w, config.clip_length, config.clip_t),
        (0.0, clip_center_y, clip_center_z),
        1.4,
        "Generated",
    )
    root = rounded_box(
        "ChestClip_Root",
        (config.clip_w, 4.0, config.clip_gap + config.clip_t),
        (0.0, 14.0, config.body_d + (config.clip_gap + config.clip_t) / 2),
        1.2,
        "Generated",
    )
    lower_lip = rounded_box(
        "ChestClip_RetentionLip",
        (config.clip_w, 3.5, config.clip_gap + config.clip_lip),
        (
            0.0,
            -13.2,
            config.body_d + (config.clip_gap + config.clip_lip) / 2,
        ),
        1.0,
        "Generated",
    )
    return [arm, root, lower_lip]


def build_rear_shell(config: EnclosureConfig = DEFAULT_CONFIG):
    require_blender()
    shell_mat = _material("MAT_RearShell", (0.68, 0.46, 0.18, 1.0))
    shell_z0 = 3.3
    shell_depth = config.body_d - shell_z0
    shell = rounded_box(
        "DV_RearShell_Clip",
        (config.body_w, config.body_h, shell_depth),
        (0.0, 0.0, shell_z0 + shell_depth / 2),
        config.corner_r,
        "PrintParts",
    )
    cavity_depth = shell_depth - config.rear_plate_t + 0.3
    cavity = rounded_box(
        "CUT_Rear_Cavity",
        (
            config.body_w - 2 * config.wall,
            config.body_h - 2 * config.wall,
            cavity_depth,
        ),
        (
            0.0,
            0.0,
            shell_z0 - 0.15 + cavity_depth / 2,
        ),
        max(0.8, config.corner_r - config.wall),
        "Cutters",
    )
    boolean_apply(shell, cavity)
    _cut_rear_snap_recesses(shell, config)

    union_all(shell, _battery_rails(config))
    union_all(shell, _pcb_clamps(config))
    _cut_edge_openings(shell, config)

    u2_x, u2_y = case_xy_to_blender(config.u2_case, config)
    funnel = cylinder(
        "CUT_U2_Funnel",
        config.acoustic_funnel_d,
        config.body_d - config.pcb_bottom_z + 0.6,
        (u2_x, u2_y, (config.body_d + config.pcb_bottom_z) / 2),
        48,
        "Cutters",
    )
    boolean_apply(shell, funnel)
    external = cylinder(
        "CUT_U2_External",
        config.acoustic_external_d,
        config.rear_plate_t + 1.2,
        (u2_x, u2_y, config.body_d - config.rear_plate_t / 2 + 0.3),
        48,
        "Cutters",
    )
    boolean_apply(shell, external)
    mesh_seat = cylinder(
        "CUT_U2_MeshSeat",
        config.acoustic_mesh_d,
        0.40,
        (u2_x, u2_y, config.body_d - config.rear_plate_t + 0.15),
        48,
        "Cutters",
    )
    boolean_apply(shell, mesh_seat)

    rim_outer = cylinder(
        "U2_AntiBlock_Rim",
        config.acoustic_mesh_d,
        0.9,
        (u2_x, u2_y, config.body_d + 0.35),
        48,
        "Generated",
    )
    rim_inner = cylinder(
        "CUT_U2_RimInner",
        config.acoustic_external_d + 0.6,
        1.2,
        (u2_x, u2_y, config.body_d + 0.4),
        48,
        "Cutters",
    )
    boolean_apply(rim_outer, rim_inner)
    for offset_x in (-3.0, 3.0):
        notch = rounded_box(
            "CUT_U2_RimNotch",
            (2.0, 1.2, 1.2),
            (u2_x + offset_x, u2_y, config.body_d + 0.4),
            0.2,
            "Cutters",
        )
        boolean_apply(rim_outer, notch)
    union_all(shell, (rim_outer,))

    union_all(shell, _clip_parts(config))
    shell.name = "DV_RearShell_Clip"
    _assign_material(shell, shell_mat)
    return shell


def validate_print_mesh(obj) -> List[str]:
    require_blender()
    errors: List[str] = []
    if obj.name not in PRINT_OBJECTS:
        errors.append(f"unexpected print object name: {obj.name}")
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    try:
        if not mesh.polygons:
            errors.append("mesh contains no polygons")
            return errors
        bm = bmesh.new()
        bm.from_mesh(mesh)
        non_manifold = [edge for edge in bm.edges if len(edge.link_faces) != 2]
        if non_manifold:
            errors.append(f"mesh has {len(non_manifold)} non-manifold edges")
        unseen = set(bm.verts)
        island_count = 0
        while unseen:
            island_count += 1
            stack = [unseen.pop()]
            while stack:
                vertex = stack.pop()
                for edge in vertex.link_edges:
                    other = edge.other_vert(vertex)
                    if other in unseen:
                        unseen.remove(other)
                        stack.append(other)
        if island_count != 1:
            errors.append(f"mesh has {island_count} disconnected islands")
        volume = bm.calc_volume(signed=True)
        if volume <= 0.01:
            errors.append("mesh has non-positive volume")
        bm.free()
    finally:
        evaluated.to_mesh_clear()
    return errors


def _export_stl(obj, path: Path) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    if hasattr(bpy.ops.wm, "stl_export"):
        bpy.ops.wm.stl_export(
            filepath=str(path),
            export_selected_objects=True,
            ascii_format=False,
        )
    else:
        bpy.ops.export_mesh.stl(filepath=str(path), use_selection=True, ascii=False)
    obj.select_set(False)


def _camera_target(camera, target: Sequence[float]) -> None:
    direction = Vector(target) - camera.location
    camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def _setup_render() -> None:
    require_blender()
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = 900
    scene.render.resolution_y = 760
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.world.color = (0.08, 0.09, 0.10)
    shading = scene.display.shading
    shading.light = "STUDIO"
    shading.color_type = "MATERIAL"
    shading.show_shadows = True
    shading.show_cavity = True
    shading.cavity_type = "WORLD"
    shading.show_specular_highlight = True
    shading.background_type = "VIEWPORT"
    shading.background_color = (0.055, 0.065, 0.075)

    bpy.ops.object.light_add(type="AREA", location=(35, -28, 46))
    key = bpy.context.object
    key.name = "Render_Key"
    key.data.energy = 700
    key.data.shape = "DISK"
    key.data.size = 28
    bpy.ops.object.light_add(type="AREA", location=(-30, 20, 30))
    fill = bpy.context.object
    fill.name = "Render_Fill"
    fill.data.energy = 450
    fill.data.size = 24
    bpy.ops.object.light_add(type="AREA", location=(0, 30, -10))
    rim = bpy.context.object
    rim.name = "Render_Rim"
    rim.data.energy = 320
    rim.data.size = 20

    bpy.ops.object.camera_add(location=(62, -62, 50))
    camera = bpy.context.object
    camera.name = "Render_Camera"
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 66
    _camera_target(camera, (0, 0, 8))
    scene.camera = camera


def _render_view(path: Path, location: Sequence[float], target=(0, 0, 8)) -> None:
    camera = bpy.data.objects["Render_Camera"]
    camera.location = tuple(location)
    _camera_target(camera, target)
    bpy.context.scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)


def _render_previews(output_dir: Path, front, rear, references) -> None:
    _setup_render()
    for obj in references.values():
        obj.hide_render = True
    _render_view(output_dir / "assembly_front.png", (0, 0, -65))
    _render_view(output_dir / "assembly_rear.png", (0, 0, 72))
    _render_view(output_dir / "assembly_side.png", (70, 0, 25))

    front.location.z -= 7.0
    rear.location.z += 7.0
    _render_view(output_dir / "assembly_exploded.png", (62, -62, 52))
    front.location.z += 7.0
    rear.location.z -= 7.0

    section_objects = []
    for source in (front, rear):
        clone = source.copy()
        clone.data = source.data.copy()
        clone.name = f"SECTION_{source.name}"
        bpy.context.scene.collection.objects.link(clone)
        section_objects.append(clone)

    p1 = case_xy_to_blender(DEFAULT_CONFIG.u1_case, DEFAULT_CONFIG)
    p2 = case_xy_to_blender(DEFAULT_CONFIG.u2_case, DEFAULT_CONFIG)
    dx, dy = p2[0] - p1[0], p2[1] - p1[1]
    length = math.hypot(dx, dy)
    nx, ny = -dy / length, dx / length
    mid_x, mid_y = (p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2
    angle = math.atan2(dy, dx)
    for index, clone in enumerate(section_objects):
        cutter = rounded_box(
            f"CUT_SectionHalf_{index}",
            (100.0, 80.0, 50.0),
            (mid_x + nx * 39.95, mid_y + ny * 39.95, 8.0),
            0.0,
            "Cutters",
        )
        cutter.rotation_euler.z = angle
        boolean_apply(clone, cutter)

    front.hide_render = True
    rear.hide_render = True
    for obj in references.values():
        obj.hide_render = obj.name not in {
            "REF_PCB_30x30",
            "REF_Battery_503040",
            "REF_U1_Forward",
            "REF_U2_Rear",
        }
    _render_view(
        output_dir / "assembly_section.png",
        (mid_x + nx * 68, mid_y + ny * 68, 34),
        (mid_x, mid_y, 8),
    )
    front.hide_render = False
    rear.hide_render = False
    for obj in references.values():
        obj.hide_render = True
    for clone in section_objects:
        bpy.data.objects.remove(clone, do_unlink=True)


def build_all(output_root: str | Path) -> Dict[str, object]:
    require_blender()
    output_root = Path(output_root)
    export_dir = output_root / "exports"
    export_dir.mkdir(parents=True, exist_ok=True)
    config_errors = validate_config(DEFAULT_CONFIG)
    if config_errors:
        raise RuntimeError("; ".join(config_errors))

    reset_scene()
    references = build_references(DEFAULT_CONFIG)
    front = build_front_cover(DEFAULT_CONFIG)
    rear = build_rear_shell(DEFAULT_CONFIG)
    errors = {
        front.name: validate_print_mesh(front),
        rear.name: validate_print_mesh(rear),
    }
    fatal = [f"{name}: {', '.join(items)}" for name, items in errors.items() if items]
    if fatal:
        raise RuntimeError(" | ".join(fatal))

    _export_stl(front, export_dir / "DayVault_FrontCover.stl")
    _export_stl(rear, export_dir / "DayVault_RearShell_Clip.stl")
    _render_previews(export_dir, front, rear, references)
    bpy.ops.wm.save_as_mainfile(filepath=str(output_root / "DayVault_Enclosure.blend"))
    return {
        "front": front,
        "rear": rear,
        "references": references,
        "mesh_errors": errors,
        "export_dir": str(export_dir),
    }


if __name__ == "__main__":
    if bpy is None:
        raise SystemExit("Run this script inside Blender")
    build_all(Path(__file__).resolve().parent)
