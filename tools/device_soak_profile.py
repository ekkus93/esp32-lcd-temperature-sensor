#!/usr/bin/env python3
"""Soak the board under steady load and look for memory growth.

Reads the console's HEALTH lines over USB (opening the port restarts the board), and over Wi-Fi
keeps two live WebSocket viewers connected while polling status, history, alerts and the CSV
export. After a warm-up it fits a line through free internal RAM and free PSRAM and reports the
growth per hour, the low-water marks, dropped display frames and the request results. No
credentials are used or printed.

    python tools/device_soak_profile.py --port /dev/ttyACM0 --minutes 45
"""
import argparse
import base64
import json
import os
import re
import socket
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

import serial

HEALTH = re.compile(
    rb'HEALTH uptime_ms=(\d+).*?internal_free=(\d+) internal_min=(\d+) internal_largest=(\d+) '
    rb'psram_free=(\d+) psram_min=(\d+) main_stack_min=(\d+)(?: display_dropped=(\d+))?')
FIELDS = ('uptime_ms', 'internal_free', 'internal_min', 'internal_largest', 'psram_free',
          'psram_min', 'main_stack_min', 'display_dropped')


def slope_per_hour(points):
    """Least-squares slope of (seconds, value) points, in value units per hour."""
    if len(points) < 2:
        return 0.0
    n = len(points)
    mean_x = sum(x for x, _ in points) / n
    mean_y = sum(y for _, y in points) / n
    var = sum((x - mean_x) ** 2 for x, _ in points)
    if var == 0:
        return 0.0
    return 3600 * sum((x - mean_x) * (y - mean_y) for x, y in points) / var


