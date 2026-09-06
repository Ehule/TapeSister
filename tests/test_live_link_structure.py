#!/usr/bin/env python3
"""SDL integration guards for the Tapehead Live Link controls."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src/main_sdl.c").read_text(encoding="utf-8")
UI = (ROOT / "src/ts_ui.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")


require("case TS_SISTER_UI_ACTION_TAPEHEAD_SONG:" in MAIN,
        "the dedicated Tapehead song action must be handled")
require("case TS_SISTER_UI_ACTION_TAPEHEAD_PATTERN:" in MAIN,
        "the dedicated Tapehead pattern action must be handled")
require("tapeLinkReaderTransportState" in MAIN,
        "the UI must read producer-confirmed transport state")
require(MAIN.count("TAPE_LINK_COMMAND_TOGGLE_SONG") == 2 and
        MAIN.count("TAPE_LINK_COMMAND_TOGGLE_PATTERN") == 1,
        "transport commands must only be issued by the dedicated action handler")
require('"TH SRC"' in UI and '"TH SONG"' in UI and '"TH PATT"' in UI,
        "source and transport controls must render as separate buttons")
require("model->tapehead_song_playing" in UI and
        "model->tapehead_pattern_playing" in UI,
        "transport buttons must highlight from actual producer state")

print("Live Link SDL integration guards passed")
