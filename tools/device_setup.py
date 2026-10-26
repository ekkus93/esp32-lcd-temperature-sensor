#!/usr/bin/env python3
"""Retrieve per-device setup credentials through physical USB into a private file."""
import argparse
import json
import os
import time
from pathlib import Path
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
parser.add_argument('--output', default='.build/device-setup.json')
parser.add_argument('--wifi-file', help='Optional owner-supplied network credentials to submit over USB')
args = parser.parse_args()
os.umask(0o077)
port = serial.Serial(port=None, baudrate=115200, timeout=0.5)
port.dtr = False
port.rts = False
port.port = args.port
port.open()
# Native USB control-line changes can restart the S3. Wait for its console task.
ready_deadline = time.monotonic() + 3
ready_data = b''
while time.monotonic() < ready_deadline:
    ready_data += port.read(1024)
    if b'monitor: READY' in ready_data:
        break
try:
    port.write(b'SETUP_INFO\n')
    end = time.monotonic() + 15
    data = b''
    while time.monotonic() < end and b'SETUP_INFO_END' not in data:
        data += port.read(1024)
    data = data.replace(b'\r', b'')
    start = data.find(b'SETUP_INFO_BEGIN\n')
    finish = data.find(b'SETUP_INFO_END', start)
    if start < 0 or finish < 0:
        raise SystemExit('No setup response; check firmware/USB console. No credentials were printed.')
    response = json.loads(data[start+len(b'SETUP_INFO_BEGIN\n'):finish].strip())
    path = Path(args.output)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    os.fchmod(fd, 0o600)
    with os.fdopen(fd, 'w') as output:
        json.dump(response, output, indent=2)
        output.write('\n')
    path.chmod(0o600)
    print(f'Saved device setup credentials to {path} (mode 0600).')
    if args.wifi_file:
        credentials = json.loads(Path(args.wifi_file).read_text())
        command = json.dumps({'ssid':credentials['ssid'], 'password':credentials['password']})
        if len(command.encode()) > 480:
            raise SystemExit('Wi-Fi command too large')
        port.write(('WIFI_CONNECT '+command+'\n').encode())
        end = time.monotonic() + 10
        data = b''
        while time.monotonic() < end and b'WIFI_CONNECT_RESULT' not in data:
            data += port.read(1024)
        if b'WIFI_CONNECT_RESULT accepted' not in data:
            raise SystemExit('Wi-Fi candidate was not accepted; credentials were not printed.')
        print('Submitted candidate network; previous credentials remain until DHCP succeeds.')
finally:
    port.close()
