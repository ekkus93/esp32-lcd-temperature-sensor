import type { WifiScan } from './api-wifi-scan.generated';
import type { WifiStatus } from './api-wifi-status.generated';
import { request } from './auth';
import { wifiScanValid, wifiStatusValid } from './schema-validators.generated';
export type NetworkProgress = WifiStatus;
export type ScannedNetwork = WifiScan['networks'][number];
const validateStatus = wifiStatusValid;
const validateScan = wifiScanValid;
export async function networkProgress(): Promise<NetworkProgress> {
  const value = await request<unknown>('/wifi/status');
  if (!validateStatus(value))
    throw new Error('Invalid network progress response');
  return value;
}
export async function wifiScan(): Promise<WifiScan> {
  const value = await request<unknown>('/wifi/scan');
  if (!validateScan(value)) throw new Error('Invalid network scan response');
  return value;
}
