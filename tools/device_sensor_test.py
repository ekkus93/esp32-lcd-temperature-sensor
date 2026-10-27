#!/usr/bin/env python3
"""Test sensor mutations on the explicitly simulated ESP32 image over USB.

Only simulation's isolated NVS namespace is modified. Credentials and transport
payloads stay private. Run --verify-reboot after resetting the same image.
"""
import argparse
import csv
import io
import json
import os
import sys
import time
from pathlib import Path

import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
parser.add_argument('--verify-reboot', action='store_true')
parser.add_argument('--history-only', action='store_true')
parser.add_argument('--settings-only', action='store_true')
args = parser.parse_args()
os.umask(0o077)
port = serial.Serial(port=None, baudrate=115200, timeout=.2)
port.dtr = port.rts = False
port.port = args.port
port.open()
deadline = time.monotonic() + 3
while time.monotonic() < deadline:
    if b'monitor: READY' in port.read(2048):
        break


def call(path, method='GET', body=None, headers=None):
    request = {'path': '/api/v1' + path, 'method': method, 'headers': headers or {}}
    if body is not None:
        request['body'] = body if isinstance(body, str) else json.dumps(body)
    port.write(('HTTP_BRIDGE ' + json.dumps(request) + '\n').encode())
    data = b''
    deadline = time.monotonic() + 38
    while time.monotonic() < deadline and b'HTTP_BRIDGE_END' not in data:
        data += port.read(4096)
    data = data.replace(b'\r', b'')
    start = data.find(b'HTTP_BRIDGE_BEGIN\n')
    finish = data.find(b'HTTP_BRIDGE_END', start)
    if start < 0 or finish < 0:
        raise RuntimeError('USB response missing; private payload suppressed')
    return json.loads(data[start + len(b'HTTP_BRIDGE_BEGIN\n'):finish])


def check(ok, name):
    if not ok:
        raise RuntimeError('FAIL: ' + name)
    print('PASS:', name, flush=True)


