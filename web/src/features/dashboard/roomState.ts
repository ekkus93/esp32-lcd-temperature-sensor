// The live readings panel's headline and detail sentence (spec §18.4): the room's state in words.
import type { ActiveAlert } from '../alertText';

export interface PanelProbe {
  rom: string;
  name: string;
  state: string; // the snapshot state: normal, high, low, disconnected, stale, invalid, ...
  celsius: number | null;
  lowC: number | null;
  highC: number | null;
}

export interface RoomState {
  headline: string;
  detail: string;
}

const words = ['no', 'one', 'two', 'three', 'four', 'five'];
const count = (n: number) => words[n] ?? String(n);

export function formatTemperature(celsius: number, unit: 'C' | 'F'): string {
  return `${(unit === 'F' ? celsius * 1.8 + 32 : celsius).toFixed(1)} °${unit}`;
}

function duration(ms: number): string {
  const minutes = Math.floor(ms / 60000);
  if (minutes < 120) return minutes === 1 ? '1 minute' : `${minutes} minutes`;
  return `${Math.floor(minutes / 60)} hours`;
}

const problem = (p: PanelProbe) => p.state !== 'normal';

function joined(items: string[]): string {
  if (items.length < 2) return items.join('');
  return `${items.slice(0, -1).join(', ')} and ${items.at(-1)}`;
}

// "is too warm" etc., for the sentence naming several probes.
function condition(p: PanelProbe): string {
  if (p.state === 'high') return `${p.name} is too warm`;
  if (p.state === 'low') return `${p.name} is too cold`;
  if (p.state === 'disconnected') return `${p.name} is disconnected`;
  return `${p.name} has no current reading`;
}

function others(total: number): string {
  const rest = total - 1;
  if (rest <= 0) return '';
  return rest === 1
    ? ' The other probe is in range.'
    : ` The other ${count(rest)} probes are in range.`;
}

export function roomState({
  probes,
  unit,
  stale,
  busFault,
  alerts,
  uptimeMs,
}: {
  probes: PanelProbe[];
  unit: 'C' | 'F';
  stale: boolean;
  busFault: boolean;
  alerts: ActiveAlert[] | null;
  uptimeMs: number;
}): RoomState {
  if (stale)
    return {
      headline: 'Readings are out of date',
      detail:
        'The live connection to the device was lost. The last readings are shown until it returns.',
    };
  if (busFault)
    return {
      headline: 'The sensor bus has a fault',
      detail:
        'Current readings are unavailable. Check the probe cables and the shared data line.',
    };
  if (!probes.length)
    return {
      headline: 'No probes detected',
      detail:
        'Monitoring supports up to five probes. No alarms are generated for empty positions.',
    };

  const troubled = probes.filter(problem);
  if (!troubled.length) {
    const readings = probes.filter((p) => p.celsius !== null);
    const headline =
      probes.length === 1
        ? 'The probe is in range'
        : `All ${count(probes.length)} probes are in range`;
    if (readings.length < 2)
      return { headline, detail: 'Every reading is within its limits.' };
    const low = readings.reduce((a, b) => (b.celsius! < a.celsius! ? b : a));
    const high = readings.reduce((a, b) => (b.celsius! > a.celsius! ? b : a));
    return {
      headline,
      detail: `Lowest ${formatTemperature(low.celsius!, unit)} at ${low.name}, highest ${formatTemperature(high.celsius!, unit)} at ${high.name}.`,
    };
  }

  if (troubled.length > 1)
    return {
      headline: `${count(troubled.length).replace(/^./, (c) => c.toUpperCase())} probes need attention`,
      detail: `${joined(troubled.map(condition))}.`,
    };

  const p = troubled[0];
  const alert = alerts?.find(
    (item) => 'rom_id' in item && item.rom_id === p.rom,
  );
  // How long, once it has lasted a minute; "for less than a minute" says nothing useful.
  const elapsed = alert ? Math.max(0, uptimeMs - alert.since_uptime_ms) : 0;
  const lasting = elapsed >= 60000 ? ` for ${duration(elapsed)}` : '';
  const rest = others(probes.length);
  if (p.state === 'high' || p.state === 'low') {
    const limit = p.state === 'high' ? p.highC : p.lowC;
    const reading =
      p.celsius === null ? '' : `${formatTemperature(p.celsius, unit)}, `;
    const passed =
      limit === null
        ? `${p.state === 'high' ? 'above' : 'below'} its limit`
        : `${p.state === 'high' ? 'above' : 'below'} its ${formatTemperature(limit, unit)} limit`;
    const sentence = `${reading}${passed}${lasting}.`;
    return {
      headline: `${p.name} is running ${p.state === 'high' ? 'warm' : 'cold'}`,
      detail: sentence.charAt(0).toUpperCase() + sentence.slice(1) + rest,
    };
  }
  if (p.state === 'disconnected')
    return {
      headline: `${p.name} is disconnected`,
      detail: `No reading${lasting}. Check its cable and connector.${rest}`,
    };
  return {
    headline: `${p.name} has no current reading`,
    detail: `Its last reading is too old or could not be read.${rest}`,
  };
}
