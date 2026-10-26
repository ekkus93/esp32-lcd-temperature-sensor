import http from 'node:http';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';
import Ajv from 'ajv';
import schema from '../../contracts/snapshot.schema.json' with { type: 'json' };
const validate = new Ajv().compile(schema);
const binary = fileURLToPath(new URL('../../.build/host/monitor_sim', import.meta.url));
const count = process.argv[2] ?? '5';
let snapshot = JSON.parse(execFileSync(binary, [count], { encoding: 'utf8' }));
if (!validate(snapshot)) throw new Error(JSON.stringify(validate.errors));
const start = Date.now();
const server = http.createServer((req, res) => {
  res.setHeader('Content-Type', 'application/json');
  res.setHeader('Cache-Control', 'no-store');
  if (req.method !== 'GET') { res.writeHead(501); res.end('{"error":"Read-only host simulator"}'); return; }
  if (req.url === '/api/v1/auth/state') res.end(JSON.stringify({enrolled:true,csrf:'simulation'}));
  else if (req.url === '/api/v1/settings') res.end(JSON.stringify({schema_version:1,etag:'simulator-settings',revision:0,writable:false,units:'C',display_mode:'basic',overview_seconds:20,trends_seconds:10,lookback_minutes:60,timezone:'UTC',ntp_server:'pool.ntp.org'}));
  else if (req.url === '/api/v1/status') res.end(JSON.stringify(snapshot));
  else if (req.url === '/api/v1/sensors') res.end(JSON.stringify(snapshot.sensors.map(s=>({rom_id:s.rom_id,etag:`"simulator:${s.rom_id}:1"`,revision:1,name:s.name,short_name:'',enabled:s.enabled,disconnect_alert:true,order:s.order,calibration_c:0,low_c:null,high_c:null,hysteresis_c:.5,persistence_seconds:15,state:s.state}))));
  else if (req.url.startsWith('/api/v1/history?')) res.end(JSON.stringify({schema_version:1,simulated:true,truncated:false,time_basis:'uptime',points:[]}));
  else if (req.url === '/api/v1/alerts') res.end(JSON.stringify({active:[],events:[]}));
  else { res.writeHead(404); res.end('{"error":"Not found"}'); }
});
const sockets = new WebSocketServer({noServer: true, maxPayload: 8192});
server.on('upgrade', (req, socket, head) => {
  if (req.url !== '/api/v1/live' || sockets.clients.size >= 4) {socket.destroy(); return;}
  sockets.handleUpgrade(req, socket, head, ws => sockets.emit('connection', ws, req));
});
sockets.on('connection', ws => {ws.send(JSON.stringify(snapshot)); ws.on('error', () => ws.terminate());});
const interval = setInterval(() => {
  const uptime = Date.now() - start;
  snapshot = {...snapshot, sequence: snapshot.sequence+1, uptime_ms: uptime,
    sensors: snapshot.sensors.map(s => ({...s, last_seen_ms: uptime}))};
  for (const ws of sockets.clients) {
    if (ws.bufferedAmount > 65536) ws.terminate();
    else if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(snapshot));
  }
}, 5000);
server.listen(8080, '127.0.0.1', () => console.log(`SIMULATION: ${count} fake probes on http://127.0.0.1:8080`));
function stop() {clearInterval(interval); for(const ws of sockets.clients)ws.terminate(); sockets.close(); server.close();}
process.on('SIGINT', stop); process.on('SIGTERM', stop);