try:
    password = json.loads(Path('.build/device-admin.json').read_text())['password']
    origin = {'Origin': 'http://192.168.4.1'}
    login = call('/auth/login', 'POST', {'password': password}, origin)
    check(login['status'] == 200, 'administrator login')
    headers = {**origin, 'Cookie': login['cookie'].split(';')[0],
               'X-CSRF-Token': json.loads(login['body'])['csrf']}

    def get(path):
        response = call(path, headers=headers)
        check(response['status'] == 200, 'read ' + path)
        return json.loads(response['body'])

    snapshot = get('/status')
    check(snapshot['simulated'] is True and len(snapshot['sensors']) == 5,
          'explicit simulation image with five fake providers; safe to mutate')
    sensors = get('/sensors')
    sensor = sensors[0]
    if args.settings_only:
        settings = get('/settings')
        expected_path = Path('.build/device-preferences-expected.json')
        if args.verify_reboot:
            expected = json.loads(expected_path.read_text())
            check(all(settings[key] == value for key, value in expected.items()),
                  'device preferences persisted across ESP32 restart')
        else:
            check(call('/settings', 'PATCH', {'units':'F'}, headers)['status'] == 428,
                  'device preference precondition required')
            matched = {**headers, 'If-Match': settings['etag']}
            for bad in [{'overview_seconds':4}, {'lookback_minutes':1441},
                        {'timezone':'unknown/timezone'}, {'ntp_server':'http://time.example.org'},
                        {'schema_version':2}, '{"units":"C","units":"F"}']:
                check(call('/settings','PATCH',bad,matched)['status'] == 400,
                      'malformed device preference rejected')
            expected = {'units':'F', 'display_mode':'extended', 'overview_seconds':25,
                        'trends_seconds':15, 'lookback_minutes':360,
                        'timezone':'America/Los_Angeles', 'ntp_server':'time.example.org'}
            changed = call('/settings','PATCH',expected,matched)
            check(changed['status'] == 200, 'device preferences committed and clock configuration applied')
            check(call('/settings','PATCH',{'units':'C'},matched)['status'] == 412,
                  'stale device preference edit rejected')
            current = get('/settings')
            check(all(current[key] == value for key, value in expected.items()),
                  'device preferences share the persisted service model')
            expected_path.write_text(json.dumps(expected)+'\n')
        sys.exit(0)
    if args.history_only:
        query = '/history?from=0&to=' + str(snapshot['uptime_ms'] + 300000)
        query += '&boot_id=' + snapshot['boot_id'] + '&max_points=2'
        result = get(query)
        check(result['simulated'] and result['time_basis'] == 'uptime',
              'history records explicitly identify simulated unsynchronized time')
        check(0 < len(result['points']) <= 10, 'persisted five-minute buckets reduced within point bound')
        check(all(p['minimum_c'] <= p['average_c'] <= p['maximum_c'] and p['count'] > 0
                  for p in result['points']), 'history extrema and counts are consistent')
        filtered = get(query + '&rom_id=' + sensor['rom_id'])
        check(all(p['rom_id'] == sensor['rom_id'] for p in filtered['points']), 'ROM history selector')
        check(call('/history?from=0&to=1000000&max_points=999', headers=headers)['status'] == 400,
              'unbounded history request rejected')
        check(call('/history?from=0&from=1&to=1000000', headers=headers)['status'] == 400,
              'ambiguous duplicate query rejected')
        check(not get('/history?from=0&to=1000000&boot_id=0000000000000000')['points'],
              'unknown boot never borrows another boot history')
        export_path = '/history/export?from=0&to=' + str(snapshot['uptime_ms'] + 300000) + '&boot_id=' + snapshot['boot_id']
        check(call(export_path)['status'] == 401, 'CSV export requires an administrator session')
        exported = call(export_path, headers=headers)
        check(exported['status'] == 200, 'streamed five-minute CSV export')
        rows = list(csv.DictReader(io.StringIO(exported['body'])))
        check(rows and all(row['simulated']=='1' and row['units']=='C' and row['timestamp_quality']=='boot_uptime'
              and row['utc_seconds']=='' for row in rows), 'CSV identifies simulation and unknown UTC without inventing time')
        check(all(row['sensor_name'] and row['name_source']=='recorded' and int(row['state_flags'],16)>0 for row in rows),
              'version-two persisted names and state flags exported from LittleFS')
        check(call(export_path+'&max_points=2',headers=headers)['status']==400,
              'CSV refuses graph downsampling parameters')
        sys.exit(0)
    path = '/sensors/' + sensor['rom_id']
    saved_path = Path('.build/sensor-persistence.json')
    if args.verify_reboot:
        expected = json.loads(saved_path.read_text())
        check(all(sensor[key] == value for key, value in expected.items()),
              'ROM settings persisted through ESP32 restart')
    else:
        def patch(body, expected=202, etag=None):
            response = call(path, 'PATCH', body,
                            {**headers, 'If-Match': etag or sensor['etag']})
            check(response['status'] == expected, 'mutation HTTP ' + str(expected))

        check(call(path, 'PATCH', {'name': 'test'}, headers)['status'] == 428,
              'missing update precondition rejected')
        patch({'low_c': 30, 'high_c': 20}, 400)
        patch({'rom_id': sensors[-1]['rom_id']}, 400)
        patch('{"name":"one","name":"two"}', 400)
        old_etag = sensor['etag']
        expected = {'name': 'USB simulation probe', 'short_name': 'SIM probe',
                    'calibration_c': -2.5, 'enabled': True, 'low_c': None,
                    'high_c': 10, 'persistence_seconds': 0, 'hysteresis_c': 1}
        patch(expected)
        time.sleep(.6)
        sensor = get('/sensors')[0]
        check(all(sensor[key] == value for key, value in expected.items()),
              'settings applied by acquisition owner')
        patch({'name': 'stale overwrite'}, 412, old_etag)
        live = get('/status')['sensors'][0]
        check(live['state'] == 'high' and live['temperature_c'] == 17.5,
              'calibration and high alarm share live service model')
        check(any(item.get('rom_id') == sensor['rom_id'] for item in get('/alerts')['active']),
              'active alert published')
        patch({'enabled': False})
        time.sleep(.6)
        live = get('/status')['sensors'][0]
        check(live['state'] == 'disabled' and live['temperature_c'] is None,
              'disabled sensor has no invented live value')
        check(not any(item.get('rom_id') == sensor['rom_id'] for item in get('/alerts')['active']),
              'disable clears active alarm')
        sensor = get('/sensors')[0]
        patch({'enabled': True})
        time.sleep(.6)
        saved_path.write_text(json.dumps(expected) + '\n')
        check(len(get('/alerts')['events']) > 0, 'state transitions recorded once in bounded event stream')
        print('Reboot the simulation image, then run this script with --verify-reboot.', flush=True)
finally:
    port.close()