class Soak:
    def __init__(self, host, deadline):
        self.host = host
        self.deadline = deadline
        self.lock = threading.Lock()
        self.http = {}
        self.ws_frames = 0
        self.ws_connects = 0
        self.bytes_in = 0

    def count(self, kind, status, size=0):
        with self.lock:
            key = '%s %s' % (kind, status)
            self.http[key] = self.http.get(key, 0) + 1
            self.bytes_in += size

    def get(self, kind, path):
        try:
            with urllib.request.urlopen('http://%s%s' % (self.host, path), timeout=30) as response:
                self.count(kind, response.status, len(response.read()))
        except urllib.error.HTTPError as error:
            self.count(kind, error.code)
        except Exception as error:  # noqa: BLE001 - every failure is counted, not hidden
            self.count(kind, type(error).__name__)

    def poller(self, kind, period, path_for):
        while time.monotonic() < self.deadline:
            started = time.monotonic()
            self.get(kind, path_for())
            time.sleep(max(0.0, period - (time.monotonic() - started)))

    def viewer(self):
        while time.monotonic() < self.deadline:
            try:
                sock = socket.create_connection((self.host, 80), timeout=15)
                key = base64.b64encode(os.urandom(16)).decode()
                sock.sendall(('GET /api/v1/live HTTP/1.1\r\nHost: %s\r\nOrigin: http://%s\r\n'
                              'Upgrade: websocket\r\nConnection: Upgrade\r\n'
                              'Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n'
                              % (self.host, self.host, key)).encode())
                head = b''
                while b'\r\n\r\n' not in head:
                    chunk = sock.recv(1024)
                    if not chunk:
                        raise ConnectionError('closed during upgrade')
                    head += chunk
                if b' 101 ' not in head.split(b'\r\n', 1)[0]:
                    self.count('ws-upgrade', head.split(b' ', 2)[1].decode())
                    time.sleep(5)
                    continue
                with self.lock:
                    self.ws_connects += 1
                buffer = head.split(b'\r\n\r\n', 1)[1]
                while time.monotonic() < self.deadline:
                    while len(buffer) >= 2:
                        length = buffer[1] & 0x7f
                        offset = 2
                        if length == 126:
                            if len(buffer) < 4:
                                break
                            length = int.from_bytes(buffer[2:4], 'big')
                            offset = 4
                        elif length == 127:
                            if len(buffer) < 10:
                                break
                            length = int.from_bytes(buffer[2:10], 'big')
                            offset = 10
                        if len(buffer) < offset + length:
                            break
                        with self.lock:
                            self.ws_frames += 1
                            self.bytes_in += offset + length
                        buffer = buffer[offset + length:]
                    chunk = sock.recv(4096)
                    if not chunk:
                        raise ConnectionError('closed by device')
                    buffer += chunk
                sock.close()
            except Exception as error:  # noqa: BLE001
                self.count('ws', type(error).__name__)
                time.sleep(5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', required=True)
    parser.add_argument('--host', default='100.64.64.61')
    parser.add_argument('--minutes', type=float, default=45)
    parser.add_argument('--warmup-minutes', type=float, default=5)
    parser.add_argument('--report', default='.build/device-soak-profile.json')
    args = parser.parse_args()

    port = serial.Serial(port=None, baudrate=115200, timeout=0.5)
    port.dtr = False
    port.rts = False
    port.port = args.port
    port.open()
    console = b''
    ready = time.monotonic() + 15
    while time.monotonic() < ready and b'monitor: READY' not in console:
        console += port.read(4096)
    time.sleep(10)  # let Wi-Fi connect after the restart the port open causes

    start = time.monotonic()
    deadline = start + args.minutes * 60
    soak = Soak(args.host, deadline)
    now = lambda: int(time.time())  # noqa: E731
    threads = [threading.Thread(target=soak.viewer, daemon=True) for _ in range(2)]
    threads += [
        threading.Thread(target=soak.poller, args=('status', 2, lambda: '/api/v1/status'),
                         daemon=True),
        threading.Thread(target=soak.poller, args=(
            'history', 5, lambda: '/api/v1/history?from=%d&to=%d&max_points=48'
            % ((now() - 86400) * 1000, now() * 1000)), daemon=True),
        threading.Thread(target=soak.poller, args=('alerts', 10, lambda: '/api/v1/alerts'),
                         daemon=True),
        threading.Thread(target=soak.poller, args=(
            'export', 60, lambda: '/api/v1/history/export?from=%d&to=%d'
            % ((now() - 86400) * 1000, now() * 1000)), daemon=True),
    ]
    for thread in threads:
        thread.start()
    samples = []
    faults = []
    while time.monotonic() < deadline:
        data = port.read(4096)
        console += data
        for line in data.split(b'\n'):
            match = HEALTH.search(line)
            if match:
                values = [int(v) if v is not None else 0 for v in match.groups()]
                samples.append({'host_s': round(time.monotonic() - start, 1),
                                **dict(zip(FIELDS, values))})
            lowered = line.lower()
            if any(word in lowered for word in (b'panic', b'abort', b'backtrace', b'task_wdt',
                                                b'guru')):
                faults.append(line.decode('utf-8', 'replace').strip()[:160])
    for thread in threads:
        thread.join(timeout=35)

    warm = [s for s in samples if s['host_s'] >= args.warmup_minutes * 60]
    report = {
        'minutes': args.minutes,
        'warmup_minutes': args.warmup_minutes,
        'health_samples': len(samples),
        'health_samples_after_warmup': len(warm),
        'internal_free_slope_bytes_per_hour':
            round(slope_per_hour([(s['host_s'], s['internal_free']) for s in warm])),
        'psram_free_slope_bytes_per_hour':
            round(slope_per_hour([(s['host_s'], s['psram_free']) for s in warm])),
        'internal_free_first_last_after_warmup':
            [warm[0]['internal_free'], warm[-1]['internal_free']] if warm else None,
        'psram_free_first_last_after_warmup':
            [warm[0]['psram_free'], warm[-1]['psram_free']] if warm else None,
        'internal_min_low_water': min((s['internal_min'] for s in samples), default=None),
        'internal_largest_min': min((s['internal_largest'] for s in samples), default=None),
        'psram_min_low_water': min((s['psram_min'] for s in samples), default=None),
        'main_stack_min': min((s['main_stack_min'] for s in samples), default=None),
        'display_dropped_final': samples[-1]['display_dropped'] if samples else None,
        'http_results': dict(sorted(soak.http.items())),
        'websocket_connects': soak.ws_connects,
        'websocket_frames': soak.ws_frames,
        'bytes_received': soak.bytes_in,
        'console_faults': faults,
    }
    Path(args.report).parent.mkdir(parents=True, exist_ok=True)
    Path(args.report).write_text(json.dumps({'summary': report, 'samples': samples}, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
