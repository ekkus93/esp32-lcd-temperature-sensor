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
parser.add_argument('--retirement-only', action='store_true')
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
    # Owner login may need about 30 seconds for PBKDF2 on the MCU; leave the
    # same margin used by the authenticated device API test bridge.
    deadline = time.monotonic() + 68
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
        # History is rate limited per client (one query per second); wait and ask again.
        for _ in range(3):
            if response['status'] != 429:
                break
            time.sleep(1.1)
            response = call(path, headers=headers)
        check(response['status'] == 200, 'read ' + path)
        return json.loads(response['body'])

    snapshot = get('/status')
    check(snapshot['simulated'] is True and len(snapshot['sensors']) == 5,
          'explicit simulation image with five fake providers; safe to mutate')
    sensors = get('/sensors')
    if args.retirement_only:
        target_id = '2801000000000029'
        replacement_id = '28060000000000ac'
        expected_path = Path('.build/sensor-retirement-expected.json')
        if args.verify_reboot:
            expected = json.loads(expected_path.read_text())
            current = {item['rom_id']: item for item in sensors}
            check(target_id not in current and replacement_id in current,
                  'retired ROM stays absent and replacement remains after restart')
            check(all(rom_id in current and all(current[rom_id][key] == value
                                                for key, value in values.items())
                      for rom_id, values in expected['sensors'].items()),
                  'four retained mappings and the replacement defaults persisted')
            sys.exit(0)

        check(snapshot['simulated'] and len(sensors) == 5,
              'five-mapping simulation image is active')
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            target = next((item for item in sensors if item['rom_id'] == target_id), None)
            if target and target['state'] == 'disconnected':
                break
            time.sleep(.5)
            sensors = json.loads(call('/sensors', headers=headers)['body'])
        check(target is not None and target['state'] == 'disconnected',
              'saved ROM 1 reaches disconnected state in replacement simulation')
        retained = {item['rom_id']: item for item in sensors if item['rom_id'] != target_id}
        check(len(retained) == 4 and replacement_id not in retained,
              'replacement ROM is excess before the old slot is retired')
        connected = next(iter(retained.values()))
        check(call('/sensors/' + connected['rom_id'], 'DELETE', headers=origin)['status'] == 401,
              'unauthenticated retirement is rejected')
        check(call('/sensors/' + connected['rom_id'], 'DELETE', headers={
            **headers, 'If-Match': '"stale-revision"'})['status'] == 412,
              'stale retirement revision is rejected')
        check(call('/sensors/' + connected['rom_id'], 'DELETE',
                   headers={**headers, 'If-Match': connected['etag']})['status'] == 409,
              'connected probe retirement is rejected')
        check(call('/sensors/' + target_id, 'DELETE', headers=headers)['status'] == 428,
              'retirement requires a current sensor precondition')
        retirement = call('/sensors/' + target_id, 'DELETE',
                          headers={**headers, 'If-Match': target['etag']})
        check(retirement['status'] == 202 and
              json.loads(retirement['body']) == {'status': 'queued'},
              'disconnected ROM retirement is queued with the current revision')
        deadline = time.monotonic() + 15
        current = sensors
        while time.monotonic() < deadline:
            time.sleep(.5)
            current = json.loads(call('/sensors', headers=headers)['body'])
            ids = {item['rom_id'] for item in current}
            if target_id not in ids and replacement_id in ids:
                break
        check(target_id not in {item['rom_id'] for item in current} and
              replacement_id in {item['rom_id'] for item in current},
              'durable retirement frees capacity and admits the excess ROM')
        current_by_rom = {item['rom_id']: item for item in current}
        keys = ('name', 'short_name', 'enabled', 'disconnect_alert', 'order',
                'calibration_c', 'low_c', 'high_c', 'hysteresis_c', 'persistence_seconds')
        check(all(all(current_by_rom[rom_id][key] == before[key]
                      for key in keys)
                  for rom_id, before in retained.items()),
              'retirement preserves every remaining ROM-keyed configuration')
        replacement = current_by_rom[replacement_id]
        check(replacement['name'] == 'Sensor 1' and replacement['enabled'] is True and
              replacement['calibration_c'] == 0 and replacement['low_c'] is None and
              replacement['high_c'] is None,
              'new ROM receives neutral defaults instead of retired settings')
        expected_path.write_text(json.dumps({'sensors': {
            rom_id: {key: current_by_rom[rom_id][key] for key in keys}
            for rom_id in [*retained.keys(), replacement_id]}}) + '\n')
        print('Reboot the simulation image, then run --retirement-only --verify-reboot.',
              flush=True)
        sys.exit(0)
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
        # CSV export is public (one per 30 seconds per client), so no session is sent.
        exported = call(export_path)
        check(exported['status'] == 200, 'streamed five-minute CSV export without a session')
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
        expected = json.loads(saved_path.read_text())['sensors']
        current = {item['rom_id']: item for item in get('/sensors')}
        check(all(rom_id in current and all(current[rom_id][key] == value
                                            for key, value in values.items())
                  for rom_id, values in expected.items()),
              'two ROM-keyed settings persisted without cross-contamination through restart')
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
        sensor = get('/sensors')[0]
        second = get('/sensors')[1]
        second_path = '/sensors/' + second['rom_id']
        second_expected = {'name': 'Second simulation probe', 'short_name': 'SIM 2',
                           'calibration_c': 3.5, 'enabled': True, 'low_c': 5,
                           'high_c': 40, 'persistence_seconds': 0, 'hysteresis_c': 0.25}
        second_update = call(second_path, 'PATCH', second_expected,
                             {**headers, 'If-Match': second['etag']})
        check(second_update['status'] in (200, 202),
              'second ROM settings committed independently')
        time.sleep(.6)
        current = get('/sensors')
        by_rom = {item['rom_id']: item for item in current}
        check(all(by_rom[second['rom_id']][key] == value
                  for key, value in second_expected.items()),
              'second ROM retains its own distinct label, limits and calibration')
        live = get('/status')['sensors']
        live_by_rom = {item['rom_id']: item for item in live}
        expected_temperature = 19.0 + int(second['rom_id'][2:4], 16) + 3.5
        check(live_by_rom[second['rom_id']]['temperature_c'] == expected_temperature,
              'second ROM applies only its own calibration')
        saved_path.write_text(json.dumps({'sensors': {
            sensor['rom_id']: expected, second['rom_id']: second_expected}}) + '\n')
        check(len(get('/alerts')['events']) > 0, 'state transitions recorded once in bounded event stream')
        print('Reboot the simulation image, then run this script with --verify-reboot.', flush=True)
finally:
    port.close()
