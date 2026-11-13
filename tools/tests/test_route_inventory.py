"""Every API route the HTTP code handles is behind the one access gate (review audit H04).

Static checks on firmware/components/http_api/http_api.cpp, paired with the gate matrix in
firmware/tests/route_access_tests.cpp:
- every /api/v1 path the handler names is in the test's route inventory, and the reverse, so the
  inventory the gate is tested over cannot fall behind the code;
- inside HttpApi::handle, only open auth routes (state, enroll, login) are handled before the
  api_gate() call; every other route comes after it.
The live WebSocket (/api/v1/live) is registered separately with its own Origin check.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HTTP = (ROOT / 'firmware/components/http_api/http_api.cpp').read_text(encoding='utf-8')
TEST = (ROOT / 'firmware/tests/route_access_tests.cpp').read_text(encoding='utf-8')
BEFORE_GATE_ALLOWED = {'/api/v1/auth/state', '/api/v1/auth/enroll', '/api/v1/auth/login'}
SEPARATE = {'/api/v1/live'}
ROM_PREFIX = '/api/v1/sensors/'


def handle_body() -> str:
    start = HTTP.index('esp_err_t HttpApi::handle(HttpExchange &ex) {')
    end = HTTP.find('\nesp_err_t HttpApi::', start + 10)
    return HTTP[start:end if end >= 0 else len(HTTP)]


def inventory_paths() -> set[str]:
    block = TEST[TEST.index('const Route inventory[] = {'):]
    block = block[:block.index('};')]
    paths = set(re.findall(r'"(/api/v1/[a-z0-9/_-]+)"', block))
    if '"/api/v1/sensors/" + rom' in block or 'hold' in block:
        paths.add(ROM_PREFIX)
    return paths


class RouteInventory(unittest.TestCase):
    def test_handled_paths_match_the_tested_inventory(self):
        handled = set(re.findall(r'"(/api/v1/[a-z0-9/_-]+)"', HTTP)) - SEPARATE
        self.assertEqual(handled, inventory_paths())

    def test_only_open_auth_routes_are_handled_before_the_gate(self):
        body = handle_body()
        gate = body.index('api_gate(')
        before = set(re.findall(r'path == "(/api/v1/[a-z0-9/_-]+)"', body[:gate]))
        self.assertEqual(before, BEFORE_GATE_ALLOWED)
        self.assertNotIn('rfind("/api/v1/', body[:gate])
        after = set(re.findall(r'"(/api/v1/[a-z0-9/_-]+)"', body[gate:]))
        self.assertTrue(after >= {'/api/v1/system/factory-reset', '/api/v1/system/reboot',
                                  '/api/v1/ota', '/api/v1/settings', ROM_PREFIX})

    def test_the_gate_refusals_return_before_any_handler(self):
        body = handle_body()
        gate = body[body.index('switch (api_gate('):]
        gate = gate[:gate.index('}') + 1]
        self.assertIn('return send_error(ex, "404 Not Found"', gate)
        self.assertIn('return send_error(ex, "401 Unauthorized"', gate)


if __name__ == '__main__':
    unittest.main()
