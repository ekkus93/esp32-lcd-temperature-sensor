import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { setTimeout as delay } from "node:timers/promises";
import Ajv from "../web/node_modules/ajv/dist/ajv.js";
import schema from "../contracts/snapshot.schema.json" with { type: "json" };
import WebSocket from "../web/node_modules/ws/wrapper.mjs";

const port = 30000 + Math.floor(Math.random() * 20000);
const address = `ws://127.0.0.1:${port}/api/v1/live`;
const child = spawn(process.execPath, ["web/scripts/simulator.mjs", "5"], {
  cwd: new URL("..", import.meta.url),
  env: {
    ...process.env,
    MONITOR_SIMULATOR_PORT: String(port),
    MONITOR_SIMULATOR_INTERVAL_MS: "20",
    MONITOR_SIMULATOR_MAX_BUFFERED_BYTES: "4096",
    MONITOR_SIMULATOR_TEST_BACKPRESSURE: "1",
  },
  stdio: ["ignore", "pipe", "pipe"],
});
let output = "";
child.stdout.on("data", (data) => (output += data));
child.stderr.on("data", (data) => (output += data));
const sockets = new Set();
// The simulator sends its first snapshot immediately upon WebSocket connection.
// Register the message handler before "open": otherwise the first frame can
// arrive between connect() resolving and nextMessage() subscribing.
const pendingMessages = new WeakMap();
function connect(target = address) {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(target);
    sockets.add(socket);
    const state = { buffer: [], waiters: [] };
    pendingMessages.set(socket, state);
    socket.on("message", (data, binary) => {
      const frame = { data, binary };
      const waiter = state.waiters.shift();
      if (waiter) waiter(frame);
      else state.buffer.push(frame);
    });
    const timeout = setTimeout(
      () => reject(new Error("WebSocket connect timed out")),
      5000,
    );
    socket.once("open", () => {
      clearTimeout(timeout);
      resolve(socket);
    });
    socket.once("error", (error) => {
      clearTimeout(timeout);
      reject(error);
    });
  });
}
function nextMessage(socket, timeoutMs = 6500) {
  const state = pendingMessages.get(socket);
  assert.ok(state, "nextMessage requires a socket created by connect");
  if (state.buffer.length) return Promise.resolve(state.buffer.shift());
  return new Promise((resolve, reject) => {
    const deliver = (frame) => {
      clearTimeout(timeout);
      resolve(frame);
    };
    const timeout = setTimeout(() => {
      state.waiters.splice(state.waiters.indexOf(deliver), 1);
      reject(new Error("WebSocket snapshot timed out"));
    }, timeoutMs);
    state.waiters.push(deliver);
  });
}
function closed(socket, timeoutMs = 3000) {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(
      () => reject(new Error("WebSocket stayed open after command")),
      timeoutMs,
    );
    socket.once("close", (code) => {
      clearTimeout(timeout);
      resolve(code);
    });
  });
}
try {
  const readyDeadline = Date.now() + 10000;
  while (
    !output.includes("SIMULATION:") &&
    Date.now() < readyDeadline &&
    child.exitCode === null
  )
    await delay(25);
  assert.equal(child.exitCode, null, `simulator exited early: ${output}`);
  assert.match(output, /SIMULATION: 5 fake probes/);
  const authState = await fetch(`http://127.0.0.1:${port}/api/v1/auth/state`);
  assert.equal(authState.status, 200);
  const auth = await authState.json();
  assert.equal(auth.enrolled, true);
  assert.match(auth.csrf, /^[a-z0-9-]{32,64}$/);
  const statusResponse = await fetch(`http://127.0.0.1:${port}/api/v1/status`);
  assert.equal(statusResponse.status, 200);
  assert.equal((await statusResponse.json()).sensors.length, 5);
  const socket = await connect();
  const first = await nextMessage(socket);
  assert.equal(first.binary, false);
  const parse = new Ajv().compile(schema);
  const initial = JSON.parse(first.data.toString());
  assert.equal(parse(initial), true, JSON.stringify(parse.errors));
  assert.equal(initial.sensors.length, 5);
  assert.ok(initial.boot_id && Number.isInteger(initial.sequence));

  const heartbeat = await nextMessage(socket);
  const update = JSON.parse(heartbeat.data.toString());
  assert.equal(parse(update), true, JSON.stringify(parse.errors));
  assert.equal(update.boot_id, initial.boot_id);
  assert.ok(update.sequence > initial.sequence);

  const commandSocket = await connect();
  const commandInitial = await nextMessage(commandSocket);
  assert.equal(parse(JSON.parse(commandInitial.data.toString())), true);
  const didClose = closed(commandSocket);
  commandSocket.send("{invalid json");
  assert.equal(await didClose, 1008);
  const peers = [];
  for (let index = 0; index < 3; index++) {
    const peer = await connect();
    const welcome = await nextMessage(peer);
    assert.equal(parse(JSON.parse(welcome.data.toString())), true);
    peers.push(peer);
  }
  const overflow = new WebSocket(address);
  sockets.add(overflow);
  const overflowResult = await new Promise((resolve) => {
    const timeout = setTimeout(() => resolve("timeout"), 3000);
    const finish = (result) => {
      clearTimeout(timeout);
      resolve(result);
    };
    overflow.once("open", () => finish("open"));
    overflow.once("error", () => finish("error"));
    overflow.once("close", (code) => finish(`close:${code}`));
  });
  assert.ok(
    overflowResult === "error" || overflowResult.startsWith("close:"),
    `fifth concurrent client was not rejected: ${overflowResult}`,
  );
  await Promise.all(
    peers.map((peer) => {
      const didDisconnect = closed(peer);
      peer.close();
      return didDisconnect;
    }),
  );
  const mainClosed = closed(socket);
  socket.close();
  await mainClosed;
  const slowClient = await connect(`${address}?stall_send=1`);
  const slowInitial = await nextMessage(slowClient);
  assert.equal(parse(JSON.parse(slowInitial.data.toString())), true);
  assert.equal(
    await closed(slowClient, 3000),
    1006,
    "server did not terminate the client whose outbound buffer exceeded its bound",
  );
  for (let attempt = 0; attempt < 30; attempt++) {
    const reconnect = await connect();
    const welcome = await nextMessage(reconnect);
    assert.equal(parse(JSON.parse(welcome.data.toString())), true);
    const didDisconnect = closed(reconnect);
    reconnect.close();
    await didDisconnect;
  }
  console.log(
    "PASS: simulator live snapshots, four-client limit, slow-client cutoff, read-only rejection, and 30 reconnects",
  );
} catch (error) {
  console.error(error instanceof Error ? error.message : error);
  console.error(output);
  process.exitCode = 1;
} finally {
  for (const socket of sockets) socket.terminate();
  child.kill("SIGTERM");
  await Promise.race([
    new Promise((resolve) => child.once("exit", resolve)),
    delay(1000),
  ]);
}
