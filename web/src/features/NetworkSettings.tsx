import { useEffect, useRef, useState } from 'react';
import {
  Alert,
  Button,
  Card,
  CardContent,
  Chip,
  Stack,
  TextField,
  Typography,
} from '@mui/material';
import { request, setCsrf } from '../api/auth';
import { networkProgress, wifiScan, type ScannedNetwork } from '../api/network';
export default function NetworkSettings() {
  const mounted = useRef(true);
  useEffect(() => {
    mounted.current = true;
    return () => {
      mounted.current = false;
    };
  }, []);
  const [ssid, setSsid] = useState('');
  const [password, setPassword] = useState('');
  const [message, setMessage] = useState('');
  const [busy, setBusy] = useState(false);
  const [scanning, setScanning] = useState(false);
  const [networks, setNetworks] = useState<ScannedNetwork[]>([]);
  async function scan() {
    setScanning(true);
    setMessage('Scanning for nearby 2.4 GHz networks…');
    try {
      await request('/wifi/scan', 'POST');
      for (let attempt = 0; attempt < 12; attempt++) {
        await new Promise((resolve) => setTimeout(resolve, 500));
        if (!mounted.current) return;
        const result = await wifiScan();
        if (!result.scanning) {
          setNetworks(result.networks);
          setMessage(
            result.networks.length
              ? 'Select a network or enter its SSID manually.'
              : 'No visible networks found. You can enter a hidden SSID manually.',
          );
          return;
        }
      }
      throw new Error('Scan is still busy. Try again shortly.');
    } catch (e) {
      setMessage(e instanceof Error ? e.message : 'Network scan failed');
    } finally {
      setScanning(false);
    }
  }
  async function connect(event: React.FormEvent) {
    event.preventDefault();
    setBusy(true);
    setMessage('');
    try {
      await request('/wifi/connect', 'POST', { ssid, password });
      setPassword('');
      setMessage(
        'Testing candidate Wi-Fi. Existing credentials remain until connection succeeds.',
      );
      for (let attempt = 0; attempt < 60; attempt++) {
        await new Promise((resolve) => setTimeout(resolve, 500));
        if (!mounted.current) return;
        const progress = await networkProgress();
        if (progress.attempt === 'succeeded') {
          setMessage(
            `Connected and saved. LAN address: http://${progress.ip}. Setup stays available briefly during handoff.`,
          );
          return;
        }
        if (
          progress.attempt === 'failed' ||
          progress.attempt === 'storage_error'
        ) {
          setMessage(
            `${progress.error}. Previous credentials are preserved; you can retry.`,
          );
          return;
        }
        setMessage(
          `Testing network: ${progress.remaining_seconds} seconds remaining. Setup remains available.`,
        );
      }
      setMessage(
        'Connection outcome is delayed. Check status before retrying.',
      );
    } catch (e) {
      setMessage(e instanceof Error ? e.message : 'Connection request failed');
    } finally {
      setBusy(false);
    }
  }
  async function status() {
    try {
      const next = await networkProgress();
      setMessage(
        `Network: ${next.state}. LAN address: ${next.ip}. ${next.error}`,
      );
    } catch (e) {
      setMessage(
        e instanceof Error ? e.message : 'Unable to read network state',
      );
    }
  }
  async function logout() {
    try {
      await request('/auth/logout', 'POST');
      setCsrf('');
      window.dispatchEvent(new Event('auth-required'));
    } catch (e) {
      setMessage(e instanceof Error ? e.message : 'Sign out failed');
    }
  }
  return (
    <Card>
      <CardContent>
        <Typography variant="h6">Wi-Fi network</Typography>
        <Typography sx={{ mb: 2 }}>
          Changes are validated before replacing saved credentials. AP setup
          remains available during the attempt.
        </Typography>
        {message && (
          <Alert severity="info" role="status" sx={{ mb: 2 }}>
            {message}
          </Alert>
        )}
        <form onSubmit={connect}>
          <Stack spacing={2}>
            <Button onClick={() => void scan()} disabled={scanning || busy}>
              Scan for networks
            </Button>
            <Stack direction="row" sx={{ gap: 1, flexWrap: 'wrap' }}>
              {networks.map((network) => (
                <Chip
                  key={network.ssid}
                  label={`${network.ssid} · ${network.rssi} dBm · ${network.secure ? 'secured' : 'open'}`}
                  onClick={() => setSsid(network.ssid)}
                />
              ))}
            </Stack>
            <TextField
              label="2.4 GHz network SSID"
              required
              value={ssid}
              onChange={(e) => setSsid(e.target.value)}
              slotProps={{ htmlInput: { maxLength: 32 } }}
            />
            <TextField
              label="Wi-Fi password"
              type="password"
              autoComplete="off"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              slotProps={{ htmlInput: { maxLength: 64 } }}
              helperText="Leave empty for an open network"
            />
            <Button type="submit" variant="contained" disabled={busy}>
              Test and connect
            </Button>
            <Button onClick={() => void status()}>
              Check connection status
            </Button>
            <Button onClick={() => void logout()}>
              Sign out administrator
            </Button>
          </Stack>
        </form>
      </CardContent>
    </Card>
  );
}
