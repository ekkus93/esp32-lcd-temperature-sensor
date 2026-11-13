// The guided ice-bath calibration for one probe (spec §17.2). It holds the probe's alerts while the
// probe is in the bath (spec §17.3), measures the live readings until they are steady, offers the
// offset that makes the probe read 0 °C, and saves it with the usual sensor update.
import { useEffect, useRef, useState } from 'react';
import {
  Alert,
  Box,
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  LinearProgress,
  Stack,
  Typography,
} from '@mui/material';
import {
  getSensors,
  saveSensor,
  setCalibrationHold,
  type SensorConfiguration,
} from '../../api/sensors';
import { temperature } from '../../api/client';
import type { Snapshot } from '../../api/generated';
import {
  GIVE_UP_MS,
  STEADY_SAMPLES,
  iceOffset,
  isSteady,
  judgeOffset,
  rawReading,
  type OffsetVerdict,
} from './iceCalibration';
import { createHoldKeeper, type HoldKeeper } from './holdKeeper';

type Unit = 'C' | 'F';
const REVISION_POLL_MS = 15000;
const READY_STATES = ['normal', 'high', 'low'];

type Step =
  | 'starting'
  | 'instructions'
  | 'measuring'
  | 'stopped'
  | 'result'
  | 'saving'
  | 'saved'
  | 'unprotected' // the pause was lost; it must be re-established before measuring
  | 'error';

// A temperature difference in the display unit (a difference has no 32 °F offset).
const delta = (celsius: number, unit: Unit) =>
  unit === 'F' ? celsius * 1.8 : celsius;
const signed = (value: number) => `${value > 0 ? '+' : ''}${value.toFixed(1)}`;

export interface IceCalibrationProps {
  rom: string;
  name: string;
  unit: Unit;
  etag: string;
  calibrationC: number;
  lowC: number | null;
  highC: number | null;
  snapshot: Snapshot | null;
  onCalibrated: (updated: SensorConfiguration) => void;
  onEditLimits: () => void;
  onClose: () => void;
}

