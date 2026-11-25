import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("live_logs", Path(__file__).parents[1] / "tools/compare_live_test_logs.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture():
    return [
        {"type": "audio_window", "t_ms": 5, "from_ms": 0, "peak_ms": 999, "over_budget": 999},
        {"type": "live_test_start", "t_ms": 100, "detail": "scenario=tree04-live-v1 steps=1 project_hash=abc backend=dummy rate=48000 frames=512 channels=4 workers=0 profile=0"},
        {"type": "live_test_checkpoint", "t_ms": 101, "detail": "generation=1 callbacks=100 frames=51200 over_budget=40"},
        {"type": "live_test_action_begin", "t_ms": 102, "detail": "index=0 action=finish target=0 value=0.000000 planned_ms=0 actual_ms=2 late_ms=2"},
        {"type": "audio_window", "t_ms": 185, "from_ms": 150, "peak_ms": 12, "over_budget": 2},
        {"type": "live_test_action_end", "t_ms": 201, "detail": "index=0 success=1 duration_ms=3"},
        {"type": "live_test_checkpoint", "t_ms": 202, "detail": "generation=1 callbacks=110 frames=56320 over_budget=42"},
        {"type": "live_test_complete", "t_ms": 203, "detail": "reason=finished"},
    ]


def stress_fixture():
    rows = []
    for job, stage in enumerate(("full_pass", "dense_closed", "dense_open", "dense_overlap", "short_overlap", "recovery")):
        for state, advance in (("begin", 0), ("end", 10)):
            calls = 100 + job * 20 + advance
            rows.append({"type": "live_test_stage", "t_ms": job * 100 + advance,
                         "detail": f"stage={stage} state={state} job={job} movement={-1 if job == 0 else 0} profile={int(job != 0)} callbacks={calls} frames={calls * 512} over_budget=0 near_budget=0 start_gaps=0"})
    for serial, (job, stage) in enumerate(((0, "full_pass"), (3, "dense_overlap"), (4, "short_overlap")), 1):
        rows.append({"type": "live_test_transition", "t_ms": job * 100 + 9,
                     "detail": f"transition={serial} job={job} stage={stage} from=0 to=0 from_id=42 to_id=42 requested_fade_s=.001 effective_fade_s=.001 dual_frames=48 progress_frames=48 completed=1 retired_reason=completed from_start_s=.009 from_end_s=.01 to_start_s=0 to_end_s=.001"})
    rows.append({"type": "live_test_coverage", "t_ms": 600,
                 "detail": "planned_transitions=3 observed_transitions=3 completed_transitions=3 interrupted_transitions=0 full_movements_expected=1 full_movements_completed=1 missing_sources=0 trace_overflow=0 coverage_complete=1 workload=content alignment_available=0 alignment_verified=0 alignment_reason=native_section_limits"})
    rows.append({"type": "live_test_complete", "t_ms": 601})
    return sorted(rows, key=lambda row: row["t_ms"])


class Comparison(unittest.TestCase):
    def test_density_alignment_requires_simultaneous_rendered_peaks(self):
        proof = ("job=6 method=peak_midpoints from=0 to=1 from_id=42 to_id=77 "
                 "from_point_s=20.05 to_point_s=40.05 from_density_start_s=20 from_density_end_s=20.1 "
                 "to_density_start_s=40 to_density_end_s=40.1 from_in_s=14.05 from_out_s=22.05 "
                 "to_in_s=39.05 to_out_s=43.05 from_speed=1 to_speed=.5 fade_s=4 phase_s=2")
        trace = {"job": "6", "stage": "aligned_overlap", "from": "0", "to": "1",
                 "from_id": "42", "to_id": "77", "completed": "1", "retired_reason": "completed",
                 "effective_fade_s": "4", "from_start_s": "18.05", "from_end_s": "22.05",
                 "to_start_s": "39.05", "to_end_s": "41.05", "progress_frames": "192000",
                 "common_density_frames": "4800", "density_witness_frame": "96000"}
        movements = [{"movement": 0, "id": "42", "length_seconds": 60, "inventory": {"speed": "1"}},
                     {"movement": 1, "id": "77", "length_seconds": 60, "inventory": {"speed": ".5"}}]
        stages = [{"job": "6", "stage": "aligned_overlap", "complete": True}]
        summary = {"alignment_available": "1", "alignment_verified": "1", "alignment_reason": "available"}
        def check(text, traces):
            rows = [{"type": "live_test_start", "detail": "rate=48000"},
                    {"type": "live_test_alignment", "detail": text}]
            return module.density_alignment(rows, stages, movements, traces, summary)
        result = check(proof, [trace])
        self.assertTrue(result["verified"], result["problems"])
        rounded = dict(trace, effective_fade_s="3.99998", from_start_s="18.05002")
        self.assertTrue(check(proof, [rounded])["verified"])
        self.assertFalse(check(proof, [dict(trace, effective_fade_s="3.99")])["verified"])
        self.assertFalse(check(proof, [dict(trace, common_density_frames="0")])["verified"])
        self.assertFalse(check(proof, [dict(trace, density_witness_frame="0")])["verified"])
        # Original 12-second outgoing window faded six seconds after its peak.
        late = dict(trace, from_start_s="26.05", from_end_s="30.05")
        self.assertIn("density_point_outside_rendered_overlap", check(proof, [late])["problems"])
        unsynchronized = dict(trace, to_start_s="38.05", to_end_s="40.05")
        self.assertIn("density_points_not_simultaneous", check(proof, [unsynchronized])["problems"])
        for changed in (proof.replace("to_speed=.5", "to_speed=1"),
                        proof.replace("from_id=42", "from_id=99"),
                        proof.replace("from_point_s=20.05", "from_point_s=nan"),
                        proof.replace("from_density_end_s=20.1", "from_density_end_s=20.01")):
            self.assertFalse(check(changed, [trace])["verified"])
        self.assertFalse(check(proof, [])["verified"])
        self.assertFalse(check(proof, [trace, trace])["verified"])

    def test_unavailable_density_alignment_is_explicit(self):
        summary = {"alignment_available": "0", "alignment_verified": "0", "alignment_reason": "native_section_limits"}
        result = module.density_alignment([], [], [], [], summary)
        self.assertFalse(result["available"])
        self.assertFalse(result["verified"])
        self.assertEqual(result["reason"], "native_section_limits")
        self.assertEqual(result["problems"], [])
        self.assertIn("missing_density_alignment_status", module.density_alignment([], [], [], [], {})["problems"])

    def test_short_crossfade_coverage_needs_audio_progress_not_windows(self):
        rows = stress_fixture()
        movements = [{"movement": 0, "id": "42", "length_seconds": .01}]
        stages = module.stage_usage(rows, [])
        self.assertEqual(len(stages), 6)
        self.assertTrue(all(s["complete"] and s["counter_deltas"]["callbacks"] == 10 for s in stages))
        self.assertTrue(all(s["average_callback_ms"] is None for s in stages))
        coverage = module.stress_coverage(rows, stages, movements)
        self.assertTrue(coverage["complete"], coverage["problems"])
        for row in rows:
            if row["type"] == "live_test_transition":
                row["detail"] = row["detail"].replace("progress_frames=48", "progress_frames=0")
                break
        coverage = module.stress_coverage(rows, stages, movements)
        self.assertFalse(coverage["complete"])
        self.assertIn("transition_without_dual_scene_progress", coverage["problems"])

    def test_stress_rejects_missing_duplicate_or_incomplete_evidence(self):
        movements = [{"movement": 0, "id": "42", "length_seconds": .01}]
        for mutation in ("missing_counter", "duplicate_begin", "orphan_end", "short_full_pass", "missing_source", "trace_overflow", "missing_inventory"):
            with self.subTest(mutation=mutation):
                rows = stress_fixture()
                if mutation == "missing_counter":
                    rows[0]["detail"] = rows[0]["detail"].replace("frames=51200", "")
                elif mutation == "duplicate_begin":
                    rows.append(dict(rows[0]))
                elif mutation == "orphan_end":
                    rows = rows[1:]
                elif mutation == "short_full_pass":
                    for row in rows:
                        if row["type"] == "live_test_transition" and "stage=full_pass" in row["detail"]:
                            row["detail"] = row["detail"].replace("from_end_s=.01", "from_end_s=.005")
                elif mutation in ("missing_source", "trace_overflow"):
                    key = "missing_sources" if mutation == "missing_source" else "trace_overflow"
                    rows[-2]["detail"] = rows[-2]["detail"].replace(f"{key}=0", f"{key}=1")
                inventory = [] if mutation == "missing_inventory" else movements
                rows.sort(key=lambda row: row["t_ms"])
                coverage = module.stress_coverage(rows, module.stage_usage(rows, []), inventory)
                self.assertFalse(coverage["complete"], mutation)

    def test_asynchronously_drained_audio_is_attributed_by_timestamp(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "drained.jsonl"
            rows = fixture()
            window = rows.pop(4)
            rows.append(window)  # Writer drains this after the completion marker.
            rows.append({"type": "audio_timing", "t_ms": 185, "duration_ms": 14,
                         "budget_ms": 10.666667, "over_budget": True,
                         "extra_start_gap_ms": 3})
            rows.append({"type": "audio_timing", "t_ms": 90, "duration_ms": 999,
                         "budget_ms": 10.666667, "over_budget": True})
            rows.append({"type": "audio_timing", "t_ms": 201, "duration_ms": 99,
                         "budget_ms": 10.666667, "over_budget": True})
            rows.append({"type": "audio_window", "t_ms": 201, "from_ms": 186,
                         "peak_ms": 99, "over_budget": 1})
            path.write_text("\n".join(map(json.dumps, rows)), encoding="utf-8")
            run = module.report([path])["runs"][0]
            self.assertTrue(run["complete"])
            self.assertEqual(run["observed_peak_ms_in_complete_windows"], 12)
            self.assertEqual(run["timing"]["observed_peak_ms"], 14)
            self.assertEqual(run["timing"]["flagged_over_budget_events"], 1)
            self.assertEqual(run["timing"]["boundary_timing_events_excluded"], 1)
            self.assertEqual(run["deadline_status"], "deadline_misses_observed")
            self.assertAlmostEqual(run["buffer_budget_ms"], 512 / 48)

    def test_timing_keeps_paused_tails_and_start_gaps_separate(self):
        run = [
            {"type": "live_test_phase", "t_ms": 0, "detail": "mode=playing movement=1 fx_canvas=0"},
            {"type": "audio_timing", "t_ms": 1, "duration_ms": 15, "over_budget": True},
            {"type": "live_test_phase", "t_ms": 2, "detail": "mode=paused_tails movement=1 fx_canvas=1"},
            {"type": "audio_timing", "t_ms": 3, "duration_ms": 12, "over_budget": True},
            {"type": "audio_timing", "t_ms": 4, "duration_ms": .1, "over_budget": False, "extra_start_gap_ms": 20},
        ]
        timing = module.timing_usage(run, [])
        self.assertEqual(timing["flagged_over_budget_events"], 2)
        self.assertEqual(timing["largest_extra_start_gap_ms"], 20)
        self.assertEqual([(r["mode"], r["over_budget_events"]) for r in timing["over_budget_by_movement_and_state"]],
                         [("playing", 1), ("paused_tails", 1)])

    def test_stage_windows_do_not_mix_profiled_and_unprofiled_passes(self):
        run = [
            {"type": "live_test_stage", "t_ms": 0, "detail": "stage=full_pass movement=0 profile=0"},
            {"type": "live_test_stage", "t_ms": 10000, "detail": "stage=dense_closed movement=0 profile=1"},
            {"type": "live_test_complete", "t_ms": 20000},
        ]
        windows = [{"from_ms": a, "t_ms": b, "callbacks": 10, "average_ms": avg, "peak_ms": avg}
                   for a, b, avg in [(100, 9000, 1), (9000, 11000, 999), (11000, 19000, 2)]]
        stages = module.stage_usage(run, windows)
        self.assertEqual([s["average_callback_ms"] for s in stages], [1, 2])
        self.assertEqual([s["profile"] for s in stages], ["0", "1"])

    def test_idle_windows_exclude_boundaries_and_settling(self):
        run = [{"type": "live_test_phase", "t_ms": 100, "detail": "mode=stopped fx_canvas=1 settle_ms=2000"},
               {"type": "live_test_phase", "t_ms": 6100, "detail": "mode=playing fx_canvas=0 settle_ms=2000"},
               {"type": "live_test_complete", "t_ms": 12000}]
        def window(a, b, average, count):
            return {"from_ms": a, "t_ms": b, "average_ms": average, "callbacks": count, "peak_ms": average * 2}
        windows = [window(0, 3000, 999, 100), window(2100, 4000, .1, 100), window(4000, 6000, .2, 200),
                   window(6000, 9000, 999, 100), window(9000, 11000, 2, 100)]
        result = {r["mode"]: r for r in module.phase_usage(run, windows)}
        self.assertEqual(result["stopped"]["windows"], 2)
        self.assertAlmostEqual(result["stopped"]["average_callback_ms"], 1 / 6)
        self.assertEqual(result["playing"]["peak_callback_ms"], 4)

    def test_movements_stay_separate_and_names_are_preserved(self):
        run = [{"type": "live_test_movement", "detail":
                f"movement={i} id={10+i} cards=1 markers=0 length_s=2 playback=sampled_loop name=Movement {i} key=value"}
               for i in range(2)]
        run += [{"type": "live_test_phase", "t_ms": 0, "detail": "mode=playing fx_canvas=1 movement=0 settle_ms=2000"},
                {"type": "live_test_phase", "t_ms": 6000, "detail": "mode=playing fx_canvas=1 movement=1 settle_ms=2000"},
                {"type": "live_test_complete", "t_ms": 12000}]
        windows = [{"from_ms": a, "t_ms": b, "callbacks": 10, "average_ms": load, "peak_ms": load, "over_budget": 0}
                   for a, b, load in [(2000, 5000, 1), (5000, 8000, 999), (8000, 11000, 5)]]
        result = module.movement_usage(run, windows)
        self.assertEqual([m["name"] for m in result], ["Movement 0 key=value", "Movement 1 key=value"])
        self.assertEqual([m["phase_usage"][0]["average_callback_ms"] for m in result], [1, 5])

    def test_gallery_requires_inventory_for_every_movement(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "gallery.jsonl"
            rows = fixture()
            rows[1]["detail"] = rows[1]["detail"].replace("tree04-live-v1", "gallery-live-v1") + " movements=2"
            for count in (0, 1, 2):
                data = rows[:2] + [{"type": "live_test_movement", "t_ms": 101, "detail":
                    f"movement={i} id={i+1} cards=0 markers=0 length_s=0 playback=skipped_empty name=Empty {i}"}
                    for i in range(count)] + rows[2:]
                path.write_text("\n".join(map(json.dumps, data)), encoding="utf-8")
                self.assertEqual(module.report([path])["runs"][0]["complete"], count == 2)

    def test_matched_and_changed(self):
        with tempfile.TemporaryDirectory() as folder:
            a, b = Path(folder) / "a.jsonl", Path(folder) / "b.jsonl"
            rows = fixture()
            def save(path):
                path.write_text("\n".join(json.dumps(row) for row in rows), encoding="utf-8")
            save(a)
            save(b)
            result = module.report([a, b])
            self.assertTrue(result["matched_control"])
            self.assertEqual(result["runs"][0]["callbacks"], 10)
            self.assertEqual(result["runs"][0]["over_budget"], 2)
            self.assertEqual(result["runs"][0]["observed_peak_ms_in_complete_windows"], 12)
            rows[3]["detail"] = rows[3]["detail"].replace("target=0", "target=1")
            save(b)
            self.assertFalse(module.report([a, b])["matched_control"])

    def test_device_name_and_host(self):
        self.assertEqual(module.fields({"detail": "output=MOTU M Series connection=active"})["output"], "MOTU M Series")
        with tempfile.TemporaryDirectory() as folder:
            a, b = Path(folder) / "a.jsonl", Path(folder) / "b.jsonl"
            rows = fixture() + [
                {"type": "audio_device", "t_ms": 101, "detail": "generation=1 output=MOTU M Series connection=active"},
                {"type": "session_start", "t_ms": 0, "detail": "platform=Windows; CPUs=12; RAM_MB=32768;"},
            ]
            a.write_text("\n".join(map(json.dumps, rows)), encoding="utf-8")
            rows[-2]["detail"] = rows[-2]["detail"].replace("MOTU M Series", "Other Output")
            b.write_text("\n".join(map(json.dumps, rows)), encoding="utf-8")
            result = module.report([a, b])
            self.assertFalse(result["matched_control"])
            self.assertIn("output_device", result["mismatches"])
            self.assertEqual(result["runs"][0]["host_summary"]["CPUs"], "12")

    def test_failed_missing_and_reset(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "run.jsonl"
            for mutation in ("failed", "missing", "reset"):
                rows = fixture()
                if mutation == "failed":
                    rows[5]["detail"] = rows[5]["detail"].replace("success=1", "success=0")
                elif mutation == "missing":
                    del rows[3]
                else:
                    rows[6]["detail"] = rows[6]["detail"].replace("generation=1", "generation=2")
                path.write_text("\n".join(map(json.dumps, rows)), encoding="utf-8")
                self.assertFalse(module.report([path])["runs"][0]["complete"])


if __name__ == "__main__":
    unittest.main()
