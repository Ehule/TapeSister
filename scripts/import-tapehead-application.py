#!/usr/bin/env python3
"""Reproduce the embedded application without changing the TapeHead checkout."""
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "third_party/tapehead/application"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("upstream", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    upstream = args.upstream.resolve()
    manifest = json.loads((DEST / "SOURCES.json").read_text())
    source_files = set(manifest["sha256"])
    actual_files = {p.relative_to(upstream).as_posix()
                    for p in (upstream / "src").rglob("*") if p.is_file()}
    actual_files.add("LICENSE")
    if actual_files != source_files:
        raise SystemExit("Upstream source file inventory differs from the pinned manifest")
    for name, expected in manifest["sha256"].items():
        if hashlib.sha256((upstream / name).read_bytes()).hexdigest() != expected:
            raise SystemExit("Upstream checksum mismatch: " + name)
    with tempfile.TemporaryDirectory(prefix="tapesister-tapehead-") as temporary:
        staged = Path(temporary)
        for name in sorted(source_files):
            target = staged / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(upstream / name, target)
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i",
                        str(DEST / "embedded.patch")], cwd=staged, check=True,
                       stdout=subprocess.DEVNULL)
        if args.check:
            committed_files = {p.relative_to(DEST).as_posix()
                               for p in (DEST / "src").rglob("*") if p.is_file()}
            committed_files.add("LICENSE")
            if committed_files != source_files:
                raise SystemExit("Committed source file inventory differs from the manifest")
            for name in sorted(source_files):
                if (staged / name).read_bytes() != (DEST / name).read_bytes():
                    raise SystemExit("Embedded source differs from reproducible import: " + name)
        else:
            for name in sorted(source_files):
                target = DEST / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(staged / name, target)
    print(f"{'Verified' if args.check else 'Imported'} {len(source_files)} files from "
          f"TapeHead {manifest['commit']} with the embedded host patch")


if __name__ == "__main__":
    main()
