"""Host fault tests must only touch temporary stores (review audit A00.3).

The firmware host tests inject storage faults into real HistoryStore, EventStore and settings code.
This check keeps them away from anything that could alter a device or its owner's data: every
filesystem location they name is a unique temporary directory (an mkdtemp template under /tmp), a
file name joined to one, or the /dev/full write-failure device; sockets are in-process pairs, never
network connections; and no test names a serial port, the flashing tool or the private setup files
under .build/. Addresses such as the setup access point's 192.168.4.1 appear only as data.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HOST_TESTS = sorted((ROOT / 'firmware' / 'tests').glob('*.cpp'))
DISPLAY_TESTS = sorted((ROOT / 'firmware' / 'display_tests').glob('*.cpp'))

# String literals naming an absolute path.
ABSOLUTE_PATH = re.compile(r'"(/[A-Za-z0-9_./-]+)')
# Things a host test must never reach.
FORBIDDEN = [
    (re.compile(r'/dev/tty'), 'a serial port'),
    (re.compile(r'\.build/(device|elephant|nvs)'), 'a private setup or backup file'),
    (re.compile(r'esptool|write[_-]flash|erase[_-]flash'), 'the flashing tool'),
    (re.compile(r'\bconnect\(|getaddrinfo|gethostbyname|AF_INET'), 'a network connection'),
]


def allowed(path: str) -> bool:
    if path == '/dev/full':
        return True  # an always-failing write target used to inject write errors
    if path.count('/') == 1:
        return True  # a file name joined to a temporary directory, such as "/powercut.bin"
    # mkdtemp templates: a unique directory per run, removed by the test.
    return path.startswith('/tmp/') and path.endswith('XXXXXX')


class HostFixturesAreIsolated(unittest.TestCase):
    def test_sources_are_present(self):
        self.assertGreater(len(HOST_TESTS), 20)

    def test_absolute_paths_are_temporary_or_dev_full(self):
        for source in HOST_TESTS + DISPLAY_TESTS:
            text = source.read_text(encoding='utf-8')
            for match in ABSOLUTE_PATH.finditer(text):
                path = match.group(1)
                # Strings such as "/api/v1/..." are HTTP routes, not files.
                if path.startswith(('/api/', '/history', '/sensors', '/alerts', '/settings',
                                    '/status', '/auth', '/wifi', '/system', '/ota', '/clock',
                                    '/probe-scan', '/live', '/events', '/setup', '/index',
                                    '/assets', '/generate_204', '/hotspot', '/ncsi', '/connecttest',
                                    '/redirect', '/success', '/canonical', '/library')):
                    continue
                with self.subTest(file=source.name, path=path):
                    self.assertTrue(allowed(path), f'{source.name} names {path}')

    def test_no_device_serial_network_or_credentials(self):
        for source in HOST_TESTS + DISPLAY_TESTS:
            text = source.read_text(encoding='utf-8')
            for pattern, what in FORBIDDEN:
                with self.subTest(file=source.name, what=what):
                    self.assertIsNone(pattern.search(text), f'{source.name} refers to {what}')


class TheCheckCatchesUnsafeFixtures(unittest.TestCase):
    def test_paths(self):
        for path in ['/tmp/monitor-x-XXXXXX', '/dev/full', '/powercut.bin']:
            self.assertTrue(allowed(path), path)
        for path in ['/home/phil/history', '/tmp/fixed-name', '/var/lib/x', '/dev/ttyACM0']:
            self.assertFalse(allowed(path), path)

    def test_forbidden_references(self):
        samples = {
            'a serial port': 'open("/dev/ttyACM1")',
            'a private setup or backup file': 'read(".build/device-admin.json")',
            'the flashing tool': 'system("esptool write-flash")',
            'a network connection': 'connect(fd, addr, len)',
        }
        for what, text in samples.items():
            matched = [w for pattern, w in FORBIDDEN if pattern.search(text)]
            self.assertIn(what, matched, text)

if __name__ == '__main__':
    unittest.main()