export default function IceCalibrationDialog(props: IceCalibrationProps) {
  const {
    rom,
    name,
    unit,
    etag,
    calibrationC,
    lowC,
    highC,
    snapshot,
    onCalibrated,
    onEditLimits,
    onClose,
  } = props;
  const [step, setStep] = useState<Step>('starting');
  const [message, setMessage] = useState('');
  const [samples, setSamples] = useState<number[]>([]);
  const [offset, setOffset] = useState<number | null>(null);
  const [verdict, setVerdict] = useState<OffsetVerdict>({ kind: 'ok' });
  const mounted = useRef(true);
  const keeper = useRef<HoldKeeper | null>(null);
  const readings = useRef<number[]>([]);
  const lastSeen = useRef<number | null>(null);
  const measuring = useRef(false);
  const giveUp = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const poll = useRef<ReturnType<typeof setInterval> | undefined>(undefined);

  const live = snapshot?.sensors.find((s) => s.rom_id === rom) ?? null;
  const readyToMeasure = live !== null && READY_STATES.includes(live.state);

  // Stops measuring: its give-up and revision-check timers. The alert pause keeps renewing.
  function clearMeasurement() {
    clearTimeout(giveUp.current);
    clearInterval(poll.current);
    measuring.current = false;
  }

  const reason = (cause: unknown) =>
    cause instanceof Error ? cause.message : 'The device did not answer.';

  // Asks the device to pause the probe's alerts; the step changes once it answers.
  async function establish() {
    const current = keeper.current;
    if (!current) return;
    try {
      if ((await current.start()) && mounted.current) setStep('instructions');
    } catch (cause) {
      if (mounted.current) {
        setStep('error');
        setMessage(
          `The device did not pause alerts for this probe: ${reason(cause)}`,
        );
      }
    }
  }

  // Pauses the alerts again after a loss or a refusal; measuring waits for it.
  function pauseAgain() {
    setStep('starting');
    setMessage('');
    void establish();
  }

  // Ends the flow: measuring stops and the pause is shortened to its five-minute tail, once.
  async function endFlow() {
    clearMeasurement();
    await keeper.current?.close();
  }

  // Opening the flow pauses the probe's alerts; they stay paused, renewed, while it is open.
  useEffect(() => {
    mounted.current = true;
    keeper.current = createHoldKeeper({
      send: (minutes) => setCalibrationHold(rom, minutes),
      onLost: (cause) => {
        if (!mounted.current) return;
        clearMeasurement();
        setStep('unprotected');
        setMessage(
          `Alerts for this probe are no longer paused: the device did not renew the pause (${reason(cause)}). Pause them again before measuring, or take the probe out of the bath.`,
        );
      },
    });
    // Started after this effect, as App.tsx does: its state changes come with the device's answer.
    void Promise.resolve().then(establish);
    return () => {
      mounted.current = false;
      clearMeasurement();
      // Leaving the page while the flow is open still ends with the tail.
      void keeper.current?.close();
    };
    // The flow belongs to one probe for as long as it is open.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [rom]);

  // Each new live reading while measuring is one sample, counted once.
  useEffect(() => {
    if (step !== 'measuring' || !measuring.current || !snapshot) return;
    if (snapshot.bus_fault)
      return stop(
        'The sensor bus faulted, so readings stopped. Check the wiring and start again.',
      );
    if (!live)
      return stop(
        'The probe is no longer reported. Check its cable and start again.',
      );
    if (!READY_STATES.includes(live.state))
      return stop(
        `The probe is ${live.state.replaceAll('_', ' ')}, so readings stopped. Start again once it reads.`,
      );
    if (live.temperature_c === null || live.last_seen_ms === null) return;
    if (live.last_seen_ms === lastSeen.current) return;
    lastSeen.current = live.last_seen_ms;
    readings.current = [
      ...readings.current,
      rawReading(live.temperature_c, calibrationC),
    ].slice(-STEADY_SAMPLES);
    setSamples(readings.current);
    if (isSteady(readings.current)) {
      const next = iceOffset(readings.current);
      clearMeasurement();
      setOffset(next);
      setVerdict(judgeOffset(next));
      setStep('result');
    }
    // `stop` and the timers are stable for this flow.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [snapshot, step]);

  function stop(why: string) {
    clearMeasurement();
    setMessage(why);
    setStep('stopped');
  }

  function startMeasuring() {
    readings.current = [];
    setSamples([]);
    setOffset(null);
    lastSeen.current = live?.last_seen_ms ?? null;
    measuring.current = true;
    setMessage('');
    setStep('measuring');
    giveUp.current = setTimeout(
      () =>
        stop(
          'No steady reading in 10 minutes. Stir the ice bath, make sure the probe is covered, and try again.',
        ),
      GIVE_UP_MS,
    );
    // The settings must not change under the measurement.
    poll.current = setInterval(async () => {
      try {
        const latest = (await getSensors()).find((s) => s.rom_id === rom);
        if (!latest) stop('The probe was removed from the list. Start again.');
        else if (latest.etag !== etag)
          stop('The probe’s settings changed elsewhere. Start again.');
      } catch {
        // A failed check is retried at the next interval.
      }
    }, REVISION_POLL_MS);
  }

  async function save() {
    if (offset === null) return;
    setStep('saving');
    setMessage('');
    try {
      const latest = (await getSensors()).find((s) => s.rom_id === rom);
      if (!latest || latest.etag !== etag) {
        stop(
          'The probe’s settings changed while you were calibrating. Nothing was saved; start again.',
        );
        return;
      }
      await saveSensor(rom, { calibration_c: offset }, etag);
      // Wait for the device to apply it, as the editor does.
      for (let attempt = 0; attempt < 12; attempt++) {
        await new Promise((resolve) => setTimeout(resolve, 500));
        if (!mounted.current) return;
        const updated = (await getSensors()).find((s) => s.rom_id === rom);
        if (updated && updated.etag !== etag) {
          onCalibrated(updated);
          await endFlow();
          if (mounted.current) setStep('saved');
          return;
        }
      }
      if (mounted.current) {
        setStep('result');
        setMessage(
          'The offset is queued and has not been confirmed yet. Refresh before trying again.',
        );
      }
    } catch (cause) {
      if (mounted.current) {
        setStep('result');
        setMessage(
          cause instanceof Error
            ? cause.message
            : 'The offset could not be saved.',
        );
      }
    }
  }

  async function close() {
    await endFlow();
    onClose();
  }

  const shown = (celsius: number) => temperature(celsius, unit);
  // The probe already reads the ice point with its saved offset: there is nothing to change.
  const unchanged = offset !== null && Math.abs(offset - calibrationC) < 1e-9;
  const limits =
    lowC === null && highC === null
      ? 'no limits set'
      : [
          lowC === null ? 'no low limit' : `low ${shown(lowC)}`,
          highC === null ? 'no high limit' : `high ${shown(highC)}`,
        ].join(', ');

  return (
    <Dialog
      open
      onClose={() => void close()}
      aria-labelledby="ice-calibration-title"
      fullWidth
      maxWidth="sm"
    >
      <DialogTitle id="ice-calibration-title">
        Calibrate {name} in ice
      </DialogTitle>
      <DialogContent>
        <Stack spacing={2} sx={{ pt: 1 }}>
          {step === 'starting' && (
            <Typography>Pausing alerts for this probe…</Typography>
          )}
          {step === 'instructions' && (
            <>
              <Typography>
                Make the ice bath: crushed ice (not cubes) in a tall container,
                topped up with cold water to just below the ice surface, then
                stirred.
              </Typography>
              <Typography>
                Put the probe’s metal tip at least 5 cm into the bath, in the
                middle, not touching the container. Stir while it measures, and
                add ice if it floats free.
              </Typography>
              <Alert severity="info">
                Alerts for this probe are paused while it is in the bath.
                Calibrate each probe on its own cable, as it is installed.
              </Alert>
            </>
          )}
          {step === 'measuring' && (
            <>
              <Typography>
                Waiting for a steady reading. The last minute of readings must
                stay within 0.1 °C.
              </Typography>
              <Typography variant="titleMedium">
                {live?.temperature_c === null ||
                live?.temperature_c === undefined
                  ? 'No reading yet'
                  : `Live reading ${shown(live.temperature_c)}`}
              </Typography>
              <Box>
                <LinearProgress
                  variant="determinate"
                  value={(samples.length / STEADY_SAMPLES) * 100}
                  aria-label="Readings collected"
                />
                <Typography variant="bodySmall" color="text.secondary">
                  {samples.length} of {STEADY_SAMPLES} readings
                </Typography>
              </Box>
            </>
          )}
          {step === 'stopped' && <Alert severity="warning">{message}</Alert>}
          {(step === 'error' || step === 'unprotected') && (
            <Alert severity="error" role="alert">
              {message}
            </Alert>
          )}
          {step === 'result' && offset !== null && (
            <>
              <Typography variant="titleMedium">
                {unchanged
                  ? 'The probe reads steady in the ice.'
                  : `The probe reads steady in the ice. Save the offset that makes it read ${temperature(0, unit)}.`}
              </Typography>
              <Typography>
                {`Offset: ${signed(delta(calibrationC, unit))} → ${signed(delta(offset, unit))} °${unit}. Every reading will change by ${signed(delta(offset - calibrationC, unit))} °${unit}.`}
              </Typography>
              {unchanged && (
                <Alert severity="success">
                  The probe already reads {temperature(0, unit)} in the ice with
                  its saved offset. Nothing needs saving.
                </Alert>
              )}
              {!unchanged && verdict.kind === 'warn' && (
                <Alert severity="warning">{verdict.reason}</Alert>
              )}
              {verdict.kind === 'refuse' && (
                <Alert severity="error">{verdict.reason}</Alert>
              )}
              <Alert severity="info">
                Your high and low limits are not changed ({limits}). If you set
                them to allow for this probe reading high or low, adjust them
                after saving.{' '}
                <Button
                  size="small"
                  onClick={() => {
                    void endFlow().then(() => {
                      onEditLimits();
                      onClose();
                    });
                  }}
                >
                  Edit limits
                </Button>
              </Alert>
            </>
          )}
          {step === 'saving' && (
            <Typography role="status">Saving the offset…</Typography>
          )}
          {step === 'saved' && (
            <Alert severity="success" role="status">
              Saved. Put the probe back in the bath: it should read{' '}
              {temperature(0, unit)} within a few minutes. Dry it when you are
              done.
            </Alert>
          )}
          {message && step === 'result' && (
            <Alert severity="warning">{message}</Alert>
          )}
        </Stack>
      </DialogContent>
      <DialogActions>
        {step === 'instructions' && (
          <>
            <Button onClick={() => void close()}>Cancel</Button>
            <Button
              variant="contained"
              onClick={startMeasuring}
              disabled={!readyToMeasure}
            >
              {readyToMeasure ? 'Start measuring' : 'Waiting for a reading'}
            </Button>
          </>
        )}
        {step === 'measuring' && (
          <Button onClick={() => void close()}>Cancel</Button>
        )}
        {step === 'stopped' && (
          <>
            <Button onClick={() => void close()}>Cancel</Button>
            <Button variant="contained" onClick={() => setStep('instructions')}>
              Start again
            </Button>
          </>
        )}
        {step === 'result' && (
          <>
            <Button onClick={() => void close()}>Cancel</Button>
            {unchanged ? (
              <Button variant="contained" onClick={() => void close()}>
                Done
              </Button>
            ) : (
              <Button
                variant="contained"
                onClick={() => void save()}
                disabled={verdict.kind === 'refuse'}
              >
                {verdict.kind === 'warn'
                  ? 'Save this offset anyway'
                  : 'Save offset'}
              </Button>
            )}
          </>
        )}
        {step === 'saved' && (
          <Button variant="contained" onClick={onClose}>
            Done
          </Button>
        )}
        {step === 'starting' && (
          <Button onClick={() => void close()}>Close</Button>
        )}
        {(step === 'error' || step === 'unprotected') && (
          <>
            <Button onClick={() => void close()}>Close</Button>
            <Button variant="contained" onClick={pauseAgain}>
              Pause alerts again
            </Button>
          </>
        )}
      </DialogActions>
    </Dialog>
  );
}
