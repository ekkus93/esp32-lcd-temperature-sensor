#!/usr/bin/env python3
"""No function on the history read path keeps a large buffer on the stack (2026-11-18 overflow).

A history visit runs at the bottom of a deep call chain on 8 KiB tasks (the HTTP history worker and
the trend restore). A 4 KiB array in segment_fingerprint() overflowed the worker's stack on the
board. The host build compiles monitor_core with -fstack-usage; this check reads those reports and
fails if any function in the history read path has a frame over the limit.
Usage: check_history_stack.py <host build directory>
"""
import sys
from pathlib import Path

# Host (x86-64) frames run about 1.5 times the ESP32's (HistoryStore::visit: 1,152 bytes here, 784 on
# the board); 2 KiB still catches any buffer like the 4 KiB one that overflowed.
LIMIT = 2048
FILES = ('history.cpp', 'history_visit.cpp', 'history_csv.cpp', 'history_query.cpp')

build = Path(sys.argv[1])
reports = [p for p in build.rglob('*.su') if any(p.name.startswith(f) for f in FILES)]
if len(reports) < len(FILES):
    print(f'expected stack-usage reports for {FILES}, found {[p.name for p in reports]}')
    sys.exit(1)
over = []
for report in reports:
    for line in report.read_text().splitlines():
        location, size, _kind = line.rsplit('\t', 2)
        if int(size) > LIMIT:
            over.append(f'{int(size):6} bytes  {location}')
if over:
    print(f'History read path functions with stack frames over {LIMIT} bytes:')
    print('\n'.join(over))
    sys.exit(1)
print(f'history read path: no stack frame over {LIMIT} bytes in {len(reports)} files')
