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
  firmware: { version: '0.1.0', partition: 'ota_0', ota_state: 'valid' },
  logging: {
    healthy: false,
    interval_seconds: 300,
    storage_available: false,
    storage_total_bytes: 0,
    storage_used_bytes: 0,
  },
};
