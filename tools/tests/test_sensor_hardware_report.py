"""Host-only regression for numeric sensor diagnostics; no serial device."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1] / "sensor_hardware_report.py"
spec = importlib.util.spec_from_file_location("sensor_hardware_report", TOOL)
sensor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sensor)


def line(cycles, probes, valid, invalid, faults, latency=750, lifetime_max=None):
    return (
        f"I (12345) monitor: SENSOR_DIAG cycles={cycles} last_probes={probes} "
        f"max_probes={probes} valid={valid} invalid={invalid} "
        f"fault_cycles={faults} last_read_ms={latency} "
        f"max_read_ms={latency if lifetime_max is None else lifetime_max}"
    )


class SensorDiagnosticsReportTests(unittest.TestCase):
    def test_parse_discards_unrelated_and_partial_lines(self):
        self.assertIsNone(sensor.parse_line("admin_password=never-print"))
        self.assertIsNone(sensor.parse_line("SENSOR_DIAG cycles=3 invalid=0"))
        self.assertEqual(sensor.parse_line(line(2, 5, 10, 0, 0))["last_probes"], 5)

    def test_clean_and_failed_qualification(self):
        samples = [sensor.parse_line(line(10, 5, 50, 0, 0)),
                   sensor.parse_line(line(11, 5, 55, 0, 0))]
        result = sensor.report(samples, expected_probes=5, max_invalid=0, max_faults=0)
        self.assertTrue(result["passed"])
        self.assertFalse(result["hardware_verified"])
        self.assertEqual(result["observed_cycles"], 1)
        self.assertFalse(sensor.report(samples, expected_probes=4)["passed"])
        damaged = [samples[0], sensor.parse_line(line(11, 4, 52, 3, 1))]
        self.assertFalse(sensor.report(damaged, expected_probes=5,
                                       max_invalid=0, max_faults=0)["passed"])

    def test_reset_and_missing_data_fail(self):
        first = sensor.parse_line(line(15, 1, 15, 2, 1))
        second = sensor.parse_line(line(1, 1, 1, 0, 0))
        self.assertFalse(sensor.report([first, second])["passed"])
        self.assertFalse(sensor.report([], min_records=2)["passed"])

    def test_detects_unobserved_latency_spike(self):
        # Last readings are fast, but a previous acquisition was too slow.
        samples = [sensor.parse_line(line(10, 5, 50, 0, 0, 200, 1100)),
                   sensor.parse_line(line(11, 5, 55, 0, 0, 180, 1100))]
        result = sensor.report(samples, expected_probes=5, max_read_ms=1000)
        self.assertFalse(result["passed"])
        self.assertEqual(result["max_observed_read_ms"], 1100)
        self.assertIn("sampling latency exceeded limit", result["errors"])

    def test_rejects_stagnant_and_inconsistent_counters(self):
        stagnant = [sensor.parse_line(line(5, 1, 5, 0, 0)),
                    sensor.parse_line(line(5, 1, 5, 0, 0))]
        self.assertIn("no new acquisition cycles observed",
                      sensor.report(stagnant)["errors"])
        inconsistent = sensor.parse_line(line(6, 1, 6, 0, 0, 900, 500))
        self.assertIn("read latency exceeds cumulative maximum",
                      sensor.report([stagnant[0], inconsistent])["errors"])

    def test_detects_missing_valid_reads_and_short_acquisition(self):
        # Probe discovery alone does not demonstrate successful conversions.
        missing = [sensor.parse_line(line(10, 5, 0, 0, 0)),
                   sensor.parse_line(line(11, 5, 0, 0, 0))]
        result = sensor.report(missing, expected_probes=5)
        self.assertIn("no valid readings observed", result["errors"])
        self.assertEqual(result["valid_delta"], 0)
        # A full five-probe qualification must count five valid conversions
        # per cycle, not merely have five probes in the final snapshot.
        partial = [sensor.parse_line(line(10, 5, 50, 0, 0)),
                   sensor.parse_line(line(12, 5, 59, 0, 0))]
        self.assertTrue(sensor.report(partial, expected_probes=5)["passed"])
        self.assertIn(
            "valid readings per acquisition cycle below minimum",
            sensor.report(partial, expected_probes=5,
                          min_valid_per_cycle=5)["errors"])
        complete = [sensor.parse_line(line(10, 5, 50, 0, 0)),
                    sensor.parse_line(line(12, 5, 60, 0, 0))]
        self.assertTrue(sensor.report(complete, expected_probes=5,
                                      min_valid_per_cycle=5)["passed"])

    def test_rejects_impossible_reading_counters(self):
        first = sensor.parse_line(line(10, 5, 50, 0, 0))
        impossible = sensor.parse_line(line(11, 5, 56, 0, 0))
        self.assertIn("readings exceed five-probe acquisition capacity",
                      sensor.report([first, impossible])["errors"])
        excess = sensor.parse_line(line(10, 5, 51, 0, 0))
        self.assertIn("cumulative readings exceed acquisition capacity",
                      sensor.report([excess])["errors"])
        stalled = sensor.parse_line(line(10, 5, 51, 0, 0))
        self.assertIn("readings changed without acquisition cycle",
                      sensor.report([first, stalled])["errors"])
        too_many = sensor.parse_line(line(10, 6, 50, 0, 0))
        self.assertIn("probe count exceeds firmware capacity",
                      sensor.report([too_many])["errors"])

    def test_log_mode_does_not_emit_secrets(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "device.log"
            output = Path(tmp) / "result.json"
            path.write_text("admin_password=NEVER_LOG_THIS\n" +
                            line(1, 1, 1, 0, 0) + "\n" +
                            line(2, 1, 2, 0, 0) + "\n")
            self.assertEqual(sensor.main(["--log", str(path), "--expected-probes",
                                          "1", "--output", str(output)]), 0)
            self.assertNotIn("NEVER_LOG_THIS", output.read_text())


if __name__ == "__main__":
    unittest.main()
