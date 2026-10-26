"""Private physical-USB bridge client shared by device integration checks."""
import json
import time
import serial


class DeviceClient:
    def __init__(self, path):
        self.port = serial.Serial(port=None, baudrate=115200, timeout=.2)
        self.port.dtr = self.port.rts = False
        self.port.port = path
        self.port.open()
        self.traffic = bytearray()
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if b'monitor: READY' in self.port.read(2048):
                break

    def call(self, command, request, timeout=68):
        if command not in ('HTTP_BRIDGE', 'DNS_BRIDGE'):
            raise ValueError('Unsupported device test command')
        encoded = (command + ' ' + json.dumps(request) + '\n').encode()
        if len(encoded) >= 4096:
            raise ValueError('USB request exceeds the bounded console frame')
        self.port.write(encoded)
        begin, end = (command + '_BEGIN\n').encode(), (command + '_END').encode()
        data = bytearray()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and end not in data:
            data.extend(self.port.read(2048))
            if len(data) > 65536:
                raise RuntimeError('USB response exceeded its limit; private payload suppressed')
        self.traffic.extend(data)
        data = data.replace(b'\r', b'')
        start = data.find(begin)
        finish = data.find(end, start)
        if start < 0 or finish < 0:
            raise RuntimeError('USB bridge response missing; private payload suppressed')
        return json.loads(data[start + len(begin):finish])

    def http(self, path, method='GET', body=None, headers=None, headers_only=False):
        request = {'path':path,'method':method,'headers':headers or {},'headers_only':headers_only}
        if body is not None:
            request['body'] = body if isinstance(body,str) else json.dumps(body)
        return self.call('HTTP_BRIDGE',request)

    def close(self):
        self.port.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
