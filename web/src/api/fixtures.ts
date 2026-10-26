export const empty = {
  version: 1,
  boot_id: 'simulator',
  sequence: 0,
  uptime_ms: 0,
  simulated: true,
  sensors: [],
  bus_fault: false,
  clock: { utc: null, synchronized: false },
  wifi: { state: 'offline' },
  logging: { healthy: false, interval_seconds: 300 },
};
