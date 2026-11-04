#!/usr/bin/env python3
"""Run callback binaries serially and reject changed output hashes."""
import argparse
import json
import statistics
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("baseline", type=Path)
p.add_argument("candidate", type=Path)
p.add_argument("project", type=Path)
p.add_argument("--seconds", type=float, default=120)
p.add_argument("--warmup", type=float, default=30)
p.add_argument("--trials", type=int, default=3)
p.add_argument("--rate", type=int, default=48000)
p.add_argument("--block", type=int, default=512)
p.add_argument("--channels", type=int, default=2)
p.add_argument("--output", type=Path, required=True)
args = p.parse_args()
if args.trials < 1:
    p.error("--trials must be positive")
results = []
binaries = [("baseline", args.baseline.resolve()), ("candidate", args.candidate.resolve())]
for trial in range(args.trials):
    for label, binary in binaries[::1 if trial % 2 == 0 else -1]:
        command = [str(binary), str(args.project.resolve()), str(args.seconds),
                   str(args.warmup), str(args.rate), str(args.block), str(args.channels)]
        run = subprocess.run(command, text=True, capture_output=True)
        if run.returncode:
            raise SystemExit(f"{label} exited {run.returncode}: {run.stderr}")
        data = json.loads(next(line for line in run.stdout.splitlines() if line.startswith("{")))
        data.update(build=label, trial=trial + 1)
        results.append(data)
        args.output.write_text(json.dumps({"runs": results}, indent=2) + "\n")
        print(json.dumps(data), flush=True)
keys = ("stereo_hash", "hardware_hash")
identical = all(len({r[key] for r in results}) == 1 for key in keys)
medians = {label: statistics.median(r["thread_cpu_percent"] for r in results if r["build"] == label)
           for label, _ in binaries}
summary = {"runs": results, "output_hashes_identical": identical,
           "median_thread_cpu_percent": medians,
           "relative_cpu_reduction_percent": (1 - medians["candidate"] / medians["baseline"]) * 100}
args.output.write_text(json.dumps(summary, indent=2) + "\n")
print(json.dumps({k: v for k, v in summary.items() if k != "runs"}), flush=True)
if not identical:
    raise SystemExit("Output changed; inspect the audio before accepting this optimization.")
