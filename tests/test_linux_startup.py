#!/usr/bin/env python3
"""Launch the real Linux executable with a normal desktop stack budget."""

import argparse
import os
from pathlib import Path
import resource
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--stack-kib", type=int, default=8192)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    if args.stack_kib <= 0:
        parser.error("--stack-kib must be positive")

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
