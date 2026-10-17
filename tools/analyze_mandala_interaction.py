#!/usr/bin/env python3
"""Summarize --interact callback traces without treating start gaps as xruns."""
import argparse
import csv
import json
import statistics
from collections import defaultdict


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", help="prefix passed to the controller's --interact mode")
    parser.add_argument("--window-ms", type=float, default=250,
                        help="callback-overlap window following an OPEN/CLOSE request")
    args = parser.parse_args()
    if not 0 < args.window_ms <= 3000:
        parser.error("window must be greater than zero and at most 3000 ms")
    with open(args.prefix + "-callbacks.csv", newline="") as stream:
        callbacks = list(csv.DictReader(stream))
    with open(args.prefix + "-events.csv", newline="") as stream:
        events = list(csv.DictReader(stream))
    phases = defaultdict(list)
    for row in callbacks:
        phases[row["phase"]].append({key: float(value) for key, value in row.items()
                                     if key != "phase"})
    deadline = 512 / 48000 * 1e6
    result = {}
    for phase, data in phases.items():
        values = sorted(row["processing_us"] / 1000 for row in data)
        actions = [event for event in events if event["phase"] == phase]
        transitions = [event for event in actions if event["action"] in ("OPEN", "CLOSE")]

        def in_window(row, action):
            start = float(action["start_ms"])
            return (row["start_ms"] < start + args.window_ms
                    and row["start_ms"] + row["processing_us"] / 1000 > start)

        transition_stats = {}
        for kind in ("OPEN", "CLOSE"):
            chosen = [event for event in transitions if event["action"] == kind]
            window = [row for row in data if any(in_window(row, event) for event in chosen)]
            transition_stats[kind] = {
                "actions": len(chosen), "callbacks_in_window": len(window),
                "overruns_in_window": sum(row["processing_us"] >= deadline for row in window),
                "max_request_ms": max((float(event["duration_ms"]) for event in chosen), default=0),
            }
        outside = [row for row in data if not any(in_window(row, event) for event in transitions)]
        result[phase] = {
            "callbacks": len(values), "mean_ms": statistics.mean(values),
            "p99_ms": values[len(values) * 99 // 100], "max_ms": values[-1],
            "overruns": sum(row["processing_us"] >= deadline for row in data),
            "near": sum(deadline * .9 <= row["processing_us"] < deadline for row in data),
            "edits": sum(event["action"] in ("MIX", "TRANSITION") for event in actions),
            "window_ms": args.window_ms,
            "outside_transition_callbacks": len(outside),
            "outside_transition_overruns": sum(row["processing_us"] >= deadline for row in outside),
            "events": transition_stats,
        }
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
