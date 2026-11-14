#!/usr/bin/env python3
"""Report source files and functions that are too long (God file refactor, spec §3).

Limits: a source file longer than 400 lines, or a function longer than 80 lines. Tests, generated
files, vendored code and fonts are not checked. Functions are found by shape, not by a parser:

- C++ (.cpp): a definition starting in column 0 whose signature (on one line, or continued over
  up to six lines) ends with `{`, up to the next `}` in column 0 (this is how clang-format lays
  out every function in this tree);
- TypeScript (.ts, .tsx): a top-level `function` declaration (optionally `export`, `default`,
  `async`), up to the next `}` in column 0.

Accepted exceptions are listed in tools/unit_size_allowlist.txt, one per line: a path for a long
file, or `path:function` for a long function, each followed by `# reason`. By default this only
reports; `--strict` exits 1 when something not on the allow-list is over a limit.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILE_LIMIT = 400
FUNCTION_LIMIT = 80
ALLOWLIST = ROOT / 'tools' / 'unit_size_allowlist.txt'
SOURCE_GLOBS = ('firmware/components/**/*.cpp', 'firmware/main/*.cpp', 'web/src/**/*.ts',
                'web/src/**/*.tsx')
EXCLUDED = re.compile(r'(\.test\.tsx?$|/generated/|\.generated\.ts$|/fonts/|/managed_components/|'
                      r'/build[^/]*/)')
CPP_START = re.compile(r'^(?!(namespace|struct|class|enum|union|typedef|using|template|#|//|'
                       r'static_assert)\b)[A-Za-z_].*\)\s*(const\s*)?(noexcept\s*)?(override\s*)?\{\s*$')
# The first line of a signature that continues on the next lines.
CPP_SIGNATURE = re.compile(r'^(?!(namespace|struct|class|enum|union|typedef|using|template|#|//|'
                           r'static_assert|return|if|for|while|switch|case|default|else)\b)'
                           r'[A-Za-z_][^;{}=]*\([^;{}]*$')
CPP_NAME = re.compile(r'([A-Za-z_][A-Za-z0-9_:~]*)\s*\(')
TS_START = re.compile(r'^(export\s+)?(default\s+)?(async\s+)?function\s*\*?\s*([A-Za-z0-9_$]*)')


@dataclass(frozen=True)
class Finding:
    path: str
    function: str | None  # None: the whole file
    lines: int
    start: int = 0

    @property
    def key(self) -> str:
        return self.path if self.function is None else f'{self.path}:{self.function}'

    def describe(self) -> str:
        if self.function is None:
            return f'{self.path}: {self.lines} lines (file limit {FILE_LIMIT})'
        return (f'{self.path}:{self.start}: {self.function}() is {self.lines} lines '
                f'(function limit {FUNCTION_LIMIT})')


def functions(path: str, text: str) -> list[tuple[str, int, int]]:
    """Returns (name, first line, line count) for each function found by shape."""
    lines = text.split('\n')
    found = []
    cpp = path.endswith('.cpp')
    index = 0
    while index < len(lines):
        line = lines[index]
        name = None
        if cpp and (CPP_START.match(line) or CPP_SIGNATURE.match(line)):
            if not CPP_START.match(line):
                # Follow a multi-line signature to its `{`; a `;` means a declaration.
                end_of_signature = None
                for ahead in range(index + 1, min(index + 7, len(lines))):
                    stripped = lines[ahead].rstrip()
                    # A declaration, a one-line body such as `{}`, or a gap: not a long function.
                    if not stripped or stripped.endswith((';', '}')):
                        break
                    if stripped.endswith('{'):
                        end_of_signature = ahead
                        break
                if end_of_signature is None:
                    index += 1
                    continue
            names = CPP_NAME.findall(line.split('(')[0] + '(')
            name = names[-1] if names else '?'
        elif not cpp:
            match = TS_START.match(line)
            if match:
                name = match.group(4) or 'default'
        if name is None:
            index += 1
            continue
        end = index + 1
        while end < len(lines) and not lines[end].startswith('}'):
            end += 1
        found.append((name, index + 1, end - index + 1))
        index = end + 1
    return found


def scan(root: Path = ROOT, paths: list[str] | None = None) -> list[Finding]:
    if paths is None:
        listed = subprocess.run(['git', 'ls-files', *SOURCE_GLOBS], cwd=root, check=True,
                                capture_output=True, text=True).stdout.split()
        paths = sorted(p for p in listed if not EXCLUDED.search(p))
    findings = []
    for path in paths:
        text = (root / path).read_text(encoding='utf-8')
        count = text.count('\n') + (0 if text.endswith('\n') else 1)
        if count > FILE_LIMIT:
            findings.append(Finding(path, None, count))
        for name, start, length in functions(path, text):
            if length > FUNCTION_LIMIT:
                findings.append(Finding(path, name, length, start))
    return findings


def allowlist(path: Path | None = None) -> dict[str, str]:
    path = path or ALLOWLIST
    allowed = {}
    if not path.exists():
        return allowed
    for line in path.read_text(encoding='utf-8').splitlines():
        entry, _, reason = line.partition('#')
        entry = entry.strip()
        if entry:
            allowed[entry] = reason.strip()
    return allowed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--strict', action='store_true',
                        help='exit 1 when something not on the allow-list is over a limit')
    args = parser.parse_args(argv)
    return report(scan(), allowlist(), args.strict)


def report(findings: list[Finding], allowed: dict[str, str], strict: bool) -> int:
    over = [f for f in findings if f.key not in allowed]
    for finding in findings:
        if finding.key in allowed:
            print(f'allowed  {finding.describe()} ({allowed[finding.key]})')
        else:
            print(f'OVER     {finding.describe()}')
    stale = sorted(set(allowed) - {f.key for f in findings})
    for entry in stale:
        print(f'STALE    allow-list entry no longer needed: {entry}')
    print(f'{len(over)} over the limits, {len(findings) - len(over)} allowed')
    return 1 if strict and (over or stale) else 0


if __name__ == '__main__':
    sys.exit(main())
