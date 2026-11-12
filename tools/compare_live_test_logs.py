#!/usr/bin/env python3
"""Summarize one or compare two TapeSister live diagnostic session exports.

Uses counter differences inside the run, never the session's retained maxima.
Action order/targets are checked before timing comparisons are described as matched.
"""
import argparse
import json
import re
import sys
from pathlib import Path


def fields(row):
    return dict(re.findall(r"(\w+)=(.*?)(?=\s+\w+=|$)", row.get("detail", "")))


def phase_usage(run, windows):
    """Use complete windows after settling; never mix transition boundaries."""
    phases = [r for r in run if r.get("type") == "live_test_phase"]
    groups = {}
    for i, row in enumerate(phases):
        state = fields(row)
        begin = row["t_ms"] + float(state.get("settle_ms", 0))
        end = phases[i + 1]["t_ms"] if i + 1 < len(phases) else run[-1]["t_ms"]
        key = (state["mode"], state["fx_canvas"])
        groups.setdefault(key, []).extend(w for w in windows if w["from_ms"] >= begin and w["t_ms"] <= end)
    result = []
    for (mode, canvas), samples in sorted(groups.items()):
        callbacks = sum(w.get("callbacks", 0) for w in samples)
        result.append({"mode": mode, "fx_canvas": canvas == "1", "windows": len(samples),
                       "callbacks": callbacks,
                       "average_callback_ms": sum(w.get("average_ms", 0) * w.get("callbacks", 0) for w in samples) / callbacks if callbacks else None,
                       "peak_callback_ms": max((w["peak_ms"] for w in samples), default=None),
                       "over_budget": sum(w.get("over_budget", 0) for w in samples)})
    return result


def summarize(path):
    rows = [json.loads(line) for line in Path(path).read_text(encoding="utf-8").splitlines() if line.strip()]
    starts = [i for i, row in enumerate(rows) if row.get("type") == "live_test_start"]
    if not starts:
        raise ValueError(f"{path}: no live_test_start (test did not start)")
    run = rows[starts[-1]:]
    endings = [i for i, row in enumerate(run) if row.get("type") in ("live_test_complete", "live_test_abort")]
    if not endings:
        raise ValueError(f"{path}: incomplete export; no completion or abort marker")
    run = run[:endings[0] + 1]
    start, end = fields(run[0]), fields(run[-1])
    begins = [fields(r) for r in run if r.get("type") == "live_test_action_begin"]
    ends = [fields(r) for r in run if r.get("type") == "live_test_action_end"]
    counters = [fields(r) for r in run if r.get("type") == "live_test_checkpoint"]
    planned = int(start["steps"])
    valid = (run[-1]["type"] == "live_test_complete"
             and len(begins) == len(ends) == planned
             and [int(x["index"]) for x in begins] == list(range(planned))
             and [int(x["index"]) for x in ends] == list(range(planned))
             and all(x["success"] == "1" for x in ends)
             and len(counters) >= 2)
    if len(counters) < 2:
        raise ValueError(f"{path}: insufficient in-run counter checkpoints")
    callbacks = int(counters[-1]["callbacks"]) - int(counters[0]["callbacks"])
    overruns = int(counters[-1]["over_budget"]) - int(counters[0]["over_budget"])
    if callbacks <= 0 or overruns < 0 or any(x["generation"] != counters[0]["generation"] for x in counters):
        valid = False
    # Only whole windows wholly inside the replay; boundary windows mix load/startup.
    windows = [r for r in run if r.get("type") == "audio_window"
               and r.get("from_ms", -1) >= run[0]["t_ms"] and r["t_ms"] <= run[-1]["t_ms"]]
    observed_peak = max((r["peak_ms"] for r in windows), default=None)
    device_rows = [r for r in rows if r.get("type") == "audio_device"
                   and fields(r).get("generation") == counters[0]["generation"]
                   and r["t_ms"] <= run[-1]["t_ms"]]
    device = fields(device_rows[-1]) if device_rows else {}
    header = next((r.get("detail", "") for r in rows if r.get("type") == "session_start"), "")
    host = dict(re.findall(r"(platform|CPUs|RAM_MB)=([^;]+)", header))
    signature = [tuple(x[k] for k in ("index", "action", "target", "value", "planned_ms")) for x in begins]
    return {
        "file": str(path), "complete": valid, "reason": end.get("reason", "unknown"),
        "scenario": start["scenario"], "project_hash": start["project_hash"],
        "configuration": {k: start.get(k) for k in ("backend", "rate", "frames", "channels", "workers", "profile")},
        "output_device": device.get("output"), "host_summary": host,
        "build_revision": start.get("revision", "unknown"),
        "completed_actions": len(ends), "planned_actions": planned,
        "callbacks": callbacks, "over_budget": overruns,
        "over_budget_percent": 100 * overruns / callbacks if callbacks else None,
        "observed_peak_ms_in_complete_windows": observed_peak,
        "phase_usage": phase_usage(run, windows),
        "max_action_lateness_ms": max((float(x["late_ms"]) for x in begins), default=0),
        "max_action_duration_ms": max((float(x["duration_ms"]) for x in ends), default=0),
        "slowest_actions": sorted(
            ({"index": int(x["index"]), "action": begins[i]["action"] if i < len(begins) else "?",
              "duration_ms": float(x["duration_ms"])} for i, x in enumerate(ends)),
            key=lambda x: x["duration_ms"], reverse=True)[:8],
        "_signature": signature,
    }


def report(paths):
    results = [summarize(p) for p in paths]
    output = {"runs": results, "note": "Deadline overruns and scheduling lateness are timing observations, not detected audible clicks."}
    if len(results) == 2:
        a, b = results
        differences = [key for key in ("scenario", "project_hash", "configuration", "output_device", "host_summary", "_signature") if a[key] != b[key]]
        output["matched_control"] = a["complete"] and b["complete"] and not differences
        output["mismatches"] = differences
    for result in results:
        result.pop("_signature")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", help="one or two saved session JSONL files")
    args = parser.parse_args()
    if len(args.logs) not in (1, 2):
        parser.error("provide one or two exports")
    try:
        output = report(args.logs)
    except (OSError, ValueError, KeyError) as error:
        print(str(error), file=sys.stderr)
        return 2
    print(json.dumps(output, indent=2))
    return 0 if all(r["complete"] for r in output["runs"]) and output.get("matched_control", True) else 1


if __name__ == "__main__":
    raise SystemExit(main())
