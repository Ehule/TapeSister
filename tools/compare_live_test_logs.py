#!/usr/bin/env python3
"""Summarize one or compare two TapeSister live diagnostic session exports.

Uses counter differences inside the run, never the session's retained maxima.
Action order/targets are checked before timing comparisons are described as matched.
"""
import argparse
from bisect import bisect_right
import json
import math
import re
import statistics
import sys
from pathlib import Path


def fields(row):
    return dict(re.findall(r"(\w+)=(.*?)(?=\s+\w+=|$)", row.get("detail", "")))


def window_end(row):
    # Schema 1 stamps the final callback's START. Its window peak is a safe
    # upper bound for that callback's duration, so boundary attribution is
    # conservative until a producer supplies an exact completion timestamp.
    return row.get("end_ms", row["t_ms"] + row.get("peak_ms", 0))


def phase_usage(run, windows, by_movement=False):
    """Use complete windows after settling; never mix transition boundaries."""
    phases = [r for r in run if r.get("type") == "live_test_phase"]
    groups = {}
    for i, row in enumerate(phases):
        state = fields(row)
        begin = row["t_ms"] + float(state.get("settle_ms", 0))
        end = phases[i + 1]["t_ms"] if i + 1 < len(phases) else run[-1]["t_ms"]
        key = (state["mode"], state["fx_canvas"], int(state.get("movement", -1)) if by_movement else -1)
        groups.setdefault(key, []).extend(w for w in windows if w["from_ms"] >= begin and window_end(w) <= end)
    result = []
    for (mode, canvas, movement), samples in sorted(groups.items()):
        callbacks = sum(w.get("callbacks", 0) for w in samples)
        result.append({"mode": mode, "fx_canvas": canvas == "1", "windows": len(samples),
                       "callbacks": callbacks,
                       "average_callback_ms": sum(w.get("average_ms", 0) * w.get("callbacks", 0) for w in samples) / callbacks if callbacks else None,
                       "peak_callback_ms": max((w["peak_ms"] for w in samples), default=None),
                       "over_budget": sum(w.get("over_budget", 0) for w in samples)})
        if by_movement:
            result[-1]["movement"] = movement
    return result


def movement_usage(run, windows):
    phases = phase_usage(run, windows, by_movement=True)
    result = []
    for row in run:
        if row.get("type") != "live_test_movement":
            continue
        metadata, _, name = row.get("detail", "").partition(" name=")
        info = fields({"detail": metadata})
        index = int(info["movement"])
        result.append({"movement": index, "name": name, "id": info["id"],
                       "length_seconds": float(info["length_s"]),
                       "cards": int(info["cards"]), "markers": int(info["markers"]),
                       "playback": info["playback"],
                       "inventory": info,
                       "phase_usage": [p for p in phases if p["movement"] == index]})
    return result


def timing_usage(run, windows, begin_ms=None, end_ms=None):
    """Separate callback deadlines from arrival gaps; do not call either clicks."""
    all_events = [r for r in run if r.get("type") == "audio_timing"]
    events = [r for r in all_events
              if (begin_ms is None or r["t_ms"] >= begin_ms)
              and (end_ms is None or r["t_ms"] + r["duration_ms"] <= end_ms)]
    peaks = [r["peak_ms"] for r in windows]
    peaks.extend(r["duration_ms"] for r in events)
    loads = [r["load_percent"] for r in windows if "load_percent" in r]
    phases = [r for r in run if r.get("type") == "live_test_phase"]
    boundaries = [r["t_ms"] for r in phases]
    by_movement = {}
    for row in events:
        if not row.get("over_budget"):
            continue
        index = bisect_right(boundaries, row["t_ms"]) - 1
        phase = fields(phases[index]) if index >= 0 else {}
        key = (int(phase.get("movement", -1)), phase.get("mode", "unattributed"))
        item = by_movement.setdefault(key, {"movement": key[0], "mode": key[1],
                                           "over_budget_events": 0, "peak_ms": 0})
        item["over_budget_events"] += 1
        item["peak_ms"] = max(item["peak_ms"], row["duration_ms"])
    return {
        "observed_peak_ms": max(peaks, default=None),
        "flagged_over_budget_events": sum(bool(r.get("over_budget")) for r in events),
        "boundary_timing_events_excluded": len(all_events) - len(events),
        "largest_extra_start_gap_ms": max((r.get("extra_start_gap_ms", 0) for r in events), default=0),
        "median_window_average_load_percent": statistics.median(loads) if loads else None,
        "over_budget_by_movement_and_state": list(by_movement.values()),
        "note": "Event durations are elapsed wall time, including possible preemption. Window averages are not individual-callback percentiles; transition counters provide overlap attribution.",
    }


