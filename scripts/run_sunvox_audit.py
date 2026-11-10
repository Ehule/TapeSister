"""Compare the embedded DSP and the official SDK under matched input/settings.

An optional second build uses upstream DSP math/SIMD flags, confined to upstream
engine/math files. The TapeSister boundary and probe retain finite-value checks.
This is an experiment, not a change to application release flags.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument("--work", type=Path, required=True)
parser.add_argument("--sdk-zip", type=Path)
parser.add_argument("--seconds", type=int, default=120)
parser.add_argument("--upstream-flags", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
archive = args.sdk_zip.resolve() if args.sdk_zip else work / "sunvox_lib-2.1.4d.zip"
if not archive.exists():
    urllib.request.urlretrieve("https://warmplace.ru/soft/sunvox/sunvox_lib-2.1.4d.zip", archive)
assert hashlib.sha256(archive.read_bytes()).hexdigest() == "abe851de9d65a10e06673bf33257154591a2ff63ad2bb298488a5941cc5f4057"
windows = sys.platform == "win32"
library = "windows/lib_x86_64/sunvox.dll" if windows else "linux/lib_x86_64/sunvox.so"
with zipfile.ZipFile(archive) as z:
    for relative in ["headers/sunvox.h", library]:
        target = work / "sdk" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(z.read("sunvox_lib/sunvox_lib/" + relative))
build = work / "build"
override = work / "upstream-flags.cmake"
override.write_text('''file(GLOB audit_dsp "${CMAKE_BINARY_DIR}/sunvox/lib_sunvox/*.cpp"
  "${CMAKE_BINARY_DIR}/sunvox/lib_sunvox/psynth/*.cpp"
  "${CMAKE_BINARY_DIR}/sunvox/lib_sunvox/midi_file/*.cpp"
  "${CMAKE_BINARY_DIR}/sunvox/lib_sunvox/xm/*.cpp"
  "${CMAKE_BINARY_DIR}/sunvox/lib_dsp/*.cpp")
set_source_files_properties(${audit_dsp} PROPERTIES COMPILE_OPTIONS "-ffast-math;-mssse3")
''')

def run(command, **kw):
    print("RUN", " ".join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), check=True, **kw)

rows = []
for variant in ["current"] + (["upstream-dsp-flags"] if args.upstream_flags else []):
    run(["cmake", "-S", root, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         "-DTAPESISTER_BUILD_SDL=OFF", "-DTAPESISTER_BUNDLE_CDP8=OFF", "-DTAPESISTER_ENABLE_MIDI=OFF",
         "-DCMAKE_PROJECT_INCLUDE=" + (str(override).replace("\\", "/") if variant != "current" else "")])
    run(["cmake", "--build", build, "--parallel", "2", "--target", "tapesister_sunvox"])
    if variant == "current":
        # Exact Delay comparison must use production math flags, before the
        # optional upstream-flags experiment mutates this build directory.
        run(["cmake", "--build", build, "--parallel", "2", "--target",
             "tapesister_sunvox_patch_benchmark", "tapesister_sunvox_patch_reference",
             "tapesister_sunvox_delay_tests"])
        run(["ctest", "--test-dir", build, "--output-on-failure", "-R", "^sunvox_delay_exact$"])
        run([sys.executable, root / "scripts/compare_sunvox_delay.py", "--build", build,
             "--output", work / "delay-results.json"])
    obj = work / "audit.o"
    binary = work / ("audit.exe" if windows else "audit")
    run([shutil.which("gcc") or "cc", "-O3", "-std=c11", "-I", root / "include", "-I", work / "sdk/headers",
         "-c", root / "tools/benchmark_sunvox_native.c", "-o", obj])
    libs = ["-lwinmm", "-lws2_32"] if windows else ["-ldl"]
    run([shutil.which("g++") or "c++", obj, build / "libtapesister_sunvox.a", "-lm", "-lpthread", *libs, "-o", binary])
    for quantum in [64, 256, 1024]:
        result = run([binary, work / "sdk" / library, build / "sunvox/effects", quantum, args.seconds], capture_output=True, text=True)
        print(result.stdout, end="", flush=True)
        for line in result.stdout.splitlines():
            if line.startswith("{"):
                row = json.loads(line)
                row.update(platform=sys.platform, variant=variant)
                rows.append(row)
        (work / "results.json").write_text(json.dumps(rows, indent=2) + "\n")
print("Results:", work / "results.json")
