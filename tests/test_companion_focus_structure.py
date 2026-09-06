#!/usr/bin/env python3
"""Guards for state-preserving Tapehead/TapeSister window switching."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src" / "main_sdl.c").read_text()
CMAKE = (ROOT / "CMakeLists.txt").read_text()
MAKEFILE = (ROOT / "Makefile").read_text()

assert '#include "tape_companion.h"' in MAIN
assert "TAPE_COMPANION_TAPESISTER_NAME" in MAIN
assert "TAPE_COMPANION_TAPEHEAD_NAME" in MAIN
assert "tapeCompanionPump(&companion_focus)" in MAIN
assert "tapeCompanionClose(&companion_focus)" in MAIN
assert "event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED" in MAIN

chord = "global_key == SDLK_TAB"
request = "TAPE_COMPANION_TAPEHEAD_NAME"
assert chord in MAIN and request in MAIN
assert "!event.key.repeat && global_key == SDLK_TAB" in MAIN
assert MAIN.index(chord) < MAIN.index("ts_ui_midi_learn_chord(")
assert "KMOD_SHIFT | KMOD_ALT | KMOD_GUI" in MAIN[MAIN.index(chord):
                                                     MAIN.index(request)]

assert "src/tape_companion.c" in CMAKE
# Source ordering is not significant; Live Link sits between these files.
sdl_sources = next(line for line in MAKEFILE.splitlines()
                   if line.startswith("SDL_MAIN =")).split("=", 1)[1].split()
assert "src/main_sdl.c" in sdl_sources
assert "src/tape_companion.c" in sdl_sources

print("Companion focus SDL integration guards passed")
