import importlib.util
import json
import stat
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("setup_file", ROOT / "setup_file.py")
setup_file = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup_file)


class SetupFileTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / ".build" / "device-setup.json"

    def tearDown(self):
        self.directory.cleanup()

    def test_new_response_keeps_the_administrator_password(self):
        self.path.parent.mkdir()
        self.path.write_text(json.dumps({"admin_password": "kept-admin-value", "ssid": "old"}))
        setup_file.save_setup_response(
            self.path, {"ssid": "TempMonitor-1", "password": "new-wifi-key", "ip": "192.168.4.1"})
        saved = json.loads(self.path.read_text())
        self.assertEqual(saved["admin_password"], "kept-admin-value")
        self.assertEqual(saved["ssid"], "TempMonitor-1")
        self.assertEqual(saved["wifi_setup_password"], "new-wifi-key")
        self.assertNotIn("password", saved)

    def test_creates_a_private_file(self):
        setup_file.save_setup_response(self.path, {"ssid": "TempMonitor-1", "password": "k"})
        self.assertEqual(stat.S_IMODE(self.path.stat().st_mode), 0o600)
        self.assertEqual(json.loads(self.path.read_text())["wifi_setup_password"], "k")

    def test_an_existing_file_is_made_private(self):
        self.path.parent.mkdir()
        self.path.write_text("{}")
        self.path.chmod(0o644)
        setup_file.save_setup_response(self.path, {"ssid": "x"})
        self.assertEqual(stat.S_IMODE(self.path.stat().st_mode), 0o600)


if __name__ == "__main__":
    unittest.main()
