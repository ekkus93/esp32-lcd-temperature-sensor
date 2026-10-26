#!/usr/bin/env python3
"""Check reviewable project files and selected logs against local test secrets."""
import argparse
import json
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--log-file', action='append', default=[])
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
secrets = set()
for name in ['device-setup.json', 'device-setup-current.json', 'device-admin.json']:
    path = root / '.build' / name
    if path.exists():
        record = json.loads(path.read_text())
        for key in ['password', 'setup_secret']:
            value = record.get(key)
            if isinstance(value, str) and len(value) >= 12:
                secrets.add(value.encode())
if not secrets:
    print('Secret leak check skipped: no local device test secrets are available.')
    raise SystemExit(0)
result = subprocess.run(['git', 'ls-files', '-co', '--exclude-standard', '-z'], cwd=root,
                        capture_output=True, check=True)
paths = {root / name.decode() for name in result.stdout.split(b'\0') if name}
paths.update(Path(name) for name in args.log_file)
leaks = []
for path in paths:
    if path.is_file():
        content = path.read_bytes()
        if any(value in content for value in secrets):
            leaks.append(str(path))
if leaks:
    raise SystemExit('Sensitive test data found in: '+', '.join(sorted(leaks)))
print(f'PASS: no local test secrets found in {len(paths)} reviewable files/logs.')
