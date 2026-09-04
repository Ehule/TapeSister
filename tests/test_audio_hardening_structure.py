#!/usr/bin/env python3
"""Ordering guards for the SDL integration boundary.

Behavioral policy and state transitions live in test_audio_lifecycle.c. These
checks cover guarantees that depend on translation-unit ordering and cannot be
exercised without initializing a platform SDL audio driver.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src/main_sdl.c").read_text(encoding="utf-8")
PART3 = (ROOT / "src/main_sdl_audio_part3.inc").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")


backend_call = MAIN.index("ts_audio_apply_backend_before_sdl(ui.config.audio_backend)")
sdl_init = MAIN.index("SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)")
require(backend_call < sdl_init,
        "configured backend must be applied before SDL_Init")
require("if (!ts_audio_apply_backend_before_sdl" in MAIN,
        "backend selection failure must stop startup")
require("if (!ts_audio_log_active_backend" in MAIN,
        "an explicitly requested backend must not silently activate another driver")
require("disableWasapi" not in MAIN and "disable_wasapi" not in MAIN.lower(),
        "legacy WASAPI-disable path must not return")
require("TS_INPUT_CONSUMER_ACTIVITY, 1" not in MAIN,
        "idle activity UI must not hold capture open unconditionally")

open_start = PART3.index("static SDL_AudioDeviceID ts_audio_open_device")
open_end = PART3.index("static int ts_audio_open_approved_default_output")
startup_open = PART3[open_start:open_end]
require("configured_override" not in startup_open,
        "startup output wrapper must not retain automatic fallback logic")
require("ts_audio_endpoint_open_failed" in startup_open,
        "selected output failure must become an explicit lifecycle state")

main_loop = MAIN.index("while (running) {")
poll = MAIN.index("while (SDL_PollEvent(&event))", main_loop)
device_removed = MAIN.index("event.type == SDL_AUDIODEVICEREMOVED", poll)
global_keys = MAIN.index("event.type == SDL_KEYDOWN", device_removed)
require(device_removed < global_keys,
        "device lifecycle events must precede UI/modal key short circuits")
require("ts_audio_device_event_matches" in MAIN[device_removed:global_keys],
        "removal events must be matched through real SDL identity")
require("event.adevice.which == input_device" not in MAIN,
        "real SDL capture IDs must not be compared with logical handles")

print("audio hardening structural guards passed")
