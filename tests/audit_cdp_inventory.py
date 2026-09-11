#!/usr/bin/env python3
"""Inventory CDP CMake executable declarations; this does not evaluate CMake.

Includes platform-conditional declarations and utilities. Targets are not a
count of distinct DSP processes or modes, nor a claim that every target builds.
"""
import argparse
import ast
import json
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cdp-source", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    source = args.cdp_source.resolve()
    repo = Path(__file__).resolve().parents[1]
    manifest = (repo / "cmake/CDP8Manifest.cmake").read_text()
    required = set(re.search(r"set\(TAPESISTER_CDP8_REQUIRED_PROGRAMS\s+([^)]*)", manifest)[1].split())
    probe_cases = []
    for node in ast.parse((Path(__file__).parent / "audit_cdp_candidates.py").read_text()).body:
        if isinstance(node, ast.Assign) and any(
                isinstance(t, ast.Name) and t.id in ("CASES", "SPECTRAL") for t in node.targets):
            probe_cases.extend(ast.literal_eval(node.value))
    programs = {}
    for cmake in sorted(source.rglob("CMakeLists.txt")):
        text = re.sub(r"#[^\n]*", "", cmake.read_text())
        for match in re.finditer(r"add_executable\s*\(([^)]*)\)", text, re.I):
            tokens = match[1].split()
            if not tokens:
                continue
            name, *sources = tokens
            row = programs.setdefault(name, dict(program=name, bundled=name in required,
                                                declarations=[], probed_modes=[]))
            row["declarations"].append(dict(
                cmake=str(cmake.relative_to(source)),
                source_arguments=sources))
    for ident, prefix, _, _ in probe_cases:
        programs[prefix.split()[0]]["probed_modes"].append(ident)
    data = dict(schema=1,
                scope="Uncommented CMake executable declarations, including platform alternatives and utilities; not mode or build counts",
                unique_targets=len(programs),
                declarations=sum(len(x["declarations"]) for x in programs.values()),
                bundled_targets=len(required),
                programs=[programs[name] for name in sorted(programs)])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(data, indent=2) + "\n")
    print(data["unique_targets"], "unique targets;", data["declarations"], "declarations")


if __name__ == "__main__":
    main()