def system_usage(run):
    samples = [r for r in run if r.get("type") == "system"]
    def values(key):
        return [r[key] for r in samples if r.get(key, -1) >= 0]
    cpu = values("system_cpu_percent")
    process = values("process_cpu_one_core_percent")
    return {
        "samples": len(samples),
        "median_system_cpu_percent": statistics.median(cpu) if cpu else None,
        "peak_system_cpu_percent": max(cpu, default=None),
        "median_process_cpu_one_core_percent": statistics.median(process) if process else None,
        "peak_resident_bytes": max(values("resident_bytes"), default=None),
        "minimum_system_available_bytes": min(values("system_available_bytes"), default=None),
    }


def stage_usage(run, windows):
    """Retain separate full-play, matched-position and recovery intervals."""
    stages = [r for r in run if r.get("type") == "live_test_stage"]
    result = []
    for index, row in enumerate(stages):
        info = fields(row)
        if info.get("state") == "end":
            continue
        if info.get("state") == "begin":
            end_row = next((r for r in stages[index + 1:]
                            if fields(r).get("state") == "end"
                            and all(fields(r).get(k) == info.get(k) for k in ("job", "stage", "movement"))), None)
        else:
            end_row = stages[index + 1] if index + 1 < len(stages) else run[-1]
        complete = end_row is not None
        end_row = end_row or run[-1]
        ending = fields(end_row)
        begin = row["t_ms"]
        end = end_row["t_ms"]
        samples = [w for w in windows if w["from_ms"] >= begin and window_end(w) <= end]
        callbacks = sum(w.get("callbacks", 0) for w in samples)
        counters = {}
        if info.get("state") == "begin" and any(
                key not in info or key not in ending
                for key in ("callbacks", "frames", "over_budget", "near_budget", "start_gaps")):
            complete = False
        for key in ("callbacks", "frames", "over_budget", "near_budget", "start_gaps"):
            if complete and key in info and key in ending:
                counters[key] = int(ending[key]) - int(info[key])
                complete = complete and counters[key] >= 0
        if info.get("state") == "begin" and (counters.get("callbacks", 0) <= 0 or counters.get("frames", 0) <= 0):
            complete = False
        result.append({"stage": info.get("stage"), "movement": int(info.get("movement", -1)),
                       "job": info.get("job"), "complete": complete,
                       "profile": info.get("profile"), "from_ms": begin, "to_ms": end,
                       "counter_deltas": counters,
                       "metadata": info, "complete_windows": len(samples),
                       "callbacks_in_complete_windows": callbacks,
                       "average_callback_ms": sum(w.get("average_ms", 0) * w.get("callbacks", 0) for w in samples) / callbacks if callbacks else None,
                       "peak_callback_ms_in_complete_windows": max((w["peak_ms"] for w in samples), default=None),
                       "over_budget_in_complete_windows": sum(w.get("over_budget", 0) for w in samples)})
    return result


