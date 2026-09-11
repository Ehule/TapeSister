#!/usr/bin/env python3
"""Opt-in native CDP feasibility probes, not Portal enablement/regression gates.

Requires numpy and scipy. Each case runs in an isolated scratch directory.
Results retain failures, commands and signal measurements for audit review.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
import warnings

import numpy as np
from scipy.io import wavfile


# id, command before input/output, scalar arguments, current Portal coverage
CASES = [
    ("extend.zigzag.1", "extend zigzag 1", "0.2 1.8 3 0.1 -s10 -m0.5 -r17", "factory"),
    ("extend.drunk.1", "extend drunk 1", "3 1 0.7 0.2 0.08 -s10 -r17", "factory"),
    ("extend.drunk.2", "extend drunk 2", "3 1 0.7 0.2 0.08 4 8 -s10 -r17 -l0.2 -h0.4", "new_mode"),
    ("extend.iterate.1", "extend iterate 1", "3 -d0.2 -r0.2 -p2 -a0.1 -f0.15 -g0.2 -s17", "new_mode"),
    ("extend.iterate.2", "extend iterate 2", "4 -d0.2 -r0.2 -p2 -a0.1 -f0.15 -g0.2 -s17", "factory"),
    ("extend.freeze.1", "extend freeze 1", "3 0.08 0.1 2 0.1 0.5 0.8 0.5 -s17", "new_mode"),
    ("extend.freeze.2", "extend freeze 2", "8 0.08 0.1 2 0.1 0.5 0.8 0.5 -s17", "factory"),
    ("extend.baktobak", "extend baktobak", "0.7 10", "new"),
    ("bounce.bounce", "bounce bounce", "6 0.3 0.75 0.1 1 -s0.03", "new"),
    ("modify.speed.5", "modify speed 5", "2 1 -s0.1", "new"),
    ("modify.stack", "modify stack", "7 3 0.7 0 0.25 1", "new"),
    ("modify.radical.3", "modify radical 3", "3 -l-12 -h7 -s0.1 -e1.8", "factory"),
    ("filter.lohi.1", "filter lohi 1", "-40 1000 2500 -t0.1 -s0.5", "new"),
    ("dvdwind.dvdwind", "dvdwind dvdwind", "2 30", "new"),
    ("constrict.constrict", "constrict constrict", "50", "new"),
    ("gate.gate.1", "gate gate 1", "-24", "new"),
    ("gate.gate.2", "gate gate 2", "-24", "new"),
    ("silend.silend.1", "silend silend 1", "0.25", "new"),
    ("clip.clip.1", "clip clip 1", "0.1", "new"),
    ("clip.clip.2", "clip clip 2", "0.5", "new"),
    ("quirk.quirk.1", "quirk quirk 1", "2", "new"),
    ("quirk.quirk.2", "quirk quirk 2", "2", "new"),
    ("envspeak.envspeak.1", "envspeak envspeak 1", "20 5 0 2 0", "new"),
    ("envspeak.envspeak.2", "envspeak envspeak 2", "20 5 0", "new"),
    ("envspeak.envspeak.5", "envspeak envspeak 5", "20 5 0 3 0", "new"),
    ("envspeak.envspeak.6", "envspeak envspeak 6", "20 5 0 3 0", "new"),
    ("newdelay.newdelay", "newdelay newdelay", "48 0.4 0.5", "new"),
    ("tremenv.tremenv", "tremenv tremenv", "8 0.6 20 2", "new"),
    ("phase.phase.2", "phase phase 2", "-t0.5", "new_stereo_only"),
    ("modify.loudness.3", "modify loudness 3", "-l0.8", "raw_mono"),
    ("modify.loudness.4", "modify loudness 4", "-l0.5", "raw_mono"),
    ("modify.revecho.1", "modify revecho 1", "180 0.4 0.35 0.2 -p0.5", "raw_mono"),
    ("filter.variable.2", "filter variable 2", "0.5 0.5 1000 -t0.25", "raw_mono"),
    ("filter.sweeping.2", "filter sweeping 2", "0.5 0.5 200 3000 0.5 -t0.25 -p0", "raw_mono"),
    ("filter.phasing.1", "filter phasing 1", "0.6 3 -t0.25", "raw_mono"),
    ("filter.phasing.2", "filter phasing 2", "0.6 3 -t0.25", "raw_mono"),
    ("sfedit.cut.1", "sfedit cut 1", "0.1 0.4 -w5", "raw_mono"),
    ("sfedit.cutend.1", "sfedit cutend 1", "0.3 -w5", "raw_mono"),
    ("sfedit.excise.1", "sfedit excise 1", "0.1 0.4 -w5", "raw_mono"),
    ("extend.doublets", "extend doublets", "0.1 2", "raw_mono"),
    ("extend.loop.1", "extend loop 1", "0.1 100 25 -w5", "raw_mono"),
    ("extend.loop.2", "extend loop 2", "2 0.1 100 -l0 -w5", "raw_mono"),
    ("extend.loop.3", "extend loop 3", "4 0.1 100 -l0 -w5", "raw_mono"),
    ("extend.scramble.1", "extend scramble 1", "0.1 0.3 2 -w5 -s17", "raw_mono"),
    ("envel.dovetail.1", "envel dovetail 1", "0.1 0.2 0 0", "raw_mono"),
    ("envel.dovetail.2", "envel dovetail 2", "0.1 0.2", "raw_mono"),
    ("envel.swell", "envel swell", "1 0", "raw_mono"),
    ("envel.tremolo.1", "envel tremolo 1", "4 0.6 1", "raw_mono"),
    ("envel.warp.2", "envel warp 2", "20", "raw_mono"),
]

SPECTRAL = [
    ("spec.magnify", "spec magnify", "0.5 2", "new"),
    ("spec.gate", "spec gate", "0.01", "new"),
    ("blur.drunk", "blur drunk", "8 0.5 3 -z", "new"),
    ("blur.scatter", "blur scatter", "32 -n", "new"),
    ("caltrain.caltrain", "caltrain caltrain", "0.2 1000", "new"),
    ("superaccu.superaccu.2", "superaccu superaccu 2", "-d0.1", "new"),
    ("spectstr.stretch", "spectstr stretch", "2 0.2 0.1", "new"),
    ("specfold.specfold.1", "specfold specfold 1", "20 256 2", "new"),
    ("specfold.specfold.2", "specfold specfold 2", "20 256", "new"),
    ("specfold.specfold.3", "specfold specfold 3", "20 256 17", "new"),
    ("specfnu.specfnu.1", "specfnu specfnu 1", "4 -g0.5", "new"),
    ("specfnu.specfnu.3", "specfnu specfnu 3", "0 -g0.5", "new"),
    ("specfnu.specfnu.4", "specfnu specfnu 4", "0.1 -g0.5", "new"),
]


def execute(command, directory):
    start = time.monotonic()
    try:
        p = subprocess.run(command, cwd=directory, capture_output=True, text=True,
                           errors="replace", timeout=12)
        return p.returncode, p.stdout + p.stderr, time.monotonic() - start
    except subprocess.TimeoutExpired:
        return -999, "12 second timeout", time.monotonic() - start


def fixtures(rate):
    t = np.arange(rate * 2) / rate
    envelope = np.maximum(0, np.sin(2 * np.pi * 3 * t)) ** 2
    left = envelope * (.20 * np.sin(2 * np.pi * 220 * t) +
                       .07 * np.sin(2 * np.pi * 660 * t) +
                       .03 * np.sin(2 * np.pi * 1760 * t))
    right = envelope * (.08 * np.sin(2 * np.pi * 347 * t + .3))
    return {"mono": left[:, None], "stereo": np.column_stack((left, right)),
            "linked": np.column_stack((left, -.25 * left)),
            "silent_right": np.column_stack((left, np.zeros_like(left)))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cdp-bin", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--rates", type=int, nargs="+", default=[44100, 48000])
    parser.add_argument("--only", default="", help="ID substring for a targeted rerun")
    args = parser.parse_args()
    root = args.out_dir.resolve(); root.mkdir(parents=True, exist_ok=True)
    binary = args.cdp_bin.resolve()
    results = []
    for rate in args.rates:
        inputs = fixtures(rate)
        for spectral, cases in [(False, CASES), (True, SPECTRAL)]:
            for ident, prefix, params, coverage in cases:
                if args.only and args.only not in ident:
                    continue
                for kind, samples in inputs.items():
                    if spectral and kind != "mono":
                        continue
                    job = root / str(rate) / ident / kind
                    job.mkdir(parents=True, exist_ok=True)
                    wavfile.write(job / "input.wav", rate, samples.astype(np.float32))
                    before = hashlib.sha256((job / "input.wav").read_bytes()).hexdigest()
                    output = job / "output.wav"
                    output.unlink(missing_ok=True)
                    words = prefix.split()
                    cmd = [str(binary / words[0]), *words[1:],
                           "input.ana" if spectral else "input.wav",
                           "output.ana" if spectral else "-foutput.wav", *params.split()]
                    commands = []
                    if spectral:
                        (job / "input.ana").unlink(missing_ok=True)
                        (job / "output.ana").unlink(missing_ok=True)
                        commands.append([str(binary / "pvoc"), "anal", "1", "input.wav",
                                         "input.ana", "-c1024", "-o3"])
                    commands.append(cmd)
                    if spectral:
                        commands.append([str(binary / "pvoc"), "synth", "output.ana", "-foutput.wav"])
                    row = dict(id=ident, coverage=coverage, rate=rate, fixture=kind,
                               commands=commands, spectral=spectral)
                    logs = []; elapsed = 0
                    for command in commands:
                        code, log, seconds = execute(command, job)
                        logs.append(log); elapsed += seconds
                        if code:
                            break
                    row.update(returncode=code, seconds=round(elapsed, 4))
                    row["source_unchanged"] = before == hashlib.sha256((job / "input.wav").read_bytes()).hexdigest()
                    (job / "run.log").write_text("\n".join(logs))
                    try:
                        with warnings.catch_warnings():
                            warnings.simplefilter("ignore", wavfile.WavFileWarning)
                            output_rate, data = wavfile.read(output)
                        if np.issubdtype(data.dtype, np.integer):
                            data = data.astype(np.float64) / (np.iinfo(data.dtype).max + 1.0)
                        else:
                            data = data.astype(np.float64)
                        if data.ndim == 1:
                            data = data[:, None]
                        row.update(frames=len(data), channels=data.shape[1], output_rate=output_rate,
                                   finite=bool(np.isfinite(data).all()),
                                   peak=float(np.max(np.abs(data))) if data.size else 0)
                        row["channel_count_matches"] = data.shape[1] == samples.shape[1]
                        row["non_silent"] = row["peak"] > 1e-7
                        # This only establishes a bounded, readable render. It does not
                        # certify channel linking, musical behavior or Portal readiness.
                        row["valid"] = (code == 0 and len(data) > 0 and row["finite"] and
                                        output_rate == rate and len(data) <= rate * 30 and
                                        row["source_unchanged"] and row["channel_count_matches"]
                                        and row["non_silent"])
                        if kind == "linked" and data.shape[1] == 2:
                            row["linked_error"] = float(np.max(np.abs(data[:, 1] + .25 * data[:, 0])))
                        if kind == "silent_right" and data.shape[1] == 2:
                            row["silent_right_peak"] = float(np.max(np.abs(data[:, 1])))
                        row["sha256"] = hashlib.sha256(output.read_bytes()).hexdigest()
                    except (RuntimeError, ValueError, OSError) as exc:
                        row.update(valid=False, error=str(exc))
                    if not row["valid"]:
                        row["diagnostic"] = "\n".join(logs)[-1500:]
                    results.append(row)
                print(f"{rate} {ident}: " + ", ".join(
                    f"{r['fixture']}={'ok' if r['valid'] else 'FAIL'}" for r in results
                    if r['id'] == ident and r['rate'] == rate), flush=True)
                (root / "results.json").write_text(json.dumps(results, indent=2))
    print(f"{len(results)} probes; {sum(r['valid'] for r in results)} valid WAV outputs")


if __name__ == "__main__":
    main()
