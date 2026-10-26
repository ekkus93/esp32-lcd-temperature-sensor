#!/usr/bin/env python3
"""Observe the bare-board firmware without resetting or changing GPIOs."""
import argparse
import re
import sys
import time
from pathlib import Path
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
parser.add_argument('--seconds', type=float, default=20)
parser.add_argument('--output', default='.build/device-smoke.log')
args = parser.parse_args()
port = serial.Serial(port=None, baudrate=115200, timeout=0.5)
# Configure before open: opening with asserted DTR/RTS can enter ROM download mode.
port.dtr = False
port.rts = False
port.port = args.port
port.open()
log = bytearray()
end = time.monotonic() + args.seconds
try:
    while time.monotonic() < end:
        log.extend(port.read(4096))
finally:
    port.close()
text = log.decode('utf-8', errors='replace')
path = Path(args.output)
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(text)
print(text)
health = re.findall(r'HEALTH uptime_ms=(\d+) sensors=(\d+).*heap=(\d+)', text)
if len(health) < 2:
    sys.exit('FAIL: fewer than two health samples; inspect boot/reset/serial state')
if any(int(row[1]) != 0 or int(row[2]) <= 0 for row in health):
    sys.exit('FAIL: unexpected bare-board sensor count or heap')
if any(int(b[0])-int(a[0]) not in range(4900, 6001) for a,b in zip(health, health[1:])):
    sys.exit('FAIL: health task cadence outside 4.9–6 seconds')
if re.search(r'Guru Meditation|abort\(\)|panic', text, re.I):
    sys.exit('FAIL: device panic')
print('PASS: bare-board health cadence, no fabricated probes, no panic')
