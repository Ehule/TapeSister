"""Paired native render timings and exact PCM comparison; no audio device needed.

The reference executable links the pristine selected handler from the pinned SDK.
All other engine/host code and compiler flags must match the candidate build.
Timings are observations, never CI speed thresholds. PCM equality is required.
"""
import argparse
import json
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--kernel", choices=["delay", "echo"], default="delay")
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
suffix = ".exe" if platform.system() == "Windows" else ""
reference = "tapesister_sunvox_echo_reference" if args.kernel == "echo" else "tapesister_sunvox_patch_reference"
before = args.build.resolve() / (reference + suffix)
after = args.build.resolve() / ("tapesister_sunvox_patch_benchmark" + suffix)
velvet = "echo2 Velvet Reverb 3"
cases = []
for patch in [velvet, "RAGINGCOAST Ensemble", "cube48 ShimmerClassic"]:
    for mode in ["defaults", "audit"]:
        for quantum in [64, 256, 1024]:
            primary = patch == velvet and quantum == 256
            cases.append((patch, 48000, quantum, 60 if primary else 15, mode, 3 if primary else 1))
for rate in [8000, 44100, 96000, 192000]:
    cases.append((velvet, rate, 256, 15, "defaults", 1))

rows = []
with tempfile.TemporaryDirectory(prefix="sunvox-exact-") as directory:
    pcm = {name: Path(directory) / (name + ".f32") for name in ["reference", "optimized"]}
    for patch, rate, quantum, seconds, mode, trials in cases:
        for trial in range(trials):
            results = {}
            order = [("reference", before), ("optimized", after)]
            if trial % 2:
                order.reverse()
            for name, binary in order:
                run = subprocess.run([str(binary), patch, str(rate), str(quantum), str(seconds), mode,
                    str(pcm[name])], check=True, text=True, capture_output=True)
                values = dict(field.split("=", 1) for field in run.stdout.strip().split())
                if values.get("fixed_seed") != "1":
                    raise SystemExit("Exact random-patch comparison requires the GNU/Clang Linux or Windows probes")
                results[name] = {"cpu_percent": float(values["cpu_percent"]),
                    "rms": float(values["rms"]), "fnv64": values["fnv64"]}
            if pcm["reference"].read_bytes() != pcm["optimized"].read_bytes():
                raise SystemExit(f"PCM mismatch: {patch}, {mode}, {rate} Hz, {quantum} frames")
            row = dict(patch=patch, rate=rate, quantum=quantum, audio_seconds=seconds, controls=mode,
                trial=trial+1, exact_pcm=True, **results)
            rows.append(row)
            print(json.dumps(row), flush=True)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps({"platform": platform.platform(), "rows": rows}, indent=2)+"\n", encoding="utf-8")

for mode in ["defaults", "audit"]:
    selected = [r for r in rows if r["patch"] == velvet and r["rate"] == 48000
                and r["quantum"] == 256 and r["controls"] == mode]
    a = statistics.median(r["reference"]["cpu_percent"] for r in selected)
    b = statistics.median(r["optimized"]["cpu_percent"] for r in selected)
    print(f"Velvet {mode}: median CPU {a:.4f}% -> {b:.4f}% ({(1-b/a)*100:.2f}% less); exact PCM")
