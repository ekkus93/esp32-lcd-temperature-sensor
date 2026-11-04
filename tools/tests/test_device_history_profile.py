"""Host regressions for the accelerated MCU history profiling window."""
import importlib.util
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1] / "device_history_profile.py"
SPEC = importlib.util.spec_from_file_location("device_history_profile", TOOL)
profile = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(profile)


class SimulatedHistoryWindowTests(unittest.TestCase):
    def test_window_covers_accelerated_history_until_profile_completion(self):
        start, end = profile.simulated_history_window(60_000, 120)
        self.assertEqual(start, 0)
        # 120 seconds of real time represent 2,400 seconds of simulated uptime.
        self.assertEqual(end, 3_900_000)

    def test_long_running_device_window_is_clamped_to_query_limit(self):
        start, end = profile.simulated_history_window(3_000_000_000, 120)
        self.assertEqual(end - start, profile.MAX_QUERY_RANGE_MS)
        self.assertEqual(end, ((3_000_000_000 + 120_000) * 20) + 300_000)


if __name__ == "__main__":
    unittest.main()
