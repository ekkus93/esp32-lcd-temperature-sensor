import { useEffect, useRef, useState } from 'react';
import {
  Alert,
  Button,
  Card,
  CardContent,
  FormControlLabel,
  Stack,
  Switch,
  TextField,
  Typography,
} from '@mui/material';
import {
  getSensorStorageStatus,
  getSensors,
  retireSensor,
  saveSensor,
  type SensorConfiguration,
} from '../api/sensors';
const numericFields = [
  'calibration_c',
  'hysteresis_c',
  'persistence_seconds',
  'low_c',
  'high_c',
] as const;
function numericText(sensor: SensorConfiguration) {
  return Object.fromEntries(
    numericFields.map((key) => [key, String(sensor[key] ?? '')]),
  ) as Record<(typeof numericFields)[number], string>;
}
function SensorEditor({
  sensor,
  writable,
  onUnavailable,
  onRetired,
}: {
  sensor: SensorConfiguration;
  writable: boolean;
  onUnavailable: () => void;
  onRetired: (id: string) => void;
}) {
  const [numbers, setNumbers] = useState(() => numericText(sensor));
  const mounted = useRef(true);
  useEffect(() => {
    mounted.current = true;
    return () => {
      mounted.current = false;
    };
  }, []);
  const [draft, setDraft] = useState(sensor);
  const [message, setMessage] = useState('');
  const [busy, setBusy] = useState(false);
  async function submit(event: React.FormEvent) {
    event.preventDefault();
    setMessage('');
    setBusy(true);
    const parsed = { ...draft };
    for (const key of numericFields) {
      const value = numbers[key];
      if (value === '' && (key === 'low_c' || key === 'high_c'))
        parsed[key] = null;
      else if (value.trim() === '' || !Number.isFinite(Number(value))) {
        setMessage('Enter valid numeric settings.');
        setBusy(false);
        return;
      } else parsed[key] = Number(value);
    }
    if (
      parsed.low_c !== null &&
      parsed.high_c !== null &&
      parsed.low_c >= parsed.high_c
    ) {
      setMessage('Low threshold must be below high threshold.');
      setBusy(false);
      return;
    }
    try {
      const {
        rom_id: id,
        state: _state,
        etag,
        revision: _revision,
        ...patch
      } = parsed;
      void _state;
      void _revision;
      await saveSensor(id, patch, etag);
      setMessage('Update queued. It applies with the next measurement cycle.');
      // Keep the next edit tied to the applied revision, not the stale form.
      let resolved = false;
      for (let attempt = 0; attempt < 12; attempt++) {
        await new Promise((resolve) => setTimeout(resolve, 500));
        if (!mounted.current) return;
        const updated = (await getSensors()).find((item) => item.rom_id === id);
        if (updated && updated.etag !== etag) {
          const storage = await getSensorStorageStatus();
          if (!storage.writable) {
            onUnavailable();
            setMessage(
              'Settings were not committed. The previous stored configuration was restored.',
            );
          } else {
            setDraft(updated);
            setNumbers(numericText(updated));
            setMessage('Settings committed and applied.');
          }
          resolved = true;
          break;
        }
      }
      if (mounted.current && !resolved) {
        const storage = await getSensorStorageStatus();
        if (!storage.writable) {
          onUnavailable();
          setMessage(
            'Settings were not committed. The previous stored configuration was restored.',
          );
        } else
          setMessage('Update is still pending. Refresh before trying again.');
      }
    } catch (e) {
      setMessage(e instanceof Error ? e.message : 'Unable to update sensor');
    } finally {
      setBusy(false);
    }
  }
  async function retire() {
    const { rom_id: id, etag } = sensor;
    if (
      !window.confirm(
        `Retire disconnected probe ${id}? Its configuration mapping will be removed after saving. Existing history remains under this ROM, and a returning probe starts with default settings.`,
      )
    )
      return;
    setBusy(true);
    setMessage(
      'Retirement queued. Waiting for the saved mapping to be removed.',
    );
    try {
      await retireSensor(id, etag);
      for (let attempt = 0; attempt < 12; attempt++) {
        await new Promise((resolve) => setTimeout(resolve, 500));
        if (!mounted.current) return;
        const updated = await getSensors();
        if (!updated.some((item) => item.rom_id === id)) {
          const storage = await getSensorStorageStatus();
          if (!storage.writable) {
            onUnavailable();
            setMessage(
              'Probe retirement was not committed; stored data is preserved.',
            );
          } else onRetired(id);
          return;
        }
      }
      if (mounted.current)
        setMessage(
          'Retirement is still pending or the probe reconnected. Refresh before trying again.',
        );
    } catch (e) {
      if (mounted.current)
        setMessage(e instanceof Error ? e.message : 'Unable to retire probe');
    } finally {
      setBusy(false);
    }
  }
  const numeric = (key: (typeof numericFields)[number], value: string) =>
    setNumbers({ ...numbers, [key]: value });
  return (
    <Card>
      <CardContent>
        <Typography variant="h6">{sensor.name}</Typography>
        <Typography variant="caption" component="p">
          ROM {sensor.rom_id} · {sensor.state.replaceAll('_', ' ')}
        </Typography>
        <form onSubmit={submit}>
          <fieldset
            disabled={!writable}
            style={{ border: 0, margin: 0, padding: 0, minWidth: 0 }}
          >
            <Stack spacing={2} sx={{ mt: 2 }}>
              {message && (
                <Alert severity="info" role="status">
                  {message}
                </Alert>
              )}
              <TextField
                label="Sensor name"
                value={draft.name}
                onChange={(e) => setDraft({ ...draft, name: e.target.value })}
                required
                slotProps={{ htmlInput: { maxLength: 32 } }}
              />
              <TextField
                label="Short LCD label"
                value={draft.short_name}
                onChange={(e) =>
                  setDraft({ ...draft, short_name: e.target.value })
                }
                slotProps={{ htmlInput: { maxLength: 12 } }}
              />
              <FormControlLabel
                label="Monitoring enabled"
                control={
                  <Switch
                    checked={draft.enabled}
                    onChange={(_, enabled) => setDraft({ ...draft, enabled })}
                  />
                }
              />
              <TextField
                label="Display position"
                type="number"
                value={draft.order + 1}
                onChange={(e) =>
                  setDraft({ ...draft, order: Number(e.target.value) - 1 })
                }
                slotProps={{ htmlInput: { min: 1, max: 5, step: 1 } }}
              />
              <TextField
                label="Calibration offset (°C)"
                type="number"
                value={numbers.calibration_c}
                onChange={(e) => numeric('calibration_c', e.target.value)}
                slotProps={{ htmlInput: { min: -20, max: 20, step: 0.01 } }}
              />
              <TextField
                label="Low threshold (°C)"
                type="number"
                value={numbers.low_c}
                onChange={(e) => numeric('low_c', e.target.value)}
                helperText="Leave empty to disable this threshold"
                slotProps={{ htmlInput: { min: -55, max: 125, step: 0.1 } }}
              />
              <TextField
                label="High threshold (°C)"
                type="number"
                value={numbers.high_c}
                onChange={(e) => numeric('high_c', e.target.value)}
                helperText="Leave empty to disable this threshold"
                slotProps={{ htmlInput: { min: -55, max: 125, step: 0.1 } }}
              />
              <TextField
                label="Hysteresis (°C)"
                type="number"
                value={numbers.hysteresis_c}
                onChange={(e) => numeric('hysteresis_c', e.target.value)}
                slotProps={{ htmlInput: { min: 0, max: 20, step: 0.1 } }}
              />
              <TextField
                label="Alert persistence (seconds)"
                type="number"
                value={numbers.persistence_seconds}
                onChange={(e) => numeric('persistence_seconds', e.target.value)}
                slotProps={{ htmlInput: { min: 0, max: 3600, step: 1 } }}
              />
              <FormControlLabel
                label="Alert when this probe disconnects"
                control={
                  <Switch
                    checked={draft.disconnect_alert}
                    onChange={(_, disconnect_alert) =>
                      setDraft({ ...draft, disconnect_alert })
                    }
                  />
                }
              />
              <Button
                variant="contained"
                type="submit"
                disabled={busy || !writable}
              >
                Save sensor settings
              </Button>
              {sensor.state === 'disconnected' && (
                <Button
                  color="warning"
                  onClick={() => void retire()}
                  disabled={busy || !writable}
                >
                  Retire disconnected probe
                </Button>
              )}
            </Stack>
          </fieldset>
        </form>
      </CardContent>
    </Card>
  );
}
export default function SensorsView() {
  const [sensors, setSensors] = useState<SensorConfiguration[] | null>(null);
  const [writable, setWritable] = useState<boolean | null>(null);
  const [error, setError] = useState('');
  const [message, setMessage] = useState('');
  const [refresh, setRefresh] = useState(0);
  useEffect(() => {
    let active = true;
    Promise.all([getSensors(), getSensorStorageStatus()])
      .then(([next, storage]) => {
        if (active) {
          setSensors(next);
          setWritable(storage.writable);
        }
      })
      .catch((e) => {
        if (active)
          setError(e instanceof Error ? e.message : 'Sensors unavailable');
      });
    return () => {
      active = false;
    };
  }, [refresh]);
  return (
    <Stack spacing={2}>
      <Alert severity="info">
        Identify a probe by warming it and watching its live value change. Its
        label follows its immutable ROM address.
      </Alert>
      {error && (
        <Alert severity="error" role="alert">
          {error}
        </Alert>
      )}
      {message && (
        <Alert severity="success" role="status">
          {message}
        </Alert>
      )}
      {writable === false && (
        <Alert severity="error" role="alert">
          Sensor settings are read-only because stored configuration could not
          be safely opened or saved. Existing data is preserved for recovery.
        </Alert>
      )}
      <Button
        onClick={() => {
          setError('');
          setMessage('');
          setRefresh(refresh + 1);
        }}
      >
        Refresh sensor configuration
      </Button>
      {sensors === null && !error && <Typography>Loading sensors…</Typography>}
      {sensors?.length === 0 && (
        <Typography>
          No sensors discovered. Unused positions do not cause alarms.
        </Typography>
      )}
      {sensors?.map((sensor) => (
        <SensorEditor
          key={`${sensor.rom_id}-${refresh}`}
          sensor={sensor}
          writable={writable === true}
          onUnavailable={() => setWritable(false)}
          onRetired={(id) => {
            setSensors(
              (current) =>
                current?.filter((item) => item.rom_id !== id) ?? null,
            );
            setMessage(
              `Probe ${id} retired. Its history remains under the original ROM.`,
            );
          }}
        />
      ))}
    </Stack>
  );
}
