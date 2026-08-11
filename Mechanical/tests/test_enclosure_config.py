import math
import unittest

from Mechanical.enclosure_config import DEFAULT_CONFIG, pcb_to_case, validate_config


class EnclosureConfigTests(unittest.TestCase):
    def test_verified_features_map_without_mirroring(self):
        self.assertEqual(pcb_to_case(2.667, 27.305), (10.667, 29.305))
        self.assertEqual(pcb_to_case(27.534, 2.617), (35.534, 4.617))
        self.assertEqual(pcb_to_case(3.048, 22.606), (11.048, 24.606))

    def test_battery_stays_inside_nominal_walls(self):
        config = DEFAULT_CONFIG
        self.assertGreaterEqual(config.battery.x0 - config.wall, 0.25)
        self.assertGreaterEqual(
            config.body_w - config.wall - config.battery.x1, 0.25
        )
        self.assertGreaterEqual(
            config.body_h - config.wall - config.battery.y1, 0.25
        )

    def test_u2_funnel_clears_battery(self):
        config = DEFAULT_CONFIG
        gap = config.battery.y0 - (
            config.u2_case.y + config.acoustic_funnel_d / 2
        )
        self.assertGreaterEqual(gap, 1.0)

    def test_u1_funnel_clears_locator(self):
        config = DEFAULT_CONFIG
        distance = math.hypot(
            config.u1_case.x - config.locator_case.x,
            config.u1_case.y - config.locator_case.y,
        )
        gap = distance - config.acoustic_funnel_d / 2 - config.locator_d / 2
        self.assertGreaterEqual(gap, 0.8)

    def test_complete_configuration_is_valid(self):
        self.assertEqual(validate_config(DEFAULT_CONFIG), [])

    def test_lightweight_shell_dimensions(self):
        self.assertEqual(DEFAULT_CONFIG.wall, 1.2)
        self.assertEqual(DEFAULT_CONFIG.front_plate_t, 1.2)
        self.assertEqual(DEFAULT_CONFIG.rear_plate_t, 1.2)
        self.assertEqual(DEFAULT_CONFIG.body_d, 15.2)
        self.assertEqual(DEFAULT_CONFIG.clip_t, 1.6)


if __name__ == "__main__":
    unittest.main()
