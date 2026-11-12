#!/usr/bin/env python3
"""Largest available history query and CSV export, while other requests keep being answered (R02-09).

Over Wi-Fi: five 30-day history queries (512 points) and three 30-day CSV exports, while another
client polls /api/v1/status twice a second and a WebSocket viewer counts live frames. Prints the
latencies, sizes, status failures and the longest gap between live frames. No credentials.
"""
import base64, json, os, socket, statistics, threading, time, urllib.error, urllib.request

import sys
H = sys.argv[1] if len(sys.argv) > 1 else '100.64.64.61'
stop = threading.Event()
status_lat, status_fail, ws_frames = [], [], []


def get(path, timeout=60):
    t = time.monotonic()
    try:
        with urllib.request.urlopen('http://%s%s' % (H, path), timeout=timeout) as r:
            body = r.read()
            return r.status, time.monotonic() - t, len(body)
    except urllib.error.HTTPError as e:
        return e.code, time.monotonic() - t, 0
    except Exception as e:  # noqa: BLE001
        return type(e).__name__, time.monotonic() - t, 0


def poll_status():
    while not stop.is_set():
        s, d, _ = get('/api/v1/status', 10)
        (status_lat if s == 200 else status_fail).append((time.monotonic(), d, s))
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
            ws_frames.append(time.monotonic())


threads = [threading.Thread(target=poll_status, daemon=True), threading.Thread(target=viewer, daemon=True)]
for t in threads:
    t.start()
time.sleep(3)
now = int(time.time())
frm, to = (now - 30 * 86400) * 1000, now * 1000
results = {'query': [], 'export': []}
for i in range(5):
    s, d, n = get('/api/v1/history?from=%d&to=%d&max_points=512' % (frm, to))
    results['query'].append((s, round(d, 3), n))
    time.sleep(1.2)
for i in range(3):
    while True:
        s, d, n = get('/api/v1/history/export?from=%d&to=%d' % (frm, to), 120)
        if s != 429:
            break
        time.sleep(3)
    results['export'].append((s, round(d, 3), n))
    time.sleep(31)
stop.set()
time.sleep(1.5)
lat = [d for _, d, _ in status_lat]
gaps = [b - a for a, b in zip(ws_frames, ws_frames[1:])]
print(json.dumps({
    'query_30d_max_points_512': results['query'],
    'export_30d': results['export'],
    'status_requests_ok': len(status_lat),
    'status_failures': [(round(d, 2), s) for _, d, s in status_fail],
    'status_latency_median_s': round(statistics.median(lat), 3) if lat else None,
    'status_latency_max_s': round(max(lat), 3) if lat else None,
    'websocket_frames': len(ws_frames),
    'websocket_max_gap_s': round(max(gaps), 2) if gaps else None,
}, indent=2))
