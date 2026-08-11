from dataclasses import dataclass
import math
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

    @property
    def width(self) -> float:
        return self.x1 - self.x0

    @property
    def height(self) -> float:
        return self.y1 - self.y0


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
    pcb_bottom_z: float = 3.2

    battery: Rect2 = Rect2(2.0, 8.0, 44.0, 38.0)
    battery_t: float = 5.0
    battery_z: float = 8.0
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

    front_plate_t: float = 1.2
    front_lip_depth: float = 2.3
    mating_clearance: float = 0.3
    rear_plate_t: float = 1.2

    usb_opening_w: float = 12.0
    usb_opening_h: float = 5.5
    card_opening_w: float = 15.0
    card_opening_h: float = 3.2

    clip_w: float = 12.0
    clip_length: float = 30.0
    clip_t: float = 1.6
    clip_gap: float = 1.0
    clip_lip: float = 0.4


DEFAULT_CONFIG = EnclosureConfig()


def pcb_to_case(x_mm: float, y_mm: float) -> Point2:
    return Point2(
        round(DEFAULT_CONFIG.pcb_x + x_mm, 3),
        round(DEFAULT_CONFIG.pcb_y + y_mm, 3),
    )


def validate_config(config: EnclosureConfig) -> List[str]:
    errors: List[str] = []
    battery = config.battery

    clearances = {
        "left": battery.x0 - config.wall,
        "right": config.body_w - config.wall - battery.x1,
        "top": battery.y0 - config.wall,
        "bottom": config.body_h - config.wall - battery.y1,
    }
    for side, clearance in clearances.items():
        if clearance < 0.25:
            errors.append(f"battery {side} clearance below 0.25 mm")

    u2_gap = battery.y0 - (
        config.u2_case.y + config.acoustic_funnel_d / 2
    )
    if u2_gap < 1.0:
        errors.append("U2 acoustic funnel too close to battery")

    u1_locator_distance = math.hypot(
        config.u1_case.x - config.locator_case.x,
        config.u1_case.y - config.locator_case.y,
    )
    u1_locator_gap = (
        u1_locator_distance
        - config.acoustic_funnel_d / 2
        - config.locator_d / 2
    )
    if u1_locator_gap < 0.8:
        errors.append("U1 acoustic funnel too close to PCB locator")

    if config.pcb_x < config.wall or config.pcb_y < config.wall:
        errors.append("PCB lies inside the nominal wall envelope")
    if config.pcb_x + config.pcb_w > config.body_w - config.wall:
        errors.append("PCB exceeds right inner wall")
    if config.pcb_y + config.pcb_h > config.body_h - config.wall:
        errors.append("PCB exceeds bottom inner wall")

    if config.clip_lip <= 0 or config.clip_lip >= config.clip_gap:
        errors.append("clip retention lip must remain clear of rear shell")
    elif config.clip_gap - config.clip_lip < 0.6:
        errors.append("clip free-end clearance below 0.6 mm")

    return errors
