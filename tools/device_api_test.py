#!/usr/bin/env python3
"""Exercise the real ESP32 HTTP/auth stack via a physical USB loopback bridge.

The bridge does not test Wi-Fi radio behavior. Secrets remain in private files;
request/response payloads are never printed by this test runner.
"""
import argparse
import json
import os
import secrets
import re
import gzip
import time
from pathlib import Path
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
parser.add_argument('--setup-file', default='.build/device-setup.json')
parser.add_argument('--admin-file', default='.build/device-admin.json')
parser.add_argument('--fresh-enrollment', action='store_true', help='Reset only a test-managed administrator and re-enroll it')
parser.add_argument('--change-password', action='store_true', help='Rotate only the test-managed administrator and update its private file')
parser.add_argument('--candidate-failure', action='store_true', help='Test a nonexistent SSID only when no real station credentials are saved')
args = parser.parse_args()
os.umask(0o077)
setup = json.loads(Path(args.setup_file).read_text())
admin_path = Path(args.admin_file)
if admin_path.exists():
    password = json.loads(admin_path.read_text())['password']
else:
    password = secrets.token_urlsafe(24)
    admin_path.parent.mkdir(parents=True, exist_ok=True)
    admin_path.write_text(json.dumps({'password':password,'managed_by_device_tests':True})+'\n')
    admin_path.chmod(0o600)
port = serial.Serial(port=None, baudrate=115200, timeout=.5)
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
health_traffic = bytearray()
def call(path, method='GET', body=None, headers=None, binary=False, headers_only=False):
    request = {'path':path, 'method':method, 'headers':headers or {},'binary':binary,'headers_only':headers_only}
    if body is not None:
        request['body'] = body if isinstance(body,str) else json.dumps(body)
    encoded = ('HTTP_BRIDGE '+json.dumps(request)+'\n').encode()
    if len(encoded) >= 4096:
        raise RuntimeError('Test request exceeds bridge limit')
    port.write(encoded)
    data = b''
    end = time.monotonic()+68
    while time.monotonic()<end and b'HTTP_BRIDGE_END' not in data:
        data += port.read(2048)
    health_traffic.extend(data)
    data = data.replace(b'\r',b'')
    for diagnostic in re.findall(rb'Password KDF unavailable: -?[0-9]+', data):
        print(diagnostic.decode(), flush=True)
    start = data.find(b'HTTP_BRIDGE_BEGIN\n')
    finish = data.find(b'HTTP_BRIDGE_END',start)
    if start<0 or finish<0:
        raise RuntimeError('USB bridge response missing (request details suppressed)')
    for metric in re.findall(rb'PBKDF2 iterations=[0-9]+ elapsed_ms=[0-9]+', data):
        print(metric.decode(), flush=True)
    response = json.loads(data[start+len(b'HTTP_BRIDGE_BEGIN\n'):finish].strip())
    return response

def check(condition, name):
    if not condition:
        raise RuntimeError('FAIL: '+name)
    print('PASS:',name,flush=True)

