#!/usr/bin/env python3
"""Launch the real Linux executable with a normal desktop stack budget."""

import argparse
import os
from pathlib import Path
import re
import resource
import subprocess
import tempfile


# Ubuntu 22.04's supported runtime, including its updated GCC 12 runtime.
# Check every shipped program/decoder, not just the application: CDP and video
# would otherwise fail later even when the first window successfully opened.
LINUX_ABI_LIMITS = {"GLIBC": (2, 35), "GLIBCXX": (3, 4, 30), "CXXABI": (1, 3, 13)}


def check_package_abi(executable: Path) -> None:
    root = executable.parent
    candidates = [executable, *root.glob("cdp/bin/*"), *root.glob("video/lib/*")]
    checked = set()
    failures = []
    for candidate in candidates:
        if not candidate.is_file():
            continue
        path = candidate.resolve(strict=True)
        if path in checked:
            continue
        with path.open("rb") as file:
            if file.read(4) != b"\x7fELF":
                continue
        checked.add(path)
        result = subprocess.run(
            ["readelf", "--version-info", "--wide", str(path)],
            capture_output=True, text=True, check=True,
        )
        requirements = {}
        for namespace, number in re.findall(
            r"\bName: (GLIBC|GLIBCXX|CXXABI)_([0-9]+(?:\.[0-9]+)*)\b", result.stdout
        ):
            version = tuple(int(part) for part in number.split("."))
            requirements[namespace] = max(version, requirements.get(namespace, ()))
        for namespace, version in requirements.items():
            limit = LINUX_ABI_LIMITS[namespace]
            if version > limit:
                required = ".".join(map(str, version))
                supported = ".".join(map(str, limit))
                failures.append(
                    f"{candidate.relative_to(root)} requires {namespace}_{required}; "
                    f"supported maximum is {namespace}_{supported}"
                )
    if not checked:
        raise SystemExit("Linux package contains no ELF executables to check")
    if failures:
        raise SystemExit("Linux package runtime is too new:\n" + "\n".join(failures))
    print(f"Linux package ABI passed for {len(checked)} ELF files "
          "(GLIBC <= 2.35, GLIBCXX <= 3.4.30, CXXABI <= 1.3.13).")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--stack-kib", type=int, default=8192)
    parser.add_argument("--check-abi", action="store_true",
                        help="check the package against the Ubuntu 22.04 runtime baseline")
    parser.add_argument("--abi-only", action="store_true",
                        help="check the packaged runtime without launching")
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    if args.stack_kib <= 0:
        parser.error("--stack-kib must be positive")
    if args.check_abi or args.abi_only:
        check_package_abi(executable)
    if args.abi_only:
        return

    def limit_child() -> None:
        # A runner with an unlimited stack must still reproduce desktop limits.
        budget = args.stack_kib * 1024
        resource.setrlimit(resource.RLIMIT_STACK, (budget, budget))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

    with tempfile.TemporaryDirectory(prefix="tapesister-startup-") as directory:
        root = Path(directory)
        config = root / "tapesister.ini"
        config.write_text(
            f"[Paths]\nExchangePath={root / 'exchange'}\n"
            "[MIDI]\nmidi_input_device=OFF\n",
            encoding="utf-8",
        )
        environment = os.environ.copy()
        environment.update(
            SDL_VIDEODRIVER="dummy",
            SDL_AUDIODRIVER="dummy",
            SDL_RENDER_DRIVER="software",
            TAPESISTER_CONFIG=str(config),
            TAPESISTER_PALETTE=str(root / "palette.pal"),
            TAPESISTER_CAPTURES=str(root / "captures"),
            TAPESISTER_DIAGNOSTIC_LOG=str(root / "tapesister-diagnostic.log"),
        )
        # This existing mode runs initialization, rendering, audio callbacks,
        # the event loop and normal cleanup, then exits without a user click.
        result = subprocess.run(
            [str(executable), "--diagnostic-performance", "2", "idle"],
            cwd=root,
            env=environment,
            preexec_fn=limit_child,
            capture_output=True,
            text=True,
            timeout=45,
        )
        log_path = root / "tapesister-diagnostic.log"
        log = log_path.read_text(encoding="utf-8") if log_path.exists() else ""
        required = (
            "renderer=software",
            "active SDL backend=dummy",
            "output state=active",
            "shutdown complete diagnostic_failed=0",
        )
        missing = [marker for marker in required if marker not in log]
        if result.returncode or missing or "Performance profile: ON" not in result.stdout:
            raise SystemExit(
                f"Linux startup failed with {args.stack_kib} KiB stack: "
                f"exit={result.returncode}, missing={missing}\n"
                f"{result.stdout}\n{result.stderr}\n{log}"
            )
        print(f"Linux executable initialized, rendered, ran audio and shut down "
              f"with a {args.stack_kib} KiB stack.")


if __name__ == "__main__":
    main()