def density_alignment(run, stages, movements, transitions, summary):
    """Verify that both selected density points actually shared a fade phase."""
    records = [fields(r) for r in run if r.get("type") == "live_test_alignment"]
    traces = [t for t in transitions if t.get("stage") == "aligned_overlap"]
    jobs = [s for s in stages if s["stage"] == "aligned_overlap"]
    available = summary.get("alignment_available")
    problems = []
    if available not in ("0", "1") or not summary.get("alignment_reason"):
        problems.append("missing_density_alignment_status")
    elif available == "0":
        if records or traces or jobs or summary.get("alignment_verified") != "0":
            problems.append("unexpected_density_alignment_evidence")
    else:
        if summary.get("alignment_verified") != "1":
            problems.append("density_alignment_not_verified")
        if len(records) != 1 or len(traces) != 1 or len(jobs) != 1:
            problems.append("missing_or_duplicate_density_alignment_evidence")
        else:
            proof, trace, stage = records[0], traces[0], jobs[0]
            inventory = {str(m["movement"]): m for m in movements}
            try:
                start = next(fields(r) for r in run if r.get("type") == "live_test_start")
                rate = float(start["rate"])
                fade, phase = float(proof["fade_s"]), float(proof["phase_s"])
                actual_fade = float(trace["effective_fade_s"])
                if not all(math.isfinite(v) for v in (rate, fade, phase, actual_fade)) or rate <= 0:
                    raise ValueError("invalid alignment timing")
                tolerance = 2 / rate + 1e-8  # Two independently rounded source positions.
                if (not stage["complete"] or proof["job"] != trace["job"] or proof["job"] != stage["job"]
                        or trace.get("completed") != "1" or trace.get("retired_reason") != "completed"
                        or not 0 < phase < fade or actual_fade > fade + 1e-8
                        or fade - actual_fade > 1 / rate + 1e-8):
                    problems.append("invalid_density_alignment_transition")
                observed_phases = []
                density_ranges = []
                for side in ("from", "to"):
                    movement = inventory[proof[side]]
                    if trace[side] != proof[side] or proof[side + "_id"] != trace[side + "_id"] or trace[side + "_id"] != movement["id"]:
                        problems.append("density_alignment_identity_mismatch")
                    point, speed, begin, end, dense_begin, dense_end = (
                        float(proof[side + suffix]) for suffix in
                        ("_point_s", "_speed", "_in_s", "_out_s", "_density_start_s", "_density_end_s"))
                    actual_begin, actual_end = float(trace[side + "_start_s"]), float(trace[side + "_end_s"])
                    if not all(math.isfinite(v) for v in (point, speed, begin, end, dense_begin, dense_end, actual_begin, actual_end)):
                        raise ValueError("nonfinite alignment proof")
                    if not .5 <= speed <= 2:
                        raise ValueError("invalid native movement speed")
                    source_tolerance = tolerance * speed
                    if (not 0 <= begin < end <= movement["length_seconds"] + source_tolerance
                            or not dense_begin <= point < dense_end
                            or not actual_begin - source_tolerance <= point <= actual_end + source_tolerance
                            or (end - begin) / speed + tolerance < 2 * fade):
                        problems.append("density_point_outside_rendered_overlap")
                    saved_speed = movement.get("inventory", {}).get("speed")
                    if saved_speed is not None and abs(float(saved_speed) - speed) > 1e-6:
                        problems.append("density_alignment_speed_mismatch")
                    actual_phase = (point - actual_begin) / speed
                    observed_phases.append(actual_phase)
                    density_ranges.append((actual_begin, speed, dense_begin, dense_end))
                    if not -tolerance <= actual_phase <= fade + tolerance or abs(actual_phase - phase) > tolerance:
                        problems.append("density_points_not_simultaneous")
                if abs(observed_phases[0] - observed_phases[1]) > tolerance:
                    problems.append("density_points_not_simultaneous")
                # A nearby geometric phase is insufficient for a sub-sample
                # burst: require a shared discrete rendered-frame witness.
                shared = int(trace["common_density_frames"])
                witness = int(trace["density_witness_frame"])
                progress = int(trace["progress_frames"])
                if not 0 < shared <= progress or not 0 <= witness < progress:
                    problems.append("no_common_rendered_density_sample")
                elif any(not low <= begin + witness * speed / rate < high
                         for begin, speed, low, high in density_ranges):
                    problems.append("density_witness_outside_peak_intervals")
            except (KeyError, ValueError, StopIteration):
                problems.append("invalid_density_alignment_proof")
    return {"available": available == "1", "verified": available == "1" and not problems,
            "reason": summary.get("alignment_reason", "not_recorded"),
            "problems": sorted(set(problems)), "evidence": records}