origin = {'Origin':'http://192.168.4.1'}
try:
    if args.fresh_enrollment:
        if not json.loads(admin_path.read_text()).get('managed_by_device_tests'):
            raise RuntimeError('Fresh enrollment requires a test-managed account; existing owner credentials were preserved')
        port.write(b'RESET_ADMIN_CREDENTIALS CONFIRM\n')
        reset_data=b''; end=time.monotonic()+8
        while time.monotonic()<end:
            reset_data+=port.read(2048)
            if b'ADMIN_RESET_OK' in reset_data and b'monitor: READY' in reset_data[reset_data.find(b'ADMIN_RESET_OK'):]:break
        check(b'ADMIN_RESET_OK' in reset_data,'explicit physical-USB credential recovery')
        port.write(b'SETUP_INFO\n')
        data=b'';end=time.monotonic()+8
        while time.monotonic()<end and b'SETUP_INFO_END' not in data:data+=port.read(2048)
        data=data.replace(b'\r',b'')
        start=data.find(b'SETUP_INFO_BEGIN\n');finish=data.find(b'SETUP_INFO_END',start)
        if start<0 or finish<0:raise RuntimeError('Fresh setup response missing')
        setup=json.loads(data[start+len(b'SETUP_INFO_BEGIN\n'):finish].strip())
        setup_path=Path(args.setup_file);setup_path.write_text(json.dumps(setup)+'\n');setup_path.chmod(0o600)

    page=call('/',headers={'Accept-Encoding':'gzip'},binary=True)
    check(page['status']==200 and page.get('content_encoding')=='gzip','embedded app served with gzip')
    html=bytes(page['bytes'])
    if html.startswith(b'\x1f\x8b'):html=gzip.decompress(html)
    check(b'<title>Temperature Monitor</title>' in html,'embedded HTML content')
    script=re.search(rb'src="([^"]+\.js)"',html)
    check(bool(script),'hashed application script in HTML')
    asset=call(script[1].decode(),headers={'Accept-Encoding':'gzip'},headers_only=True)
    check(asset['status']==200 and asset.get('content_encoding')=='gzip','hashed JavaScript served without SPA fallback')
    check(call('/api/v1/status')['status']==401,'anonymous status rejected')
    check(call('/api/v1/auth/state',headers={'Host':'attacker.invalid'})['status']==403,'forged Host rejected')
    state = json.loads(call('/api/v1/auth/state')['body'])
    if not state['enrolled']:
        check(bool(setup.get('setup_secret')),'physical setup secret available')
        result = call('/api/v1/auth/enroll','POST',{'setup_secret':setup['setup_secret'],'password':password},origin)
        check(result['status']==201,f'one-time owner enrollment (HTTP {result["status"]})')
    result = call('/api/v1/auth/login','POST',{'password':password},origin)
    check(result['status']==200,'PBKDF2 password verification and login')
    cookie = result['cookie'].split(';',1)[0]
    csrf = json.loads(result['body'])['csrf']
    authorized = {**origin,'Cookie':cookie,'X-CSRF-Token':csrf}
    check(call('/api/v1/status',headers=authorized)['status']==200,'authenticated snapshot')
    check(call('/api/v1/ota','POST','',origin)['status']==401,'anonymous OTA upload rejected')
    check(call('/api/v1/ota','POST','',{**origin,'Cookie':cookie})['status']==401,
          'OTA upload requires CSRF authorization')
    check(call('/api/v1/ota','POST','',authorized)['status']==400,
          'empty OTA image rejected before flash access')
    check(call('/api/v1/ota','POST','not an image',
               {**authorized,'Content-Type':'application/json'})['status']==415,
          'OTA rejects the wrong media type before flash access')
    check(call('/api/v1/ota','POST','X'*128,
               {**authorized,'Content-Type':'application/octet-stream'})['status']==422,
          'invalid firmware image is rejected without selecting it for boot')
    if args.change_password:
        record = json.loads(admin_path.read_text())
        check(record.get('managed_by_device_tests') is True, 'credential rotation limited to test-managed account')
        updated_password = secrets.token_urlsafe(24)
        record['pending_password'] = updated_password
        admin_path.write_text(json.dumps(record)+'\n')
        result = call('/api/v1/auth/password','POST',{'old_password':password,'new_password':updated_password},authorized)
        check(result['status']==200,'administrator password changed through real slow-hash path')
        record['password'] = updated_password
        del record['pending_password']
        admin_path.write_text(json.dumps(record)+'\n')
        check(call('/api/v1/status',headers=authorized)['status']==401,'password change invalidates existing session')
        password = updated_password
        time.sleep(.6)
        result = call('/api/v1/auth/login','POST',{'password':password},origin)
        check(result['status']==200,'new password authenticates after rotation')
        cookie = result['cookie'].split(';',1)[0]
        csrf = json.loads(result['body'])['csrf']
        authorized = {**origin,'Cookie':cookie,'X-CSRF-Token':csrf}
    check(call('/api/v1/wifi/scan','POST',{},authorized)['status']==202,'asynchronous Wi-Fi scan accepted')
    check(call('/api/v1/wifi/scan','POST',{},authorized)['status']==429,'Wi-Fi scan frequency bounded')
    scan = None
    for attempt in range(12):
        time.sleep(.5)
        scan = json.loads(call('/api/v1/wifi/scan',headers=authorized)['body'])
        if not scan['scanning']: break
    check(scan is not None and not scan['scanning'] and 0<len(scan['networks'])<=20,'bounded real radio scan completed')
    if args.candidate_failure:
        before = json.loads(call('/api/v1/wifi/status',headers=authorized)['body'])
        check(before['saved_credentials'] is False, 'failed-candidate test preserves owner networks by requiring blank station configuration')
        candidate = {'ssid':'TM-test-'+secrets.token_hex(10),'password':'test-only-password'}
        check(call('/api/v1/wifi/connect','POST',candidate,authorized)['status']==202,'candidate connection accepted asynchronously')
        check(call('/api/v1/wifi/connect','POST',candidate,authorized)['status']==409,'overlapping candidate rejected')
        progress = None
        for attempt in range(35):
            time.sleep(1)
            progress = json.loads(call('/api/v1/wifi/status',headers=authorized)['body'])
            if progress['attempt']=='failed': break
        check(progress is not None and progress['attempt']=='failed' and progress['provisioning'] is True
              and progress['saved_credentials'] is False and progress['error'],
              'failed association leaves setup available and prior credentials unchanged')
    upgrade={'Upgrade':'websocket','Connection':'Upgrade','Sec-WebSocket-Version':'13','Sec-WebSocket-Key':'dGhlIHNhbXBsZSBub25jZQ==',**origin}
    check(call('/api/v1/live',headers=upgrade)['status']==403,'anonymous WebSocket upgrade rejected')
    check(call('/api/v1/live',headers={**upgrade,'Cookie':cookie})['status']==101,'authenticated WebSocket upgrade')
    check(call('/api/v1/live',headers={**upgrade,'Cookie':cookie,'Origin':'http://attacker.invalid'})['status']==403,'forged WebSocket Origin rejected')
    snapshot = json.loads(call('/api/v1/status',headers=authorized)['body'])
    Path('.build/device-snapshot.json').write_text(json.dumps(snapshot)+'\n')
    check(snapshot['sensors']==[] and snapshot['simulated'] is False,'real bare-board snapshot has no fabricated readings')
    history_path = ('/api/v1/history?from=0&to='+str(snapshot['uptime_ms'])+
                    '&boot_id='+snapshot['boot_id'])
    history_response = call(history_path,headers=authorized)
    history = json.loads(history_response['body'])
    check(history_response['status']==200 and history['points']==[] and history['gaps']==[],
          'history contract returns explicit gap list on empty device history')
    alerts_response = call('/api/v1/alerts?limit=10',headers=authorized)
    alerts = json.loads(alerts_response['body'])
    check(alerts_response['status']==200 and alerts['event_log_healthy'] is True
          and any(event['kind']=='reboot' for event in alerts['events'])
          and alerts['has_more'] is False and alerts['oldest_available'] is not None,
          'authenticated durable event journal includes this boot')
    first_alert_page = alerts
    next_alerts = json.loads(call('/api/v1/alerts?after='+str(first_alert_page['next_after'])+
                                  '&limit=10',headers=authorized)['body'])
    check(next_alerts['events']==[] and next_alerts['has_more'] is False,
          'durable event journal cursor reaches the current end')
    check(call('/api/v1/alerts?limit=51',headers=authorized)['status']==400,
          'alert pagination limit is bounded')
    check(call('/api/v1/wifi/provisioning','POST',{}, {**origin,'Cookie':cookie})['status']==401,'missing CSRF rejected')
    check(call('/api/v1/wifi/provisioning','POST',{}, {**authorized,'Origin':'http://attacker.invalid'})['status']==403,'forged Origin rejected')
    check(call('/api/v1/wifi/connect','POST','{bad json',authorized)['status']==400,'malformed JSON rejected')
    nested='{"x":'+('['*30)+'0'+(']'*30)+'}'
    check(call('/api/v1/wifi/connect','POST',nested,authorized)['status']==400,'excessive JSON nesting rejected')
    check(call('/api/v1/wifi/connect','POST',{'ssid':'x'*33,'password':'irrelevant-password'},authorized)['status']==400,'oversized SSID rejected')
    check(call('/api/v1/auth/enroll','POST',{'setup_secret':setup.get('setup_secret',''),'password':password},origin)['status']==403,'bootstrap enrollment cannot be reused')
    check(call('/api/v1/system/reboot','POST',None,origin)['status']==401,
          'anonymous safe-restart request rejected')
    check(call('/api/v1/system/reboot','POST',None,
               {**origin,'Cookie':cookie})['status']==401,
          'safe restart requires CSRF authorization')
    check(call('/api/v1/system/reboot','POST',{'unexpected':True},authorized)['status']==400,
          'safe restart rejects request bodies')
    reboot_login=call('/api/v1/auth/login','POST',{'password':password},origin)
    check(reboot_login['status']==200,'second administrator session for restart acceptance')
    reboot_cookie=reboot_login['cookie'].split(';',1)[0]
    reboot_csrf=json.loads(reboot_login['body'])['csrf']
    reboot_authorized={**origin,'Cookie':reboot_cookie,'X-CSRF-Token':reboot_csrf}
    check(call('/api/v1/auth/logout','POST',{},authorized)['status']==200,'logout')
    check(call('/api/v1/status',headers=authorized)['status']==401,'logged-out cookie invalidated')
    check(call('/api/v1/not-a-route')['status']==401,'unknown API remains behind authentication')
    result=call('/api/v1/auth/login','POST',{'password':'intentionally-wrong-test-value'},origin)
    check(result['status']==401,'invalid password rejected')
    began=time.monotonic()
    result=call('/api/v1/auth/login','POST',{'password':'intentionally-wrong-test-value'},origin)
    check(result['status']==401 and time.monotonic()-began<2,'failed-login cooldown avoids repeated expensive work')
    times=[int(value) for value in re.findall(rb'HEALTH uptime_ms=([0-9]+)',health_traffic)]
    Path('.build/auth-cadence.json').write_text(json.dumps({'uptime_samples':times,'deltas':[b-a for a,b in zip(times,times[1:])]},indent=2)+'\n')
    check(len(times)>=2 and all(4000<=b-a<=6000 for a,b in zip(times,times[1:])) and all(abs(value-(times[0]+index*5000))<=1000 for index,value in enumerate(times)), 'sampling cadence survives password verification')
    restart=call('/api/v1/system/reboot','POST',None,reboot_authorized)
    check(restart['status']==202 and json.loads(restart['body'])['status']=='rebooting',
          'authenticated safe restart is acknowledged')
    restart_output=bytearray()
    restart_deadline=time.monotonic()+20
    while time.monotonic()<restart_deadline:
        restart_output.extend(port.read(1024))
        if b'monitor: READY no peripherals required' in restart_output:
            break
    check(b'monitor: READY no peripherals required' in restart_output,
          'device restarts and returns to READY after safe-restart acceptance')
    check(call('/api/v1/status',headers=reboot_authorized)['status']==401,
          'restart invalidates the prior in-memory administrator session')
    print('Device API tests passed; administrator password saved privately at',admin_path)
finally:
    port.close()
