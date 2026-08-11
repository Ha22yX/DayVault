import unittest

from Mechanical.dayvault_enclosure import (
    EXTERNAL_OPENINGS,
    PRINT_OBJECTS,
    REQUIRED_OBJECTS,
    front_cover_contract,
    rear_shell_contract,
)


class GeneratorContractTests(unittest.TestCase):
    def test_only_microphones_and_usb_are_externally_open(self):
        self.assertEqual(EXTERNAL_OPENINGS, ("U1", "U2", "USB_C"))

    def test_required_objects_are_declared(self):
        self.assertEqual(
            set(REQUIRED_OBJECTS),
            {
                "REF_PCB_30x30",
                "REF_Battery_503040",
                "REF_U1_Forward",
                "REF_U2_Rear",
                "REF_USB1",
                "REF_CARD1",
                "REF_SW1_RESET",
                "REF_SW2_BOOT",
                "REF_PCB_Hole_3p4",
                "DV_FrontCover",
                "DV_RearShell_Clip",
            },
        )

    def test_only_two_objects_are_printed(self):
        self.assertEqual(
            PRINT_OBJECTS, ("DV_FrontCover", "DV_RearShell_Clip")
        )

    def test_front_cover_contract(self):
        contract = front_cover_contract()
        self.assertEqual(contract["outer_size"], (46.0, 40.0, 3.5))
        self.assertEqual(contract["wall"], 1.2)
        self.assertEqual(contract["lip_depth"], 2.3)
        self.assertEqual(contract["locator_d"], 3.0)
        self.assertEqual(contract["u1"], (10.667, 29.305))
        self.assertEqual(contract["u1_external_d"], 2.5)
        self.assertEqual(contract["u1_funnel_d"], 4.5)
        self.assertEqual(contract["snap_count"], 4)

    def test_rear_shell_contract(self):
        contract = rear_shell_contract()
        self.assertEqual(contract["outer_size"], (46.0, 40.0, 11.9))
        self.assertEqual(contract["rear_wall"], 1.2)
        self.assertEqual(contract["battery_rect"], (2.0, 8.0, 44.0, 38.0))
        self.assertEqual(contract["u2"], (35.534, 4.617))
        self.assertEqual(contract["u2_rim_h"], 0.8)
        self.assertEqual(contract["clip_size"], (12.0, 30.0, 1.6))
        self.assertEqual(contract["clip_gap"], 1.0)
        self.assertEqual(contract["snap_recess_count"], 4)


if __name__ == "__main__":
    unittest.main()
