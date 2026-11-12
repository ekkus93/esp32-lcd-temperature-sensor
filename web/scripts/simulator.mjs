import http from 'node:http';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';
import Ajv from 'ajv';
import schema from '../../contracts/snapshot.schema.json' with { type: 'json' };
const validate = new Ajv().compile(schema);
const binary = fileURLToPath(
  new URL('../../.build/host/monitor_sim', import.meta.url),
);
const count = process.argv[2] ?? '5';
const csvHeader = execFileSync(binary, ['--csv-header'], { encoding: 'utf8' });
let snapshot = JSON.parse(execFileSync(binary, [count], { encoding: 'utf8' }));
if (!validate(snapshot)) throw new Error(JSON.stringify(validate.errors));
// Two hours of five-minute buckets per probe with a slow drift, so charts have something to draw.
function simulatedHistory() {
  const points = [];
  const bucketMs = 300000;
  snapshot.sensors.forEach((sensor, index) => {
    for (let bucket = 0; bucket < 24; bucket++) {
      const average = 4 + index + 0.5 * Math.sin((bucket + index) / 3);
      points.push({
        boot_id: snapshot.boot_id,
        rom_id: sensor.rom_id,
        start_ms: bucket * bucketMs,
        end_ms: (bucket + 1) * bucketMs,
        utc_seconds: null,
        average_c: Number(average.toFixed(3)),
        minimum_c: Number((average - 0.3).toFixed(3)),
        maximum_c: Number((average + 0.3).toFixed(3)),
        count: 60,
        partial: false,
      });
    }
  });
  return {
    schema_version: 1,
    simulated: true,
    truncated: false,
    time_basis: 'uptime',
    points,
    gaps: [],
  };
}
function simulatedAlerts() {
  const first = snapshot.sensors[0];
  return {
    active: [],
    events: first
      ? [
          {
            sequence: 1,
            boot_id: snapshot.boot_id,
            kind: 'sensor_state',
            rom_id: first.rom_id,
            previous: 'normal',
            state: 'high',
            uptime_ms: 600000,
            utc_seconds: null,
            detail: 0,
          },
          {
            sequence: 2,
            boot_id: snapshot.boot_id,
            kind: 'sensor_state',
            rom_id: first.rom_id,
            previous: 'high',
            state: 'normal',
            uptime_ms: 900000,
            utc_seconds: null,
            detail: 0,
          },
        ]
      : [],
    next_after: first ? 2 : 0,
    has_more: false,
    oldest_available: first ? 1 : null,
    event_log_healthy: true,
  };
}
const start = Date.now();
const server = http.createServer((req, res) => {
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Cache-Control', 'no-store');
  // One request per connection: a client that reuses an idle keep-alive socket just as the server
  // times it out gets "socket hang up", which made the live test flaky on a busy machine.
  res.setHeader('Connection', 'close');
  if (req.method !== 'GET') {
    res.writeHead(501);
    res.end('{"error":"Read-only host simulator"}');
    return;
  }
  if (req.url === '/api/v1/auth/state')
    res.end(
      JSON.stringify({
        enrolled: true,
        // The read-only simulator behaves as a signed-in owner so every screen can be viewed.
        signed_in: true,
        csrf: 'simulator-csrf-token-000000000000',
      }),
    );
  else if (req.url === '/api/v1/settings')
    res.end(
      JSON.stringify({
        schema_version: 1,
        etag: 'simulator-settings',
        revision: 0,
        writable: false,
        units: 'F',
        display_mode: 'basic',
        overview_seconds: 20,
        trends_seconds: 10,
        lookback_minutes: 60,
        timezone: 'America/Los_Angeles',
        ntp_server: 'pool.ntp.org',
        hostname: 'temperature-monitor',
      }),
    );
  else if (req.url === '/api/v1/status') res.end(JSON.stringify(snapshot));
  else if (req.url === '/api/v1/sensors')
    res.end(
      JSON.stringify(
        snapshot.sensors.map((s) => ({
          rom_id: s.rom_id,
          etag: `"simulator:${s.rom_id}:1"`,
          revision: 1,
          name: s.name,
          short_name: '',
          enabled: s.enabled,
          disconnect_alert: true,
          order: s.order,
          calibration_c: 0,
          low_c: null,
          high_c: null,
          hysteresis_c: 0.5,
          persistence_seconds: 15,
          state: s.state,
          // Simulated probes have no memory of their own.
          probe_calibration: 'unknown',
        })),
      ),
    );
  else if (req.url.startsWith('/api/v1/history/export?')) {
    res.setHeader('Content-Type', 'text/csv; charset=utf-8');
    res.setHeader(
      'Content-Disposition',
      'attachment; filename=temperature-simulation-history.csv',
    );
    res.end(csvHeader);
  } else if (req.url.startsWith('/api/v1/history?'))
    res.end(JSON.stringify(simulatedHistory()));
  else if (req.url.startsWith('/api/v1/alerts?'))
    res.end(JSON.stringify(simulatedAlerts()));
  else if (req.url === '/api/v1/sensors/storage')
    res.end(
      JSON.stringify({
        schema_version: 1,
        state: 'read_only',
        writable: false,
      }),
    );
  else if (req.url === '/api/v1/probe-scan')
    res.end(
      JSON.stringify({
        schema_version: 1,
        // As on the device: simulation builds do not scan a physical bus.
        enabled: false,
        complete: false,
        discovered_count: 0,
        overflow: false,
      }),
    );
  else if (req.url === '/api/v1/clock')
    res.end(
      JSON.stringify({
        schema_version: 1,
        synchronized: false,
        last_sync_utc_seconds: null,
        sync_age_ms: null,
      }),
    );
  else {
    res.writeHead(404);
    res.end('{"error":"Not found"}');
  }
});
const sockets = new WebSocketServer({ noServer: true, maxPayload: 1024 });
const intervalMs = Number(process.env.MONITOR_SIMULATOR_INTERVAL_MS ?? 5000);
const maxBufferedBytes = Number(
  process.env.MONITOR_SIMULATOR_MAX_BUFFERED_BYTES ?? 65536,
);
const testBackpressure =
  process.env.MONITOR_SIMULATOR_TEST_BACKPRESSURE === '1';
