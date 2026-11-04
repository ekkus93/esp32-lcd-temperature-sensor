#!/usr/bin/env python3
"""Measure populated history/export latency and memory on the MCU simulation image."""
import argparse
import http.client
import json
import os
import statistics
import threading
import time
from pathlib import Path

SIMULATED_SAMPLE_MS = 5_000
SIMULATED_TICK_REAL_MS = 250
BUCKET_MS = 300_000
MAX_QUERY_RANGE_MS = 30 * 24 * 60 * 60 * 1000


def simulated_history_window(uptime_ms, duration_seconds):
    """Return a bounded query range covering accelerated samples through test end."""
    end_ms = ((uptime_ms + duration_seconds * 1000) * SIMULATED_SAMPLE_MS
              // SIMULATED_TICK_REAL_MS) + BUCKET_MS
    return max(0, end_ms - MAX_QUERY_RANGE_MS), end_ms


def main():
    from device_client import DeviceClient

    parser = argparse.ArgumentParser()
    parser.add_argument('--port', required=True)
    parser.add_argument('--seconds', type=int, default=120)
    parser.add_argument('--output', default='docs/validation/device-history-profile-2026-10-31.json')
    args = parser.parse_args()
    if not 30 <= args.seconds <= 300:
        parser.error('--seconds must be 30..300')
    os.umask(0o077)
    password = json.loads(Path('.build/device-admin.json').read_text())['password']
    with DeviceClient(args.port) as device:
        origin = {'Origin': 'http://192.168.4.1'}
        login = device.http('/api/v1/auth/login', 'POST', {'password': password}, origin)
        if login['status'] != 200:
            raise RuntimeError('admin login failed')
        headers = {**origin, 'Cookie': login['cookie'].split(';')[0]}
        csrf = json.loads(login['body'])['csrf']
        headers['X-CSRF-Token'] = csrf
        snapshot_response = device.http('/api/v1/status', headers=headers)
        snapshot = json.loads(snapshot_response['body'])
        if snapshot_response['status'] != 200 or not snapshot['simulated'] or len(snapshot['sensors']) != 5:
            raise RuntimeError('refusing profile unless the five-provider simulation image is active')
        logging_status = snapshot.get('logging', {})
        if not logging_status.get('healthy') or not logging_status.get('storage_available'):
            raise RuntimeError('refusing profile unless history storage is healthy and available')
        initial_storage_bytes = logging_status.get('storage_used_bytes')
        if not isinstance(initial_storage_bytes, int) or initial_storage_bytes < 0:
            raise RuntimeError('device omitted its history storage usage measurement')
        network_response = device.http('/api/v1/wifi/status', headers=headers)
        network = json.loads(network_response['body'])
        host = network['ip']
        if network_response['status'] != 200 or not host or host == '0.0.0.0':
            raise RuntimeError('device has no station address for concurrent network profiling')
        cookie = headers['Cookie']
        boot_id = snapshot['boot_id']
        from_ms, end_ms = simulated_history_window(snapshot['uptime_ms'], args.seconds)
        history_path = f'/api/v1/history?from={from_ms}&to={end_ms}&boot_id={boot_id}&max_points=512'
        export_path = f'/api/v1/history/export?from={from_ms}&to={end_ms}&boot_id={boot_id}'
        stop = threading.Event()
        lock = threading.Lock()
        measurements = {'history': [], 'export': [], 'status': []}
        errors = []
        heap_samples = []
        health_lines = []

        def serial_reader():
            while not stop.is_set():
                line = device.port.readline()
                if not line:
                    continue
                if b'HEALTH ' in line:
                    text = line.decode('utf-8', 'replace').strip()
                    fields = dict(part.split('=', 1) for part in text.split() if '=' in part)
                    try:
                        heap_samples.append({key: int(fields[key]) for key in
                                             ('uptime_ms', 'internal_free', 'internal_min',
                                              'internal_largest', 'psram_free', 'psram_min',
                                              'main_stack_min')})
                    except (KeyError, ValueError):
                        pass
                    health_lines.append(text)

        def worker(kind, path):
            while not stop.is_set():
                started = time.monotonic()
                try:
                    connection = http.client.HTTPConnection(host, 80, timeout=15)
                    connection.request('GET', path, headers={'Cookie': cookie, 'Host': host})
                    response = connection.getresponse()
                    body = response.read()
                    elapsed_ms = (time.monotonic() - started) * 1000
                    if response.status != 200:
                        raise RuntimeError(f'{kind} returned HTTP {response.status}')
                    if kind == 'history':
                        records = len(json.loads(body)['points'])
                    elif kind == 'export':
                        records = max(0, body.count(b'\n') - 1)
                    else:
                        status = json.loads(body)
                        records = len(status['sensors'])
                        storage_bytes = status['logging']['storage_used_bytes']
                    with lock:
                        measurement = {'latency_ms': round(elapsed_ms, 2),
                                       'bytes': len(body), 'records': records}
                        if kind == 'status':
                            measurement['storage_used_bytes'] = storage_bytes
                        measurements[kind].append(measurement)
                    connection.close()
                except Exception as error:
                    with lock:
                        errors.append(f'{kind}: {type(error).__name__}')
                    time.sleep(.25)

        reader = threading.Thread(target=serial_reader, daemon=True)
        workers = [threading.Thread(target=worker, args=('history', history_path), daemon=True),
                   threading.Thread(target=worker, args=('export', export_path), daemon=True),
                   threading.Thread(target=worker, args=('status', '/api/v1/status'), daemon=True)]
        reader.start()
        for thread in workers:
            thread.start()
        time.sleep(args.seconds)
        stop.set()
        for thread in workers:
            thread.join(timeout=3)
        reader.join(timeout=3)
    summary = {}
    for kind, values in measurements.items():
        latencies = [item['latency_ms'] for item in values]
        sizes = [item['bytes'] for item in values]
        summary[kind] = {
            'requests': len(values),
            'latency_ms': {
                'p50': round(statistics.median(latencies), 2) if latencies else None,
                'p95': round(sorted(latencies)[int((len(latencies) - 1) * .95)], 2) if latencies else None,
                'max': round(max(latencies), 2) if latencies else None,
            },
            'response_bytes_max': max(sizes) if sizes else None,
            'records_max': max(item['records'] for item in values) if values else 0,
        }
        if kind == 'status':
            storage_values = [item['storage_used_bytes'] for item in values]
            summary[kind]['storage_used_bytes'] = {
                'min': min(storage_values) if storage_values else None,
                'max': max(storage_values) if storage_values else None,
            }
    report = {
        'simulation': True,
        'hardware_verified': False,
        'duration_seconds': args.seconds,
        'errors': errors,
        'traffic': summary,
        'history_query_window_ms': [from_ms, end_ms],
        'initial_storage_used_bytes': initial_storage_bytes,
        'observed_storage_growth_bytes': (
            max(0, summary['status']['storage_used_bytes']['max'] - initial_storage_bytes)
            if summary['status']['storage_used_bytes']['max'] is not None else None
        ),
        'health_samples': len(heap_samples),
        'memory_minima': {
            key: min(sample[key] for sample in heap_samples) if heap_samples else None
            for key in ('internal_free', 'internal_min', 'internal_largest', 'psram_free',
                        'psram_min', 'main_stack_min')
        },
        'health_uptime_ms': [heap_samples[0]['uptime_ms'], heap_samples[-1]['uptime_ms']]
        if heap_samples else [],
        'continued_health': len(heap_samples) >= args.seconds // 10,
    }
    destination = Path(args.output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + '\n')
    destination.chmod(0o600)
    print(json.dumps(report, indent=2))
    if (errors or not report['continued_health'] or
            not all(summary[k]['requests'] for k in summary) or
            not summary['history']['records_max'] or not summary['export']['records_max']):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
