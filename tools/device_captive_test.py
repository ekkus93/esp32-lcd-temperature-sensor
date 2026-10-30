#!/usr/bin/env python3
"""Check captive HTTP/DNS on the MCU's AP interface through physical USB.

This verifies local protocol handling, not phone onboarding or Wi-Fi association.
"""
import argparse
import json
from pathlib import Path
from device_client import DeviceClient

parser=argparse.ArgumentParser()
parser.add_argument('--port',required=True)
parser.add_argument('--admin-file',default='.build/device-admin.json')
args=parser.parse_args()


def check(ok,name):
    if not ok:
        raise RuntimeError('FAIL: '+name)
    print('PASS:',name,flush=True)


with DeviceClient(args.port) as device:
    password=json.loads(Path(args.admin_file).read_text())['password']
    origin='http://192.168.4.1'
    login=device.http('/api/v1/auth/login','POST',{'password':password},
                      {'Origin':origin,'Content-Type':'application/json'})
    check(login['status']==200,'USB admin login')
    authorized={'Origin':origin,'Cookie':login.get('cookie','').split(';',1)[0],
                'X-CSRF-Token':json.loads(login['body'])['csrf']}
    wifi=json.loads(device.http('/api/v1/wifi/status',headers=authorized)['body'])
    opened=not wifi['provisioning']
    if opened:
        check(device.http('/api/v1/wifi/provisioning','POST',{},authorized)['status']==202,
              'setup AP opened through authenticated API')
        for _ in range(20):
            wifi=json.loads(device.http('/api/v1/wifi/status',headers=authorized)['body'])
            if wifi['provisioning']:
                break
        check(wifi['provisioning'],'setup AP ready in the same USB session')
    try:
        for path in ['/generate_204','/hotspot-detect.html','/connecttest.txt','/ncsi.txt']:
            response=device.http(path,headers={'Host':'connectivitycheck.example.invalid'},headers_only=True)
            check(response['status']==302 and response.get('location')=='http://192.168.4.1/setup',
                  'narrow OS captive probe redirects '+path)
        check(device.http('/api/v1/status',headers={'Host':'connectivitycheck.example.invalid'})['status']==403,
              'captive Host exception never grants API access')
        check(device.http('/setup',headers={'Accept-Encoding':'gzip'},headers_only=True)['status']==200,
              'offline setup app served directly from device')
        packet=[0x12,0x34,1,0,0,1,0,0,0,0,0,0,7,*b'example',7,*b'invalid',0,0,1,0,1]
        response=device.call('DNS_BRIDGE',{'bytes':packet},timeout=6)
        check(response['ok'] and response['bytes'][-4:]==[192,168,4,1] and response['bytes'][:2]==[0x12,0x34],
              'real UDP DNS A answer on AP interface with preserved transaction ID')
        check(response['bytes'][-10:-6]==[0,0,0,0], 'captive address has zero cache lifetime')
        packet[-3]=28
        response=device.call('DNS_BRIDGE',{'bytes':packet},timeout=6)
        check(response['ok'] and response['bytes'][6:8]==[0,0], 'AAAA query has explicit no-data reply')
        packet[-3]=1
        packet[12]=0xc0
        response=device.call('DNS_BRIDGE',{'bytes':packet},timeout=6)
        check(not response['ok'], 'malformed compressed DNS query ignored without crashing')
    finally:
        if opened:
            reboot=device.http('/api/v1/system/reboot','POST','',authorized)
            if reboot['status']!=202:
                print('WARN: setup AP cleanup reboot was not accepted',flush=True)
