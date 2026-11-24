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
        {"type": "audio_window", "t_ms": 200, "from_ms": 150, "peak_ms": 12, "over_budget": 2},
        {"type": "live_test_action_end", "t_ms": 201, "detail": "index=0 success=1 duration_ms=3"},
        {"type": "live_test_checkpoint", "t_ms": 202, "detail": "generation=1 callbacks=110 frames=56320 over_budget=42"},
        {"type": "live_test_complete", "t_ms": 203, "detail": "reason=finished"},
    ]


class Comparison(unittest.TestCase):
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
