#!/usr/bin/env python3
"""Wrong logins from one client must not stall the dashboard or lock out the owner (R02-08).

This computer sends a wrong password over Wi-Fi every second (the "attacker") while it also streams
the dashboard: GET /api/v1/status twice a second and a live WebSocket. Part-way through, the owner
signs in with the right password through the USB bridge, which the device sees as another client.
Opening the serial port restarts the board, so the test waits for it first. Prints results only;
the password is read from the private admin file and never printed.

    python tools/device_login_flood_test.py --port /dev/ttyACM0
"""
import argparse
import base64
import json
import os
import socket
import statistics
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
parser.add_argument('--host', default='100.64.64.61')
parser.add_argument('--admin-file', default='.build/device-admin.json')
parser.add_argument('--seconds', type=int, default=90)
args = parser.parse_args()
H = args.host
password = json.loads(Path(args.admin_file).read_text())['admin_password']

port = serial.Serial(port=None, baudrate=115200, timeout=0.5)
port.dtr = False
port.rts = False
port.port = args.port
port.open()
boot = b''
deadline = time.monotonic() + 15
while time.monotonic() < deadline and b'monitor: READY' not in boot:
    boot += port.read(4096)


def http(path, method='GET', body=None, timeout=60):
    request = urllib.request.Request('http://%s%s' % (H, path), method=method,
                                     data=None if body is None else json.dumps(body).encode(),
                                     headers={'Content-Type': 'application/json',
                                              'Origin': 'http://%s' % H})
    started = time.monotonic()
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            response.read()
            return response.status, time.monotonic() - started
    except urllib.error.HTTPError as error:
        return error.code, time.monotonic() - started
    except Exception as error:  # noqa: BLE001 - counted, not hidden
        return type(error).__name__, time.monotonic() - started


while http('/api/v1/status', timeout=2)[0] != 200:
    time.sleep(1)
time.sleep(10)

stop = threading.Event()
status_times, status_failures, frames, attacker = [], [], [], {}


def dashboard():
    while not stop.is_set():
        code, seconds = http('/api/v1/status', timeout=10)
        (status_times if code == 200 else status_failures).append(seconds if code == 200 else code)
        time.sleep(0.5)


def viewer():
    sock = socket.create_connection((H, 80), timeout=15)
    key = base64.b64encode(os.urandom(16)).decode()
    sock.sendall(('GET /api/v1/live HTTP/1.1\r\nHost: %s\r\nOrigin: http://%s\r\nUpgrade: websocket\r\n'
                  'Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n'
                  % (H, H, key)).encode())
    sock.settimeout(1)
    while not stop.is_set():
        try:
            data = sock.recv(8192)
        except socket.timeout:
            continue
        if not data:
            break
        if b'"sequence"' in data:
            frames.append(time.monotonic())


def guesser():
    while not stop.is_set():
        code, _ = http('/api/v1/auth/login', 'POST', {'password': 'intentionally-wrong-guess'})
        attacker[code] = attacker.get(code, 0) + 1
        time.sleep(1)


def bridge_login():
    request = {'path': '/api/v1/auth/login', 'method': 'POST',
               'headers': {'Origin': 'http://192.168.4.1'}, 'binary': False, 'headers_only': False,
               'body': json.dumps({'password': password})}
    port.write(('HTTP_BRIDGE ' + json.dumps(request) + '\n').encode())
    data = b''
    end = time.monotonic() + 68
    while time.monotonic() < end and b'HTTP_BRIDGE_END' not in data:
        data += port.read(2048)
    data = data.replace(b'\r', b'')
    start = data.find(b'HTTP_BRIDGE_BEGIN\n')
    finish = data.find(b'HTTP_BRIDGE_END', start)
    return json.loads(data[start + len(b'HTTP_BRIDGE_BEGIN\n'):finish].strip())['status']


threads = [threading.Thread(target=f, daemon=True) for f in (dashboard, viewer, guesser)]
for thread in threads:
    thread.start()
time.sleep(args.seconds / 2)
owner = []
owner_started = time.monotonic()
while time.monotonic() - owner_started < 40:
    code = bridge_login()
    owner.append(code)
    if code != 429:  # 429 here means another login (the guess) is being checked: retry
        break
    time.sleep(2)
owner_seconds = time.monotonic() - owner_started
time.sleep(max(0, args.seconds / 2 - owner_seconds))
stop.set()
time.sleep(1.5)
gaps = [b - a for a, b in zip(frames, frames[1:])]
report = {
    'attacker_results': attacker,
    'owner_login_attempts': owner,
    'owner_signed_in': bool(owner) and owner[-1] == 200,
    'owner_seconds': round(owner_seconds, 1),
    'status_ok': len(status_times),
    'status_failures': status_failures,
    'status_median_s': round(statistics.median(status_times), 3) if status_times else None,
    'status_max_s': round(max(status_times), 3) if status_times else None,
    'live_frames': len(frames),
    'live_max_gap_s': round(max(gaps), 2) if gaps else None,
}
print(json.dumps(report, indent=2))
ok = (report['owner_signed_in'] and not status_failures and report['live_max_gap_s'] is not None
      and report['live_max_gap_s'] < 8 and (report['status_max_s'] or 99) < 5)
print('PASS' if ok else 'FAIL')
raise SystemExit(0 if ok else 1)
