// The trend chart's line shape: smooth through the readings, without looping or overshooting.

// Tangents for a monotone cubic through the points (Steffen 1990): zero at a peak or trough, and
// never steeper than the curve can take without overshooting its neighbouring readings.
function tangents(run: { x: number; y: number }[]): number[] {
  const h = run.slice(1).map((p, i) => p.x - run[i].x);
  const s = h.map((dx, i) => (dx > 0 ? (run[i + 1].y - run[i].y) / dx : 0));
  return run.map((_, i) => {
    if (i === 0) return s[0];
    if (i === run.length - 1) return s[i - 1];
    const [s0, s1, h0, h1] = [s[i - 1], s[i], h[i - 1], h[i]];
    if (s0 * s1 <= 0 || h0 + h1 <= 0) return 0;
    const p = (s0 * h1 + s1 * h0) / (h0 + h1);
    return (
      Math.sign(s0) * Math.min(Math.abs(s0), Math.abs(s1), Math.abs(p) / 2) * 2
    );
  });
}

// A smooth path through the points, one subpath per run. A monotone cubic, not Catmull-Rom: with
// five-minute buckets beside multi-day holes, Catmull-Rom's control points reached past the next
// reading, so lines looped back in time. Here each segment's control points lie within the
// segment's own time span and readings.
export function path(runs: { x: number; y: number }[][]): string {
  const xy = (x: number, y: number) => `${x.toFixed(1)},${y.toFixed(1)}`;
  return runs
    .filter((run) => run.length > 0)
    .map((run) => {
      if (run.length === 1) return `M${xy(run[0].x, run[0].y)}h0.01`;
      const m = tangents(run);
      let d = `M${xy(run[0].x, run[0].y)}`;
      for (let i = 0; i < run.length - 1; i++) {
        const [p1, p2] = [run[i], run[i + 1]];
        const third = (p2.x - p1.x) / 3;
        d += ` C${xy(p1.x + third, p1.y + m[i] * third)} ${xy(p2.x - third, p2.y - m[i + 1] * third)} ${xy(p2.x, p2.y)}`;
      }
      return d;
    })
    .join(' ');
}
