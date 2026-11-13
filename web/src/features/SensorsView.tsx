import { useEffect, useRef, useState } from 'react';
import { useConfirm } from '../useConfirm';
import {
  Alert,
  Button,
  Card,
  CardContent,
  FormControlLabel,
  Stack,
  Switch,
  Tab,
  Tabs,
  TextField,
  ToggleButton,
  ToggleButtonGroup,
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
type NumericKey = (typeof numericFields)[number];
type Unit = 'C' | 'F';
// Limits and thresholds are absolute temperatures; calibration and hysteresis are differences,
// which scale by 1.8 but do not take the 32 degree offset. The device always stores Celsius.
const absolute = (key: NumericKey) => key === 'low_c' || key === 'high_c';
const temperatureField = (key: NumericKey) => key !== 'persistence_seconds';
function toDisplay(key: NumericKey, celsius: number, unit: Unit) {
  if (unit === 'C' || !temperatureField(key)) return celsius;
  return Number((celsius * 1.8 + (absolute(key) ? 32 : 0)).toFixed(2));
}
// Rounded to 0.0001 °C, far below the probe's 0.0625 °C step, so 22.86 °F is exactly the 12.7 °C
// calibration limit rather than 12.700000000000001, which the device would reject.
function toCelsius(key: NumericKey, shown: number, unit: Unit) {
  if (unit === 'C' || !temperatureField(key)) return shown;
  return Number(((shown - (absolute(key) ? 32 : 0)) / 1.8).toFixed(4));
}
// Re-expresses the form's text in another unit. Untouched fields are shown from the stored Celsius
// value; edited ones convert what was typed, to 0.01, which converts back to the same 0.01 °C.
// Text that is not a number is kept as typed, so its error still shows on save.
function convertText(
  numbers: Record<NumericKey, string>,
  edited: Set<NumericKey>,
  stored: SensorConfiguration,
  from: Unit,
  to: Unit,
) {
  const fresh = numericText(stored, to);
  return Object.fromEntries(
    numericFields.map((key) => {
      const text = numbers[key];
      if (!edited.has(key)) return [key, fresh[key]];
      const value = Number(text);
      if (text.trim() === '' || !Number.isFinite(value)) return [key, text];
      const celsius = toCelsius(key, value, from);
      return [key, String(Number(toDisplay(key, celsius, to).toFixed(2)))];
    }),
  ) as Record<NumericKey, string>;
}
function numericText(sensor: SensorConfiguration, unit: Unit) {
  return Object.fromEntries(
    numericFields.map((key) => {
      const value = sensor[key];
      return [key, value === null ? '' : String(toDisplay(key, value, unit))];
    }),
  ) as Record<NumericKey, string>;
}
// What the probe's own memory holds, shown under the calibration offset. The probe keeps a copy
// of the offset to the nearest 0.1 °C, so it stays with the probe on another monitor.
function probeCalibrationText(state: SensorConfiguration['probe_calibration']) {
  switch (state) {
    case 'stored':
      return 'Also saved on the probe itself, so it stays with the probe.';
    case 'restored':
      return "Read from the probe's own memory when it was first connected.";
    case 'pending':
      return 'Saving a copy on the probe…';
    case 'failed':
      return 'Could not be saved on the probe. It may not support this.';
    default:
      return 'Not read from the probe yet.';
  }
}
function SensorEditor({
  sensor,
  unit,
  writable,
  onUnavailable,
  onSaved,
  onRetired,
}: {
  sensor: SensorConfiguration;
  unit: Unit;
  writable: boolean;
  onUnavailable: () => void;
  onSaved: (updated: SensorConfiguration) => void;
  onRetired: (id: string) => void;
}) {
  const [numbers, setNumbers] = useState(() => numericText(sensor, unit));
  // The unit the form's text is in. When the page's entry unit changes, the text is converted
  // here, during render, so no frame shows numbers under the wrong unit.
  const [shownUnit, setShownUnit] = useState(unit);
  // A save finishing after a unit switch must format its result in the unit shown by then.
  const currentUnit = useRef(unit);
  useEffect(() => {
    currentUnit.current = unit;
  }, [unit]);
  // Only fields the user edited are converted back; the rest are sent exactly as stored.
  const [edited, setEdited] = useState<Set<NumericKey>>(new Set());
  const mounted = useRef(true);
  const [confirm, dialog] = useConfirm();
  useEffect(() => {
    mounted.current = true;
    return () => {
      mounted.current = false;
    };
  }, []);
  const [draft, setDraft] = useState(sensor);
  // Kept as text so an empty field is an error, not position 0 (order -1).
  const [positionText, setPositionText] = useState(String(sensor.order + 1));
  const [positionError, setPositionError] = useState('');
  const [message, setMessage] = useState('');
  const [busy, setBusy] = useState(false);
  if (shownUnit !== unit) {
    setShownUnit(unit);
    setNumbers(convertText(numbers, edited, draft, shownUnit, unit));
  }
  async function submit(event: React.FormEvent) {
    event.preventDefault();
    setMessage('');
    setBusy(true);
    const parsed = { ...draft };
    const position = Number(positionText);
    if (
      positionText.trim() === '' ||
      !Number.isInteger(position) ||
      position < 1 ||
      position > 5
    ) {
      setPositionError('Enter a whole number from 1 to 5.');
      setBusy(false);
      return;
    }
    setPositionError('');
    parsed.order = position - 1;
    for (const key of numericFields) {
      const value = numbers[key];
      if (value === '' && (key === 'low_c' || key === 'high_c'))
        parsed[key] = null;
      else if (value.trim() === '' || !Number.isFinite(Number(value))) {
        setMessage('Enter valid numeric settings.');
        setBusy(false);
        return;
      } else if (edited.has(key))
        parsed[key] = toCelsius(key, Number(value), unit);
      // Untouched fields keep the draft's value: the last applied revision, sent unconverted.
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
        probe_calibration: _probe,
        ...patch
      } = parsed;
      void _state;
      void _revision;
      void _probe;
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
            setNumbers(numericText(updated, currentUnit.current));
            setEdited(new Set());
            setPositionText(String(updated.order + 1));
            onSaved(updated);
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
      if (mounted.current)
        setMessage(e instanceof Error ? e.message : 'Unable to update sensor');
    } finally {
      // The editor may have closed while the device was answering.
      if (mounted.current) setBusy(false);
    }
  }
  async function retire() {
    const { rom_id: id, etag } = draft;
    if (
      !(await confirm({
        title: `Retire probe ${id}?`,
        message:
          'Its configuration mapping will be removed after saving. Existing history remains under this ROM, and a returning probe starts with default settings, apart from a calibration offset saved on the probe itself.',
        confirmLabel: 'Retire probe',
        destructive: true,
      }))
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
      if (mounted.current) setBusy(false);
    }
  }
  const numeric = (key: NumericKey, value: string) => {
    setNumbers({ ...numbers, [key]: value });
    setEdited(new Set(edited).add(key));
  };
  const u = `°${unit}`;
  const limit = (key: NumericKey, celsius: number) =>
    toDisplay(key, celsius, unit);
  return (
    <Card>
      <CardContent>
        {dialog}
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
                value={positionText}
                onChange={(e) => setPositionText(e.target.value)}
                error={positionError !== ''}
                helperText={positionError}
                slotProps={{ htmlInput: { min: 1, max: 5, step: 1 } }}
              />
              <TextField
                label={`Calibration offset (${u})`}
                type="number"
                value={numbers.calibration_c}
                onChange={(e) => numeric('calibration_c', e.target.value)}
                helperText={probeCalibrationText(sensor.probe_calibration)}
                slotProps={{
                  htmlInput: {
                    min: limit('calibration_c', -12.7),
                    max: limit('calibration_c', 12.7),
                    // Any precision: a converted value must not trip the browser's step check.
                    step: 'any',
                  },
                }}
              />
              <TextField
                label={`Low threshold (${u})`}
                type="number"
                value={numbers.low_c}
                onChange={(e) => numeric('low_c', e.target.value)}
                helperText="Leave empty to disable this threshold"
                slotProps={{
                  htmlInput: {
                    min: limit('low_c', -55),
                    max: limit('low_c', 125),
                    step: 'any',
                  },
                }}
              />
              <TextField
                label={`High threshold (${u})`}
                type="number"
                value={numbers.high_c}
                onChange={(e) => numeric('high_c', e.target.value)}
                helperText="Leave empty to disable this threshold"
                slotProps={{
                  htmlInput: {
                    min: limit('low_c', -55),
                    max: limit('low_c', 125),
                    step: 'any',
                  },
                }}
              />
              <TextField
                label={`Hysteresis (${u})`}
                type="number"
                value={numbers.hysteresis_c}
                onChange={(e) => numeric('hysteresis_c', e.target.value)}
                slotProps={{
                  htmlInput: {
                    min: 0,
                    max: limit('hysteresis_c', 20),
                    step: 'any',
                  },
                }}
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
// Tabs follow the LCD display position, then the ROM, so they match the screen's order.
function byPosition(a: SensorConfiguration, b: SensorConfiguration) {
  return a.order - b.order || a.rom_id.localeCompare(b.rom_id);
}
export default function SensorsView({ unit }: { unit: Unit }) {
  const [sensors, setSensors] = useState<SensorConfiguration[] | null>(null);
  const [writable, setWritable] = useState<boolean | null>(null);
  const [error, setError] = useState('');
  const [message, setMessage] = useState('');
  const [refresh, setRefresh] = useState(0);
  // The chosen probe's ROM; when it is missing (first load, retired) the first tab is shown.
  const [selected, setSelected] = useState('');
  // The unit temperatures are typed in here. It starts as the monitor's display unit (until the
  // owner picks one) and changes nothing else; the device always stores Celsius.
  const [chosenUnit, setChosenUnit] = useState<Unit | null>(null);
  const entryUnit = chosenUnit ?? unit;
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
  const ordered = sensors ? [...sensors].sort(byPosition) : [];
  const current = ordered.some((item) => item.rom_id === selected)
    ? selected
    : (ordered[0]?.rom_id ?? '');
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
      {current && (
        <Stack
          direction="row"
          // Gaps, not margins, so a wrapped line starts flush on a phone.
          useFlexGap
          spacing={1.5}
          sx={{ alignItems: 'center', flexWrap: 'wrap', rowGap: 1 }}
        >
          <Typography id="sensor-entry-unit">Enter temperatures in</Typography>
          <ToggleButtonGroup
            exclusive
            size="small"
            value={entryUnit}
            onChange={(_, next: Unit | null) => next && setChosenUnit(next)}
            aria-labelledby="sensor-entry-unit"
          >
            <ToggleButton value="C" aria-label="Celsius" sx={{ px: 2 }}>
              °C
            </ToggleButton>
            <ToggleButton value="F" aria-label="Fahrenheit" sx={{ px: 2 }}>
              °F
            </ToggleButton>
          </ToggleButtonGroup>
          <Typography variant="body2" color="text.secondary">
            Always saved in °C.
          </Typography>
        </Stack>
      )}
      {current && (
        <Tabs
          value={current}
          onChange={(_, rom: string) => setSelected(rom)}
          // Up to five names of up to 32 characters: scroll rather than squeeze on a phone.
          variant="scrollable"
          scrollButtons="auto"
          allowScrollButtonsMobile
          aria-label="Sensors"
        >
          {ordered.map((sensor) => (
            <Tab
              key={sensor.rom_id}
              value={sensor.rom_id}
              label={sensor.name}
              id={`sensor-tab-${sensor.rom_id}`}
              aria-controls={`sensor-panel-${sensor.rom_id}`}
              sx={{
                textTransform: 'none',
                fontSize: { xs: 14, sm: 15 },
                minWidth: 0,
                maxWidth: 220,
                px: 1.5,
              }}
            />
          ))}
        </Tabs>
      )}
      {/* Every editor stays mounted so unsaved edits and a save in progress survive a tab
          switch; only the chosen one is shown. */}
      {ordered.map((sensor) => (
        <div
          key={`${sensor.rom_id}-${refresh}`}
          role="tabpanel"
          id={`sensor-panel-${sensor.rom_id}`}
          aria-labelledby={`sensor-tab-${sensor.rom_id}`}
          hidden={sensor.rom_id !== current}
        >
          <SensorEditor
            sensor={sensor}
            unit={entryUnit}
            writable={writable === true}
            onUnavailable={() => setWritable(false)}
            onSaved={(updated) =>
              setSensors(
                (list) =>
                  list?.map((item) =>
                    item.rom_id === updated.rom_id ? updated : item,
                  ) ?? null,
              )
            }
            onRetired={(id) => {
              setSensors(
                (list) => list?.filter((item) => item.rom_id !== id) ?? null,
              );
              setMessage(
                `Probe ${id} retired. Its history remains under the original ROM.`,
              );
            }}
          />
        </div>
      ))}
    </Stack>
  );
}
