"""Static checks that keep every API route behind the one tested dispatcher (review audit H04).

The behaviour is tested in C++: route_boundary (firmware/tests/route_boundary_tests.cpp) sends every
route in the table through dispatch_api() as anonymous, session-without-CSRF, wrong-CSRF and full
session callers with recording fakes, and api_router checks the table against route_access() for
every method. What C++ cannot see is code outside that path, so this file checks the sources
(God file refactor G12.2):
- the route table (api_router.cpp) names the same paths as the access policy (route_access.cpp);
- http_api.cpp has no route handler of its own: it names no /api/v1 route and calls no route_*()
  handler; API requests go through dispatch_api() only, after the Host/Origin check.
The live WebSocket (/api/v1/live) is registered separately with its own Origin check.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HTTP_DIR = ROOT / 'firmware/components/http_api'
HTTP = (HTTP_DIR / 'http_api.cpp').read_text(encoding='utf-8')
ROUTER = (HTTP_DIR / 'api_router.cpp').read_text(encoding='utf-8')
POLICY = (ROOT / 'firmware/components/monitor_core/route_access.cpp').read_text(encoding='utf-8')
PATH = re.compile(r'"(/api/v1/[a-z0-9/_-]+)"')


def handle_body() -> str:
    start = HTTP.index('esp_err_t HttpApi::handle(HttpExchange &ex) {')
    end = HTTP.find('\nesp_err_t HttpApi::', start + 10)
    return HTTP[start:end if end >= 0 else len(HTTP)]


class RouteTable(unittest.TestCase):
    def test_table_and_policy_name_the_same_paths(self):
        table = set(PATH.findall(ROUTER))
        named = {'/api/v1' + p for p in re.findall(r'"(/[a-z0-9/_-]+)"', POLICY)
                 if p not in ('/api/v1', '/sensors/', '/calibration-hold')}
        self.assertEqual(table, named | {'/api/v1/sensors/'})


class NoHandlersOutsideTheDispatcher(unittest.TestCase):
    def test_http_api_names_no_route(self):
        self.assertEqual(set(PATH.findall(HTTP)) - {'/api/v1/live'}, set())

    def test_http_api_calls_no_route_handler(self):
        self.assertEqual(re.findall(r'\broute_[a-z_]+\(', HTTP), [])

    def test_api_requests_go_through_the_dispatcher_after_the_origin_check(self):
        body = handle_body()
        origin = body.index('origin(ex,')
        dispatch = body.index('dispatch_api(ex, path,')
        self.assertLess(origin, dispatch)
        self.assertEqual(body.count('dispatch_api('), 1)


if __name__ == '__main__':
    unittest.main()
