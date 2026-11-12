#!/usr/bin/env python3
"""Submit a wrong Wi-Fi network; readings return on the LCD; the setup AP closes after the window (R03-08).

Works through the USB bridge (opening the port restarts the board), signs in with the private admin
file, submits a network that does not exist, then for 12 minutes polls the Wi-Fi status and records
the console's LCD view changes. Prints a JSON summary. The console's Wi-Fi log lines can name the
saved network: keep the output private.
"""
import json, re, time
from pathlib import Path
import serial

port = serial.Serial(port=None, baudrate=115200, timeout=.5)
port.dtr = False; port.rts = False; port.port = '/dev/ttyACM0'; port.open()
t0 = time.monotonic()
log = []  # (seconds, line)


def take(data):
    for line in data.replace(b'\r', b'').split(b'\n'):
        text = line.decode('utf-8', 'replace').strip()
        if text:
            log.append((round(time.monotonic() - t0, 1), text))


boot = b''
while time.monotonic() - t0 < 15 and b'monitor: READY' not in boot:
    boot += port.read(4096)
take(boot)


def call(path, method='GET', body=None, headers=None):
    request = {'path': path, 'method': method, 'headers': headers or {}, 'binary': False,
               'headers_only': False}
    if body is not None:
        request['body'] = json.dumps(body)
    port.write(('HTTP_BRIDGE ' + json.dumps(request) + '\n').encode())
    data = b''
    end = time.monotonic() + 68
    while time.monotonic() < end and b'HTTP_BRIDGE_END' not in data:
        data += port.read(2048)
    start = data.find(b'HTTP_BRIDGE_BEGIN')
    take(data[:start] if start >= 0 else data)
    data = data.replace(b'\r', b'')
    s = data.find(b'HTTP_BRIDGE_BEGIN\n'); f = data.find(b'HTTP_BRIDGE_END', s)
    return json.loads(data[s + len(b'HTTP_BRIDGE_BEGIN\n'):f].strip())


origin = {'Origin': 'http://192.168.4.1'}
# Let Wi-Fi connect after the restart the port open caused.
while time.monotonic() - t0 < 60:
    status = json.loads(call('/api/v1/status', headers=origin)['body'])
    if status['wifi']['state'] == 'connected':
        break
    time.sleep(3)
password = json.loads(Path('.build/device-admin.json').read_text())['admin_password']
login = call('/api/v1/auth/login', 'POST', {'password': password}, origin)
cookie = login['cookie'].split(';', 1)[0]
csrf = json.loads(login['body'])['csrf']
auth = {**origin, 'Cookie': cookie, 'X-CSRF-Token': csrf}
before = json.loads(call('/api/v1/wifi/status', headers=auth)['body'])
submit = call('/api/v1/wifi/connect', 'POST',
              {'ssid': 'r03-08-no-such-network', 'password': 'not-a-real-password'}, auth)
submitted_at = time.monotonic()
samples = []
while time.monotonic() - submitted_at < 12 * 60:
    r = call('/api/v1/wifi/status', headers=auth)
    if r['status'] == 401:  # session ended (it lasts an hour); sign in again
        login = call('/api/v1/auth/login', 'POST', {'password': password}, origin)
        auth = {**origin, 'Cookie': login['cookie'].split(';', 1)[0],
                'X-CSRF-Token': json.loads(login['body'])['csrf']}
        continue
    w = json.loads(r['body'])
    samples.append((round(time.monotonic() - submitted_at), w.get('state'), w.get('provisioning'),
                    w.get('attempt'), w.get('saved_credentials')))
    time.sleep(10)
lcd = [(s, l) for s, l in log if 'LCD_VIEW' in l]
wifi_lines = [(s, l[:140]) for s, l in log if re.search(r'wifi|Wi-Fi|setup|AP ', l, re.I)
              and 'HEALTH' not in l and 'SENSOR_DIAG' not in l]
offset = submitted_at - t0
summary = {
    'submit_status': submit['status'],
    'wifi_before': {k: before.get(k) for k in ('state', 'provisioning', 'saved_credentials')},
    'samples_s_state_provisioning_attempt_saved': samples,
    'lcd_views_after_submit': [(round(s - offset), l.split('LCD_VIEW ', 1)[1]) for s, l in lcd if s >= offset],
    'wifi_log_after_submit': [(round(s - offset), l) for s, l in wifi_lines if s >= offset][:40],
}
print(json.dumps(summary, indent=1))