def stress_coverage(run, stages, movements):
    """A command succeeding is not evidence that a crossfade rendered audio."""
    summaries = [fields(r) for r in run if r.get("type") == "live_test_coverage"]
    transitions = [fields(r) for r in run if r.get("type") == "live_test_transition"]
    info = summaries[-1] if summaries else {}
    problems = []
    expected_movements = {m["movement"]: m for m in movements if m["length_seconds"] >= .001}
    if len(summaries) != 1:
        problems.append("missing_or_duplicate_coverage_summary")
    required = ("planned_transitions", "observed_transitions", "completed_transitions",
                "interrupted_transitions", "full_movements_expected", "full_movements_completed",
                "missing_sources", "trace_overflow", "coverage_complete")
    if any(k not in info for k in required):
        problems.append("missing_coverage_fields")
    else:
        if info["coverage_complete"] != "1":
            problems.append("controller_reported_incomplete_coverage")
        if int(info["planned_transitions"]) != int(info["observed_transitions"]) or int(info["observed_transitions"]) != len(transitions):
            problems.append("transition_count_mismatch")
        if int(info["full_movements_expected"]) != int(info["full_movements_completed"]):
            problems.append("full_playback_incomplete")
        if int(info["full_movements_expected"]) != len(expected_movements):
            problems.append("full_playback_inventory_mismatch")
        if int(info["missing_sources"]):
            problems.append("missing_sources")
        if int(info["trace_overflow"]):
            problems.append("transition_trace_lost")
        if int(info.get("unsupported_movements", 0)):
            problems.append("unsupported_movement_duration")
        completed = sum(t.get("completed") == "1" for t in transitions)
        interrupted = sum(t.get("retired_reason") == "interrupted" for t in transitions)
        if completed != int(info["completed_transitions"]) or interrupted != int(info["interrupted_transitions"]):
            problems.append("transition_retirement_count_mismatch")
    ids = [t.get("transition") for t in transitions]
    if any(x is None for x in ids) or len(set(ids)) != len(ids):
        problems.append("missing_or_duplicate_transition_identity")
    for transition in transitions:
        dual, progress = int(transition.get("dual_frames", 0)), int(transition.get("progress_frames", 0))
        if dual <= 0 or progress <= 0 or progress > dual:
            problems.append("transition_without_dual_scene_progress")
        if (transition.get("completed"), transition.get("retired_reason")) not in (("1", "completed"), ("0", "interrupted")):
            problems.append("transition_not_retired")
        if transition.get("retired_reason") == "interrupted" and transition.get("stage") != "interrupted_overlap":
            problems.append("unexpected_transition_interruption")
    stage_records = [fields(r) for r in run if r.get("type") == "live_test_stage"]
    stage_counts = {}
    for row in stage_records:
        key = tuple(row.get(k) for k in ("job", "stage", "movement"))
        counts = stage_counts.setdefault(key, {"begin": 0, "end": 0})
        state = row.get("state")
        if state not in counts:
            problems.append("invalid_stage_boundary")
        else:
            counts[state] += 1
    if any(c != {"begin": 1, "end": 1} for c in stage_counts.values()):
        problems.append("duplicate_or_unpaired_stage_boundaries")
    if (expected_movements and not stages) or any(not s["complete"] for s in stages):
        problems.append("missing_or_incomplete_stages")
    if expected_movements:
        for stage in ("dense_closed", "dense_open"):
            if {s["movement"] for s in stages if s["stage"] == stage} != set(expected_movements):
                problems.append(f"missing_{stage}_coverage")
        if any(not any(s["stage"] == stage for s in stages) for stage in ("full_pass", "dense_overlap", "short_overlap", "recovery")):
            problems.append("missing_full_overlap_or_recovery_stage")
        full = set()
        for transition in transitions:
            if transition.get("stage") != "full_pass" or transition.get("completed") != "1":
                continue
            movement = int(transition.get("from", -1))
            if movement in expected_movements:
                inventory = expected_movements[movement]
                if (transition.get("from_id") == inventory["id"]
                        and float(transition.get("from_end_s", -1)) + 1e-5 >= inventory["length_seconds"]):
                    full.add(movement)
        if full != set(expected_movements):
            problems.append("missing_audio_clock_endpoints")
    alignment = density_alignment(run, stages, movements, transitions, info)
    problems.extend(alignment["problems"])
    return {"complete": not problems, "problems": sorted(set(problems)),
            "summary": info, "transitions": transitions,
            "density_alignment": alignment,
            "note": "Dual-scene progress is counted in the audio renderer, including sub-window fades. Deadline counts belong to stage intervals; silent or externally fed content is not proof of audible output."}


