#!/usr/bin/env python3
"""Fail if a local device secret appears in the project's files, staged changes or recent history.

The secrets are the values the device setup tools keep under `.build/` (the administrator
password, the Wi-Fi setup key and similar). A leak is reported by where it
was found, never by its value. The check also fails, instead of quietly skipping, when it cannot do
its job: an unreadable setup file, or git that will not answer.

No setup files at all is not an error by default (a fresh CI checkout has none); use
`--require-secrets` on a machine that has a board to insist that there is something to check.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

# Keys, in the JSON files under .build/, whose string values are secrets.
SECRET_KEYS = ('password', 'admin_password', 'wifi_setup_password', 'wifi_password', 'ap_password',
               'wifi_key')
MIN_LENGTH = 12  # shorter values would match unrelated text


class CheckError(Exception):
    """The check could not be carried out (as opposed to finding a leak)."""


def load_secrets(root):
    secrets = set()
    build = root / '.build'
    if not build.is_dir():
        return secrets
    for path in sorted(build.glob('*.json')):
        try:
            record = json.loads(path.read_text())
        except (OSError, ValueError) as exc:
            raise CheckError(f'cannot read {path.name}: {type(exc).__name__}') from exc
        if not isinstance(record, dict):
            continue
        for key in SECRET_KEYS:
            value = record.get(key)
            if isinstance(value, str) and len(value) >= MIN_LENGTH:
                secrets.add(value.encode())
    return secrets


def git(root, *arguments):
    try:
        return subprocess.run(['git', *arguments], cwd=root, capture_output=True,
                              check=True).stdout
    except (OSError, subprocess.CalledProcessError) as exc:
        raise CheckError(f'git {" ".join(arguments[:2])} failed') from exc


def contains(content, secrets):
    return any(value in content for value in secrets)


def scan(root, secrets, log_files, history):
    leaks = []
    # 1. Reviewable files in the working tree (tracked, or untracked and not ignored) and logs.
    listing = git(root, 'ls-files', '-co', '--exclude-standard', '-z')
    paths = {root / name.decode() for name in listing.split(b'\0') if name}
    paths.update(Path(name) for name in log_files)
    scanned = 0
    for path in sorted(paths):
        if path.is_file():
            scanned += 1
            if contains(path.read_bytes(), secrets):
                leaks.append(f'file {path.relative_to(root) if path.is_relative_to(root) else path}')
    # 2. What is staged right now: it can differ from the working file (the file may have been
    #    cleaned after `git add`, but the staged copy is what gets committed).
    staged = git(root, 'diff', '--cached', '--text', '--no-color')
    if contains(staged, secrets):
        leaks.append('staged changes')
    # 3. The last commits, including their messages and removed lines.
    if history:
        marker = b'\x00COMMIT '  # git writes %x00 as a NUL byte; an argument cannot hold one
        log = git(root, 'log', '-p', '--text', '--no-color', f'-n{history}',
                  '--format=%x00COMMIT %H')
        for chunk in log.split(marker)[1:]:
            sha, _, body = chunk.partition(b'\n')
            if contains(body, secrets):
                leaks.append(f'commit {sha.decode()[:12]}')
    return leaks, scanned


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--log-file', action='append', default=[],
                        help='an extra log file to scan')
    parser.add_argument('--history', type=int, default=100,
                        help='how many recent commits to scan (0 disables; default 100)')
    parser.add_argument('--require-secrets', action='store_true',
                        help='fail when there are no local secrets to check against')
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args(argv)
    root = args.root.resolve()
    try:
        secrets = load_secrets(root)
        if not secrets:
            if args.require_secrets:
                raise CheckError('no local device secrets found under .build/ to check against')
            print('Secret leak check: no local device secrets are present, so there is nothing '
                  'to look for.')
            return 0
        leaks, scanned = scan(root, secrets, args.log_file, args.history)
    except CheckError as exc:
        print(f'Secret leak check could NOT be completed: {exc}', file=sys.stderr)
        return 1
    if leaks:
        print('Sensitive test data found in: ' + ', '.join(leaks), file=sys.stderr)
        return 1
    print(f'PASS: no local test secrets found in {scanned} files/logs, the staged changes and the '
          f'last {args.history} commits.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
