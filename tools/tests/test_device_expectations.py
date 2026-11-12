import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("device_expectations", ROOT / "device_expectations.py")
expectations = importlib.util.module_from_spec(spec)
spec.loader.exec_module(expectations)
expected = expectations.expected_sensor_count


class ExpectedSensorCountTests(unittest.TestCase):
    def test_a_board_needs_the_count_given(self):
        with self.assertRaises(ValueError):
            expected(False, None)

    def test_the_given_count_is_the_only_one_accepted(self):
        self.assertEqual(expected(False, 4), 4)
        self.assertEqual(expected(False, 0), 0)

    def test_out_of_range_counts_are_refused(self):
        for count in (-1, 6):
            with self.assertRaises(ValueError):
                expected(False, count)

    def test_simulation_is_always_five(self):
        self.assertEqual(expected(True, None), 5)
        self.assertEqual(expected(True, 5), 5)
        with self.assertRaises(ValueError):
            expected(True, 3)


if __name__ == "__main__":
    unittest.main()