def summarize(path):
    rows = [json.loads(line) for line in Path(path).read_text(encoding="utf-8").splitlines() if line.strip()]
    # Audio and UI records are drained asynchronously. File order can put a
    # valid in-run audio window after the completion marker, or before Start.
    rows.sort(key=lambda row: row.get("t_ms", 0))
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
    checkpoints = [r for r in run if r.get("type") == "live_test_checkpoint"]
    counters = [fields(r) for r in checkpoints]
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
    measurement_begin, measurement_end = checkpoints[0]["t_ms"], checkpoints[-1]["t_ms"]
    windows = [r for r in run if r.get("type") == "audio_window"
               and r.get("from_ms", -1) >= measurement_begin and window_end(r) <= measurement_end]
    movements = movement_usage(run, windows)
    if start["scenario"] in ("gallery-live-v1", "gallery-stress-v1"):
        expected = int(start.get("movements", 0))
        valid = valid and expected > 0 and [m["movement"] for m in movements] == list(range(expected))
    stages = stage_usage(run, windows)
    coverage = stress_coverage(run, stages, movements) if start["scenario"] == "gallery-stress-v1" else None
    if coverage is not None:
        valid = valid and coverage["complete"]
    observed_peak = max((r["peak_ms"] for r in windows), default=None)
    device_rows = [r for r in rows if r.get("type") == "audio_device"
                   and fields(r).get("generation") == counters[0]["generation"]
                   and r["t_ms"] <= run[-1]["t_ms"]]
    device = fields(device_rows[-1]) if device_rows else {}
    header = next((r.get("detail", "") for r in rows if r.get("type") == "session_start"), "")
    host = dict(re.findall(r"(platform|CPUs|RAM_MB)=([^;]+)", header))
    signature = [tuple(x[k] for k in ("index", "action", "target", "value", "planned_ms")) for x in begins]
    timing = timing_usage(run, windows, measurement_begin, measurement_end)
    if timing["flagged_over_budget_events"] > overruns:
        valid = False  # A clean counter result cannot contradict complete events.
    rate, frames = float(start.get("rate", 0)), float(start.get("frames", 0))
    budget = 1000 * frames / rate if rate > 0 and frames > 0 else None
    peak = timing["observed_peak_ms"]
    return {
        "file": str(path), "complete": valid, "reason": end.get("reason", "unknown"),
        "scenario": start["scenario"], "project_hash": start["project_hash"],
        "configuration": {k: start.get(k) for k in ("backend", "rate", "frames", "channels", "workers", "profile")},
        "output_device": device.get("output"), "host_summary": host,
        "build_revision": start.get("revision", "unknown"),
        "completed_actions": len(ends), "planned_actions": planned,
        "callbacks": callbacks, "over_budget": overruns,
        "deadline_status": "incomplete" if not valid else "deadline_misses_observed" if overruns else "no_recorded_deadline_misses",
        "buffer_budget_ms": budget,
        "observed_peak_budget_percent": 100 * peak / budget if peak is not None and budget else None,
        "over_budget_percent": 100 * overruns / callbacks if callbacks else None,
        "observed_peak_ms_in_complete_windows": observed_peak,
        "phase_usage": phase_usage(run, windows),
        "movements": movements,
        "timing": timing,
        "system_usage": system_usage(run),
        "stages": stages,
        "stress_coverage": coverage,
        "workload_status": coverage["summary"].get("workload", "unknown") if coverage is not None else "screening",
        "schedule_adjustments": [fields(r) for r in run if r.get("type") == "live_test_schedule_adjustment"],
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