if (!Number.isInteger(intervalMs) || intervalMs < 1 || intervalMs > 60000)
  throw new Error('MONITOR_SIMULATOR_INTERVAL_MS must be between 1 and 60000');
if (!Number.isInteger(maxBufferedBytes) || maxBufferedBytes < 1)
  throw new Error(
    'MONITOR_SIMULATOR_MAX_BUFFERED_BYTES must be a positive integer',
  );
server.on('upgrade', (req, socket, head) => {
  const url = new URL(req.url, 'http://127.0.0.1');
  if (url.pathname !== '/api/v1/live' || sockets.clients.size >= 4) {
    socket.destroy();
    return;
  }
  sockets.handleUpgrade(req, socket, head, (ws) =>
    sockets.emit('connection', ws, req),
  );
});
sockets.on('connection', (ws, req) => {
  const url = new URL(req.url, 'http://127.0.0.1');
  const stallSend =
    testBackpressure && url.searchParams.get('stall_send') === '1';
  ws.send(JSON.stringify(snapshot), () => {
    if (stallSend) ws._socket.cork();
  });
  ws.on('message', (data, binary) => {
    ws.close(
      binary || data.length > 1024 ? 1009 : 1008,
      'Read-only live channel',
    );
  });
  ws.on('error', () => ws.terminate());
});
const interval = setInterval(() => {
  const uptime = Date.now() - start;
  snapshot = {
    ...snapshot,
    sequence: snapshot.sequence + 1,
    uptime_ms: uptime,
    sensors: snapshot.sensors.map((s) => ({ ...s, last_seen_ms: uptime })),
  };
  const frame = JSON.stringify(snapshot);
  for (const ws of sockets.clients) {
    if (ws.bufferedAmount > maxBufferedBytes) ws.terminate();
    else if (ws.readyState === ws.OPEN) ws.send(frame);
  }
}, intervalMs);
const port = Number(process.env.MONITOR_SIMULATOR_PORT ?? 8080);
server.listen(port, '127.0.0.1', () =>
  console.log(`SIMULATION: ${count} fake probes on http://127.0.0.1:${port}`),
);
function stop() {
  clearInterval(interval);
  for (const ws of sockets.clients) ws.terminate();
  sockets.close();
  server.close();
}
process.on('SIGINT', stop);
process.on('SIGTERM', stop);
