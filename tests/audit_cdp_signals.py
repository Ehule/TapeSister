#!/usr/bin/env python3
"""Focused native-signal checks accompanying the CDP candidate audit.

Requires numpy/scipy; does not exercise TapeSister's adapter or UI. Keeps failed
checks in results.json and exits nonzero if a stated signal expectation fails.
"""
import argparse
import json
from pathlib import Path
import warnings

import numpy as np
from scipy.io import wavfile

from audit_cdp_candidates import CASES, execute, fixtures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cdp-bin", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    args = parser.parse_args()
    binary = args.cdp_bin.resolve()
    root = args.out_dir.resolve()
    results = []

    def record(name, rate, passed, **measurements):
        results.append(dict(check=name, rate=rate, passed=bool(passed), **measurements))
        print(rate, name, "PASS" if passed else "FAIL", measurements, flush=True)
        (root / "results.json").write_text(json.dumps(results, indent=2))

    def render(name, rate, data, prefix, params):
        job = root / str(rate) / name
        job.mkdir(parents=True, exist_ok=True)
        wavfile.write(job / "input.wav", rate, data.astype(np.float32))
        (job / "output.wav").unlink(missing_ok=True)
        words = prefix.split()
        cmd = [str(binary / words[0]), *words[1:], "input.wav", "-foutput.wav", *params.split()]
        code, log, _ = execute(cmd, job)
        (job / "run.log").write_text(log)
        if code:
            raise RuntimeError(f"{name}: CDP exit {code}: {log[-300:]}")
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", wavfile.WavFileWarning)
            sr, out = wavfile.read(job / "output.wav")
        if out.ndim == 1:
            out = out[:, None]
        if sr != rate or not len(out) or not np.isfinite(out).all():
            raise RuntimeError(f"{name}: invalid output")
        return out.astype(np.float64)

    for rate in (44100, 48000):
        linked = fixtures(rate)["linked"].astype(np.float32)
        for ident, prefix, params, _ in CASES:
            if ident.startswith("extend.") and ("-s17" in params or "-r17" in params):
                a = render(ident + "-seed17a", rate, linked, prefix, params)
                b = render(ident + "-seed17b", rate, linked, prefix, params)
                c = render(ident + "-seed19", rate, linked, prefix,
                           params.replace("-s17", "-s19").replace("-r17", "-r19"))
                same = np.array_equal(a, b)
                changed = not np.array_equal(a, c)
                error = float(np.max(np.abs(a[:, 1] + .25 * a[:, 0])))
                record(ident + "-seed-and-link", rate, same and changed and error < 1e-6,
                       same_seed_identical=same, different_seed_changes=changed, linked_error=error)

        # Right channel dominates the peak: detect left-only or separate gain scans.
        data = linked.copy()
        data[:, 1] *= 12
        for mode in (3, 4):
            out = render(f"normalize{mode}", rate, data, f"modify loudness {mode}", "-l0.8")
            expected = data.astype(np.float64) * (.8 / np.max(np.abs(data)))
            error = float(np.max(np.abs(out - expected)))
            record(f"normalize{mode}-shared-gain", rate, error < 1e-6, max_error=error)

        out = render("silend", rate, data, "silend silend 1", "0.25")
        prefix_equal = np.array_equal(out[:len(data)], data)
        tail_silent = bool(np.all(out[len(data):] == 0))
        record("silend-content", rate,
               len(out) == len(data) + rate // 4 and prefix_equal and tail_silent,
               prefix_equal=prefix_equal, tail_silent=tail_silent, frames=len(out))

        # A stationary two-tone input makes the low-pass behavior measurable.
        t = np.arange(rate * 2) / rate
        tones = (.15 * np.sin(2 * np.pi * 200 * t) + .15 * np.sin(2 * np.pi * 6000 * t))[:, None]
        out = render("lohi", rate, tones, "filter lohi 1", "-40 1000 2500 -t0.1 -s0.5")
        section = out[rate // 2:rate + rate // 2, 0]
        amps = [abs(np.sum(section * np.exp(-2j * np.pi * f * np.arange(rate) / rate)))
                for f in (200, 6000)]
        ratio_db = float(20 * np.log10(max(amps[1], 1e-30) / amps[0]))
        record("lohi-band-rejection", rate, ratio_db < -30, high_relative_to_low_db=ratio_db)

        tone = (.2 * np.sin(2 * np.pi * 220 * t))[:, None]
        out = render("accelerate", rate, tone, "modify speed 5", "2 1 -s0.1")
        def crossing_frequency(start, end):
            y = out[int(start * rate):int(end * rate), 0]
            return float(np.count_nonzero((y[:-1] <= 0) & (y[1:] > 0)) / (end - start))
        early, late = crossing_frequency(.02, .08), crossing_frequency(.95, 1.1)
        record("acceleration-pitch-and-length", rate,
               len(out) < len(tone) and 190 < early < 250 and late > 400,
               early_hz=early, late_hz=late, seconds=len(out) / rate)

        out = render("stack", rate, tone, "modify stack", "12 3 1 0 0.5 1")
        section = out[rate // 10:rate // 2, 0]
        amps = [float(abs(np.sum(section * np.exp(-2j * np.pi * f * np.arange(len(section)) / rate))))
                for f in (220, 440, 880)]
        record("stack-octave-components", rate, min(amps) > 1 and max(amps) / min(amps) < 1.2,
               component_amplitudes=amps)

        impulse = np.zeros((rate * 2, 2), dtype=np.float32)
        impulse[rate // 100, 0] = .2
        out = render("newdelay", rate, impulse, "newdelay newdelay", "48 0.4 0.5")
        at = np.flatnonzero(np.abs(out[:, 0]) > 1e-5)
        period = int(round(rate / (440 * 2 ** ((48 - 69) / 12))))
        gaps = np.diff(at)
        record("newdelay-tuned-impulses", rate,
               len(gaps) >= 3 and bool(np.all(gaps == period)) and bool(np.all(out[:, 1] == 0)),
               expected_period=period, measured_periods=np.unique(gaps).tolist())

        out = render("bounce", rate, impulse, "bounce bounce", "6 0.3 0.75 0.1 1 -s0.03")
        at = np.flatnonzero(np.abs(out[:, 0]) > 1e-5)
        gaps = np.diff(at)
        record("bounce-accelerating-repeats", rate,
               len(gaps) >= 3 and bool(np.all(np.diff(gaps) < 0)) and bool(np.all(out[:, 1] == 0)),
               impulse_gaps_frames=gaps.tolist())

    raise SystemExit(0 if all(r["passed"] for r in results) else 1)


if __name__ == "__main__":
    main()
