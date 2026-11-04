#!/usr/bin/env python3
"""Verify that a slow JSON upload cannot monopolize the ESP32 HTTP task."""
import argparse
import base64
import hashlib
import json
import os
import select
import socket
import threading
import time
import urllib.request
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--host', required=True, help='Device IP or hostname')
parser.add_argument('--monitor-websocket', action='store_true',
                    help='Verify a live authenticated snapshot arrives after the slow request')
parser.add_argument('--admin-file', default='.build/device-admin.json',
                    help='Private test-admin JSON file used only with --monitor-websocket')
args = parser.parse_args()
host = args.host
start = time.monotonic()
result = {}
errors = []


def exact(sock, length):
    data = bytearray()
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise RuntimeError('WebSocket closed before a complete frame')
        data.extend(chunk)
    return bytes(data)


def websocket_frame(sock):
    first, second = exact(sock, 2)
    opcode = first & 0x0f
    length = second & 0x7f
    if length == 126:
        length = int.from_bytes(exact(sock, 2), 'big')
    elif length == 127:
        length = int.from_bytes(exact(sock, 8), 'big')
    if second & 0x80:
        raise RuntimeError('Unexpected masked server WebSocket frame')
    payload = exact(sock, length)
    if opcode == 8:
        raise RuntimeError('WebSocket server closed the live stream')
    return opcode, payload


def authenticated_websocket():
    password = json.loads(Path(args.admin_file).read_text())['password']
    origin = f'http://{host}'
    request = urllib.request.Request(
        f'{origin}/api/v1/auth/login',
        data=json.dumps({'password': password}).encode(),
        headers={'Content-Type': 'application/json', 'Origin': origin},
        method='POST',
    )
    # The PSA PBKDF2 work is intentionally time-sliced at idle priority on
    # CPU0. Allow it longer than the ordinary JSON receipt deadline.
    with urllib.request.urlopen(request, timeout=50) as response:
        if response.status != 200:
            raise RuntimeError('WebSocket test administrator login failed')
        cookie = response.headers.get('Set-Cookie', '').split(';', 1)[0]
    if not cookie:
        raise RuntimeError('Login did not provide a session cookie')
    key = base64.b64encode(os.urandom(16)).decode()
    client = socket.create_connection((host, 80), timeout=5)
    client.settimeout(8)
    client.sendall((
        f'GET /api/v1/live HTTP/1.1\r\nHost: {host}\r\n'
        f'Origin: {origin}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
        f'Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\n'
        f'Cookie: {cookie}\r\n\r\n'
    ).encode())
    handshake = bytearray()
    while b'\r\n\r\n' not in handshake:
        part = client.recv(1)
        if not part:
            raise RuntimeError('WebSocket closed during upgrade')
        handshake.extend(part)
    headers = bytes(handshake).split(b'\r\n\r\n', 1)[0]
    accept = base64.b64encode(hashlib.sha1(
        (key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
    if b'101 Switching Protocols' not in headers or b'Sec-WebSocket-Accept: ' + accept not in headers:
        client.close()
        raise RuntimeError('Authenticated WebSocket upgrade failed')
    return client


def next_snapshot(client):
    opcode, payload = websocket_frame(client)
    while opcode != 1:
        opcode, payload = websocket_frame(client)
    return json.loads(payload)


ws = authenticated_websocket() if args.monitor_websocket else None
baseline = next_snapshot(ws) if ws else None
if ws:
    ws.settimeout(8)

start = time.monotonic()
slow = socket.create_connection((host, 80), timeout=3)
slow.settimeout(8)
slow.sendall(
    (
        f'POST /api/v1/auth/login HTTP/1.1\r\n'
        f'Host: {host}\r\n'
        f'Origin: http://{host}\r\n'
        'Content-Type: application/json\r\n'
        'Content-Length: 64\r\n'
        'Connection: close\r\n\r\n'
        '{"password":"'
    ).encode()
)


def healthy_request():
    try:
        client = socket.create_connection((host, 80), timeout=3)
        client.settimeout(6)
        client.sendall(
            (f'GET /api/v1/auth/state HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n').encode()
        )
        response = bytearray()
        while b'\r\n\r\n' not in response:
            response.extend(client.recv(2048))
        headers, body = bytes(response).split(b'\r\n\r\n', 1)
        content_length = int(
            next(
                line.split(b':', 1)[1]
                for line in headers.split(b'\r\n')
                if line.lower().startswith(b'content-length:')
            )
        )
        while len(body) < content_length:
            body += client.recv(2048)
        result.update(headers=headers, body=body, elapsed=time.monotonic() - start)
        client.close()
    except Exception as error:  # Keep the main thread responsible for a clear failure.
        errors.append(repr(error))


worker = threading.Thread(target=healthy_request)
worker.start()
closed_at = None
next_send = time.monotonic() + 2
while time.monotonic() < start + 6:
    time.sleep(min(.05, max(0, next_send - time.monotonic())))
    if time.monotonic() >= next_send:
        try:
            slow.sendall(b'x')
        except OSError:
            closed_at = time.monotonic()
            break
        next_send += 2
    try:
        readable, _, _ = select.select([slow], [], [], 0)
        if readable and not slow.recv(1):
            closed_at = time.monotonic()
            break
    except OSError:
        closed_at = time.monotonic()
        break
worker.join(6)
slow.close()
worker.join(2)

if worker.is_alive() or errors:
    raise SystemExit(f'FAIL: concurrent healthy request did not recover: {errors}')
if b'200 OK' not in result['headers'] or b'"enrolled":true' not in result['body']:
    raise SystemExit('FAIL: auth-state request did not return a valid response')
if result['elapsed'] > 6:
    raise SystemExit(f"FAIL: service recovered after {result['elapsed']:.2f}s")
if closed_at is None or closed_at - start > 6:
    raise SystemExit('FAIL: incomplete body connection remained open beyond six seconds')
print(f"PASS: HTTP service recovered in {result['elapsed']:.2f}s during a slow JSON upload")
print(f"PASS: incomplete body connection closed in {closed_at - start:.2f}s")
if ws:
    body_finished = time.monotonic()
    deadline = body_finished + 7
    latest = baseline
    while time.monotonic() < deadline:
        ws.settimeout(max(.1, deadline - time.monotonic()))
        latest = next_snapshot(ws)
        if latest.get('sequence', 0) > baseline.get('sequence', 0):
            break
    ws.close()
    if latest.get('sequence', 0) <= baseline.get('sequence', 0):
        raise SystemExit('FAIL: no live snapshot after slow-body recovery')
    print('PASS: authenticated WebSocket delivered a newer snapshot after the slow-body deadline')
