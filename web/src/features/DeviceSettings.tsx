import { useEffect, useState } from 'react';
import {
  Alert,
  Button,
  Card,
  CardContent,
  MenuItem,
  Stack,
  TextField,
  Typography,
} from '@mui/material';
import {
  getSettings,
  saveSettings,
  timezones,
  type DeviceSettings as Settings,
} from '../api/settings';
const durationFields = [
  'overview_seconds',
  'trends_seconds',
  'lookback_minutes',
] as const;
function Editor({
  settings,
  onApplied,
}: {
  settings: Settings;
  onApplied: (unit: 'C' | 'F') => void;
}) {
  const [draft, setDraft] = useState(settings);
  const [numbers, setNumbers] = useState(() =>
    Object.fromEntries(
      durationFields.map((key) => [key, String(settings[key])]),
    ),
  );
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState('');
  async function submit(event: React.FormEvent) {
    event.preventDefault();
    setMessage('');
    const values = durationFields.map((key) => Number(numbers[key]));
    if (values.some((value) => !Number.isInteger(value))) {
      setMessage('Enter whole-number durations.');
      return;
    }
    const {
      etag,
      schema_version: _schema,
      revision: _revision,
      writable: _writable,
      ...patch
    } = draft;
    void _schema;
    void _revision;
    void _writable;
    durationFields.forEach((key, index) => {
      patch[key] = values[index];
    });
    setBusy(true);
    try {
      const next = await saveSettings(patch, etag);
      setDraft(next);
      onApplied(next.units);
      setMessage('Device preferences saved.');
    } catch (e) {
      setMessage(e instanceof Error ? e.message : 'Settings update failed');
    } finally {
      setBusy(false);
    }
  }
  return (
    <form onSubmit={submit}>
      <Stack spacing={2}>
        {!draft.writable && (
          <Alert severity="warning">
            Device preferences are read-only. Existing settings are preserved.
          </Alert>
        )}
        {message && (
          <Alert severity="info" role="status">
            {message}
          </Alert>
        )}
        <TextField
          label="Temperature units"
          select
          value={draft.units}
          onChange={(e) =>
            setDraft({ ...draft, units: e.target.value as 'C' | 'F' })
          }
        >
          <MenuItem value="C">Celsius (°C)</MenuItem>
          <MenuItem value="F">Fahrenheit (°F)</MenuItem>
        </TextField>
        <TextField
          label="LCD display mode"
          select
          value={draft.display_mode}
          onChange={(e) =>
            setDraft({
              ...draft,
              display_mode: e.target.value as 'basic' | 'extended',
            })
          }
        >
          <MenuItem value="basic">Basic — Overview</MenuItem>
          <MenuItem value="extended">Extended — Overview and Trends</MenuItem>
        </TextField>
        {durationFields.map((key, index) => (
          <TextField
            key={key}
            type="number"
            required
            label={
              [
                'Overview duration (seconds)',
                'Trends duration (seconds)',
                'LCD history lookback (minutes)',
              ][index]
            }
            value={numbers[key]}
            onChange={(e) => setNumbers({ ...numbers, [key]: e.target.value })}
            slotProps={{
              htmlInput: { min: 5, max: index === 2 ? 1440 : 300, step: 1 },
            }}
          />
        ))}
        <TextField
          label="Device timezone"
          select
          value={draft.timezone}
          onChange={(e) => setDraft({ ...draft, timezone: e.target.value })}
        >
          {timezones.map((zone) => (
            <MenuItem key={zone} value={zone}>
              {zone.replaceAll('_', ' ')}
            </MenuItem>
          ))}
        </TextField>
        <TextField
          label="NTP server"
          required
          value={draft.ntp_server}
          onChange={(e) => setDraft({ ...draft, ntp_server: e.target.value })}
          helperText="Hostname or IP address; UTC stays unknown until synchronization succeeds."
          slotProps={{ htmlInput: { maxLength: 253 } }}
        />
        <Button
          type="submit"
          variant="contained"
          disabled={busy || !draft.writable}
        >
          Save device preferences
        </Button>
      </Stack>
    </form>
  );
}
export default function DeviceSettings({
  onApplied,
}: {
  onApplied: (unit: 'C' | 'F') => void;
}) {
  const [settings, setSettings] = useState<Settings | null>(null);
  const [error, setError] = useState('');
  const [refresh, setRefresh] = useState(0);
  useEffect(() => {
    let active = true;
    getSettings()
      .then((next) => {
        if (active) {
          setSettings(next);
          onApplied(next.units);
        }
      })
      .catch((e: unknown) => {
        if (active)
          setError(
            e instanceof Error ? e.message : 'Device settings unavailable',
          );
      });
    return () => {
      active = false;
    };
  }, [onApplied, refresh]);
  return (
    <Card>
      <CardContent>
        <Typography variant="h6" sx={{ mb: 2 }}>
          Device preferences
        </Typography>
        <Typography sx={{ mb: 2 }}>
          The LCD is read-only. Choose its display mode, timing and temperature
          units here.
        </Typography>
        {error && <Alert severity="error">{error}</Alert>}
        <Button
          onClick={() => {
            setError('');
            setRefresh(refresh + 1);
          }}
        >
          Refresh device preferences
        </Button>
        {settings && (
          <Editor
            key={`${settings.etag}:${refresh}`}
            settings={settings}
            onApplied={onApplied}
          />
        )}
        {!settings && !error && (
          <Typography>Loading device preferences…</Typography>
        )}
      </CardContent>
    </Card>
  );
}
