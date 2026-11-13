"""Every API route the HTTP code handles is behind the one access gate (review audit H04).

Static checks on the HTTP code (firmware/components/http_api: http_api.cpp, the route table in
api_router.cpp and the handlers in routes_*.cpp), paired with the gate matrices in
firmware/tests/route_access_tests.cpp and api_router_tests.cpp:
- every /api/v1 path the HTTP code names is in the route inventory the gate is tested over, and the
  route table names every inventory path, so the inventory cannot fall behind the code;
- inside HttpApi::handle, only the open auth routes (state, enroll, login) are handled before the
  gate_request() call; every other handler is called after it.
The live WebSocket (/api/v1/live) is registered separately with its own Origin check.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HTTP_DIR = ROOT / 'firmware/components/http_api'
HTTP = (HTTP_DIR / 'http_api.cpp').read_text(encoding='utf-8')
ROUTER = (HTTP_DIR / 'api_router.cpp').read_text(encoding='utf-8')
ROUTE_FILES = {p.name: p.read_text(encoding='utf-8') for p in sorted(HTTP_DIR.glob('routes_*.cpp'))}
EARLY_HANDLERS = {'route_auth_state', 'route_auth_enroll', 'route_auth_login'}
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
        named = set()
        for text in [HTTP, *ROUTE_FILES.values()]:
            named |= set(re.findall(r'"(/api/v1/[a-z0-9/_-]+)"', text))
        self.assertLessEqual(named - SEPARATE, inventory_paths())
        table = set(re.findall(r'"(/api/v1/[a-z0-9/_-]+)"', ROUTER))
        self.assertEqual(table, inventory_paths())

    def test_only_open_auth_routes_are_handled_before_the_gate(self):
        body = handle_body()
        gate = body.index('gate_request(')
        before = set(re.findall(r'path == "(/api/v1/[a-z0-9/_-]+)"', body[:gate]))
        self.assertEqual(before, BEFORE_GATE_ALLOWED)
        self.assertNotIn('rfind("/api/v1/', body[:gate])
        self.assertEqual(set(re.findall(r'\b(route_[a-z_]+)\(', body[:gate])), EARLY_HANDLERS)
        # Every handler the dispatcher calls after the gate is one of the route files' handlers.
        defined = set()
        for text in ROUTE_FILES.values():
            defined |= set(re.findall(r'^esp_err_t (route_[a-z_]+)\(', text, re.M))
        after = set(re.findall(r'\b(route_[a-z_]+)\(', body[gate:]))
        self.assertLessEqual(after, defined)

    def test_the_gate_refusals_return_before_any_handler(self):
        body = handle_body()
        gate = body[body.index('switch (gate.gate)'):]
        gate = gate[:gate.index('}') + 1]
        self.assertIn('return send_error(ex, "404 Not Found"', gate)
        self.assertIn('return send_error(ex, "401 Unauthorized"', gate)


class RouterTableMatchesPolicy(unittest.TestCase):
    # The router's table (api_router.cpp) names the same paths as the access policy
    # (route_access.cpp); the host test api_router checks methods and patterns.
    def test_same_paths(self):
        router = (ROOT / 'firmware/components/http_api/api_router.cpp').read_text(encoding='utf-8')
        policy = (ROOT / 'firmware/components/monitor_core/route_access.cpp').read_text(
            encoding='utf-8')
        table = set(re.findall(r'"(/api/v1/[a-z0-9/_-]+)"', router))
        table = {p for p in table if not p.endswith('/')}
        named = {'/api/v1' + p for p in re.findall(r'"(/[a-z0-9/_-]+)"', policy)
                 if p not in ('/api/v1', '/sensors/', '/calibration-hold')}
        self.assertEqual(table, named)


if __name__ == '__main__':
    unittest.main()
