#!/usr/bin/env python3
"""Non-destructive physical-sensor log collector and numeric-only report.

Live serial mode sets DTR/RTS false before opening. Neither mode prints or
stores raw device logs, Wi-Fi details, credentials, ROM IDs or settings.
"""
import argparse
import json
import re
import sys
import time
from pathlib import Path

KEYS = ("cycles", "last_probes", "max_probes", "valid", "invalid",
        "fault_cycles", "last_read_ms", "max_read_ms")
PAIR = re.compile(r"([a-z_]+)=(\d+)")


def parse_line(line):
    if "SENSOR_DIAG " not in line:
        return None
    numeric = dict((name, int(value)) for name, value in
                   PAIR.findall(line.split("SENSOR_DIAG ", 1)[1]))
    return {name: numeric[name] for name in KEYS} if all(
        name in numeric for name in KEYS) else None


def report(samples, expected_probes=None, min_records=2,
           max_invalid=None, max_faults=None, max_read_ms=None,
           min_valid_per_cycle=0):
    errors = []
    if len(samples) < min_records:
        errors.append("too few diagnostic records")
    for before, after in zip(samples, samples[1:]):
        for key in ("cycles", "valid", "invalid", "fault_cycles", "max_probes",
                    "max_read_ms"):
            if after[key] < before[key]:
                errors.append("counter reset or regression: " + key)
        cycle_delta = after["cycles"] - before["cycles"]
        reading_delta = (after["valid"] + after["invalid"] -
                         before["valid"] - before["invalid"])
        if cycle_delta >= 0 and reading_delta > 5 * cycle_delta:
            errors.append("readings exceed five-probe acquisition capacity")
        # max_probes is monotonic and counts actual returned readings.
        # No interval can contain more readings than cycles * that maximum,
        # even when the installed bus has fewer than five probes.
        if cycle_delta >= 0 and reading_delta > after["max_probes"] * cycle_delta:
            errors.append("readings exceed observed probe acquisition capacity")
        if cycle_delta == 0 and reading_delta != 0:
            errors.append("readings changed without acquisition cycle")
    for item in samples:
        if item["last_probes"] > item["max_probes"]:
            errors.append("probe count exceeds cumulative maximum")
        if item["last_read_ms"] > item["max_read_ms"]:
            errors.append("read latency exceeds cumulative maximum")
        if item["fault_cycles"] > item["cycles"]:
            errors.append("fault cycles exceed acquisition cycles")
        if item["max_probes"] > 5:
            errors.append("probe count exceeds firmware capacity")
        if item["valid"] + item["invalid"] > 5 * item["cycles"]:
            errors.append("cumulative readings exceed acquisition capacity")
        if item["valid"] + item["invalid"] > item["max_probes"] * item["cycles"]:
            errors.append("cumulative readings exceed observed probe capacity")
    if len(samples) >= 2 and samples[-1]["cycles"] <= samples[0]["cycles"]:
        errors.append("no new acquisition cycles observed")
    if expected_probes is not None and any(
            item["last_probes"] != expected_probes for item in samples):
        errors.append("observed probe count differs from expected")
    invalid_delta = samples[-1]["invalid"] - samples[0]["invalid"] if samples else 0
    valid_delta = samples[-1]["valid"] - samples[0]["valid"] if samples else 0
    faults_delta = samples[-1]["fault_cycles"] - samples[0]["fault_cycles"] if samples else 0
    cycles_delta = samples[-1]["cycles"] - samples[0]["cycles"] if samples else 0
    if expected_probes is not None and expected_probes > 0 and cycles_delta > 0:
        if valid_delta <= 0:
            errors.append("no valid readings observed")
    if cycles_delta > 0 and valid_delta < min_valid_per_cycle * cycles_delta:
        errors.append("valid readings per acquisition cycle below minimum")
    if max_invalid is not None and invalid_delta > max_invalid:
        errors.append("invalid readings exceeded limit")
    if max_faults is not None and faults_delta > max_faults:
        errors.append("fault cycles exceeded limit")
    # The firmware counter includes spikes that occurred between log snapshots.
    observed_max_ms = max((item["max_read_ms"] for item in samples), default=0)
    if max_read_ms is not None and observed_max_ms > max_read_ms:
        errors.append("sampling latency exceeded limit")
    return {
        "passed": not errors,
        "records": len(samples),
        "observed_cycles": samples[-1]["cycles"] - samples[0]["cycles"] if samples else 0,
        "valid_delta": valid_delta,
        "min_valid_per_cycle": min_valid_per_cycle,
        "invalid_delta": invalid_delta,
        "fault_cycles_delta": faults_delta,
        "last_probe_count": samples[-1]["last_probes"] if samples else None,
        "max_probe_count": max((item["max_probes"] for item in samples), default=0),
        "max_observed_read_ms": observed_max_ms,
        "errors": list(dict.fromkeys(errors)),
        "hardware_verified": False,
    }


def collect_serial(path, seconds):
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("pyserial required for --port") from exc
    port = serial.Serial(port=None, baudrate=115200, timeout=0.5)
    port.dtr = False
    port.rts = False
    port.port = path
    records = []
    with port:
        finish = time.monotonic() + seconds
        while time.monotonic() < finish:
            # Only parse numeric diagnostic lines; never capture or print raw logs.
            item = parse_line(port.readline(512).decode("utf-8", errors="replace"))
            if item is not None:
                records.append(item)
    return records


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="serial port (read-only, DTR/RTS disabled)")
    source.add_argument("--log", type=Path, help="analyze a previously captured log")
    parser.add_argument("--seconds", type=float, default=180)
    parser.add_argument("--expected-probes", type=int, choices=range(0, 6))
    parser.add_argument("--min-records", type=int, default=2)
    parser.add_argument("--max-invalid", type=int)
    parser.add_argument("--max-faults", type=int)
    parser.add_argument("--max-read-ms", type=int)
    parser.add_argument("--min-valid-per-cycle", type=int, default=0,
                        choices=range(0, 6),
                        help="minimum average valid readings per acquisition (0-5)")
    parser.add_argument("--output", type=Path, help="optional numeric-only JSON report")
    args = parser.parse_args(argv)
    if args.seconds <= 0 or args.min_records < 1 or any(
            x is not None and x < 0 for x in
            (args.max_invalid, args.max_faults, args.max_read_ms)):
        parser.error("time/count limits must be positive or nonnegative")
    try:
        if args.port:
            samples = collect_serial(args.port, args.seconds)
        else:
            with args.log.open(encoding="utf-8", errors="replace") as handle:
                samples = [item for line in handle if (item := parse_line(line)) is not None]
    except (OSError, RuntimeError) as exc:
        print("Unable to collect sensor diagnostics: " + type(exc).__name__, file=sys.stderr)
        return 2
    if (args.expected_probes is not None and
            args.min_valid_per_cycle > args.expected_probes):
        parser.error("minimum valid readings cannot exceed expected probes")
    result = report(samples, args.expected_probes, args.min_records,
                    args.max_invalid, args.max_faults, args.max_read_ms,
                    args.min_valid_per_cycle)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, sort_keys=True))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
