"""tools/check_unit_size.py finds long files and functions (God file refactor G00.1)."""
import contextlib
import importlib.util
import io
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('check_unit_size', ROOT / 'tools/check_unit_size.py')
check = importlib.util.module_from_spec(spec)
sys.modules['check_unit_size'] = check  # dataclasses look their module up
spec.loader.exec_module(check)


def body(lines: int) -> str:
    return ''.join(f'    step({i});\n' for i in range(lines))


class FindsFunctions(unittest.TestCase):
    def test_cpp_definitions_in_column_zero(self):
        text = (f'namespace monitor {{\nstruct Point {{\n    int x;\n}};\n'
                f'bool HttpApi::handle(httpd_req_t *r) {{\n{body(3)}}}\n'
                f'const char *name(State state) {{\n{body(1)}}}\n'
                f'extern "C" void app_main() {{\n{body(2)}}}\n}} // namespace monitor\n')
        found = check.functions('a.cpp', text)
        self.assertEqual([(n, length) for n, _, length in found],
                         [('HttpApi::handle', 5), ('name', 3), ('app_main', 4)])

    def test_cpp_signatures_over_several_lines(self):
        text = (f'bool HttpApi::start(AuthService &auth, WifiService &wifi,\n'
                f'                    bool simulated) {{\n{body(4)}}}\n'
                f'esp_err_t declared(int a,\n                   int b);\n'
                f'Store::Store(std::string path)\n    : path_(path) {{}}\n'
                f'int after() {{\n{body(1)}}}\n')
        found = check.functions('a.cpp', text)
        self.assertEqual([(n, start, length) for n, start, length in found],
                         [('HttpApi::start', 1, 7), ('after', 12, 3)])

    def test_typescript_top_level_functions(self):
        text = (f'import x from "y";\nexport default function Page() {{\n{body(4)}}}\n'
                f'export async function load(id: string) {{\n{body(2)}}}\n'
                f'function helper() {{\n{body(1)}}}\nconst arrow = () => 1;\n')
        found = check.functions('a.tsx', text)
        self.assertEqual([(n, length) for n, _, length in found],
                         [('Page', 6), ('load', 4), ('helper', 3)])


class ReportsAndAllows(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        root = Path(self.directory.name)
        (root / 'long.cpp').write_text(
            f'void big() {{\n{body(check.FUNCTION_LIMIT)}}}\n' + '// pad\n' * check.FILE_LIMIT)
        (root / 'short.cpp').write_text('void small() {\n    step();\n}\n')
        self.findings = check.scan(root, ['long.cpp', 'short.cpp'])

    def tearDown(self):
        self.directory.cleanup()

    def run_report(self, allowed, strict):
        with contextlib.redirect_stdout(io.StringIO()) as out:
            status = check.report(self.findings, allowed, strict)
        return status, out.getvalue()

    def test_limits(self):
        self.assertEqual({f.key for f in self.findings}, {'long.cpp', 'long.cpp:big'})

    def test_report_only_by_default(self):
        status, out = self.run_report({}, strict=False)
        self.assertEqual(status, 0)
        self.assertIn('OVER     long.cpp:1: big()', out)

    def test_strict_fails_unless_allowed(self):
        self.assertEqual(self.run_report({}, strict=True)[0], 1)
        allowed = {'long.cpp': 'kept', 'long.cpp:big': 'kept'}
        status, out = self.run_report(allowed, strict=True)
        self.assertEqual(status, 0)
        self.assertIn('allowed  long.cpp:1: big()', out)

    def test_stale_allow_list_entries_fail_strict(self):
        allowed = {'long.cpp': 'kept', 'long.cpp:big': 'kept', 'gone.cpp': 'old'}
        status, out = self.run_report(allowed, strict=True)
        self.assertEqual(status, 1)
        self.assertIn('STALE', out)

    def test_allow_list_format(self):
        with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as handle:
            handle.write('# comment\na.cpp  # one reason\nb.tsx:Page # another\n\n')
        self.assertEqual(check.allowlist(Path(handle.name)),
                         {'a.cpp': 'one reason', 'b.tsx:Page': 'another'})
        Path(handle.name).unlink()


if __name__ == '__main__':
    unittest.main()
