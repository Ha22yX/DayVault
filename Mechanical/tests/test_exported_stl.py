from pathlib import Path
import struct
import unittest


EXPORT_DIR = Path(__file__).resolve().parents[1] / "exports"


def binary_stl_bounds(path: Path):
    data = path.read_bytes()
    if len(data) < 84:
        raise AssertionError(f"STL is too small: {path}")
    triangle_count = struct.unpack_from("<I", data, 80)[0]
    expected_size = 84 + triangle_count * 50
    if len(data) != expected_size:
        raise AssertionError(
            f"STL length mismatch: expected {expected_size}, got {len(data)}"
        )

    minimum = [float("inf")] * 3
    maximum = [float("-inf")] * 3
    offset = 84
    for _ in range(triangle_count):
        values = struct.unpack_from("<12fH", data, offset)
        for vertex_start in (3, 6, 9):
            for axis in range(3):
                value = values[vertex_start + axis]
                minimum[axis] = min(minimum[axis], value)
                maximum[axis] = max(maximum[axis], value)
        offset += 50
    dimensions = tuple(maximum[i] - minimum[i] for i in range(3))
    return triangle_count, dimensions


class ExportedStlTests(unittest.TestCase):
    def test_front_cover_binary_stl(self):
        triangles, dimensions = binary_stl_bounds(
            EXPORT_DIR / "DayVault_FrontCover.stl"
        )
        self.assertGreater(triangles, 500)
        self.assertAlmostEqual(dimensions[0], 46.0, delta=0.05)
        self.assertAlmostEqual(dimensions[1], 40.0, delta=0.05)
        self.assertLessEqual(dimensions[2], 4.2)

    def test_rear_shell_binary_stl(self):
        triangles, dimensions = binary_stl_bounds(
            EXPORT_DIR / "DayVault_RearShell_Clip.stl"
        )
        self.assertGreater(triangles, 800)
        self.assertAlmostEqual(dimensions[0], 46.0, delta=0.05)
        self.assertGreater(dimensions[1], 13.5)
        self.assertLess(dimensions[1], 15.0)
        self.assertAlmostEqual(dimensions[2], 40.0, delta=0.05)


if __name__ == "__main__":
    unittest.main()
