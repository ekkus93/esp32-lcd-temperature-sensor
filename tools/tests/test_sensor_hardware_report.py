"""Host-only regression for numeric sensor diagnostics; no serial device."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1] / "sensor_hardware_report.py"
spec = importlib.util.spec_from_file_location("sensor_hardware_report", TOOL)
sensor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sensor)


def line(cycles, probes, valid, invalid, faults, latency=750):
    return (
        f"I (12345) monitor: SENSOR_DIAG cycles={cycles} last_probes={probes} "
        f"max_probes={probes} valid={valid} invalid={invalid} "
        f"fault_cycles={faults} last_read_ms={latency} max_read_ms={latency}"
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
