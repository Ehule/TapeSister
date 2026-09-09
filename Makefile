.DEFAULT_GOAL := all
CC ?= cc
CXX ?= c++
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CXXFLAGS ?= -std=c++11 -O2
MINIAUDIO_DIR ?= .deps/miniaudio
MINIAUDIO_COMMIT = 350784a9467a79d0fa65802132668e5afbcf3777
CPPFLAGS ?= -Iinclude -Ithird_party -I$(MINIAUDIO_DIR)
CORE_SOURCES = src/ts_sample.c src/ts_fm.c src/ts_audition.c src/ts_note_bank.c src/ts_note_event.c src/ts_performance.c src/ts_audio_mixer.c src/ts_audio_lifecycle.c src/ts_realtime_diagnostics.c src/ts_sister_effects.c src/ts_sister_fallout.c src/ts_sister_post_fx.c src/ts_sister_limiter.c src/ts_sister_machine.c src/ts_sister_wave_snapshot.c src/ts_sister_runtime.c src/ts_sister_ui.c src/ts_sister_preset.c src/ts_sister_project_state.c src/ts_capture.c src/ts_capture_archive.c src/ts_performance_recorder.c src/ts_input_monitor.c src/ts_input_ownership.c src/ts_sample_pages.c src/ts_browser.c src/ts_config.c src/ts_audio_config.c src/ts_recipe.c src/ts_dsp_recipe.c src/ts_palette.c src/ts_cdp_recipe.c src/ts_cdp_adapter.c src/ts_transform.c src/ts_dsp_transform.c src/ts_exchange.c src/ts_render_damage.c src/ts_waveform_cache.c src/ts_waveform_display.c src/ts_ui.c src/ts_midi_map.c
CORE_SOURCES += src/ts_cdp_portal.c
DECODER_OBJ = .deps/ts_audio_import.o
CORE = $(CORE_SOURCES) $(DECODER_OBJ)
tapesister tapesister_core_tests tapesister_portal_tests tapesister_portal_controller_tests tapesister_keyboard_sustain_tests: src/ts_cdp_portal_instruments.inc
SDL_MAIN = src/main_sdl.c src/tape_link.c src/tape_companion.c
DIAG = src/ts_startup_diag.c
MIDI_C = src/ts_midi_input.c
MIDI_CPP_OBJS =
MIDI_CPPFLAGS =
MIDI_LDFLAGS =
TAPESISTER_LDFLAGS =
LIVE_LINK_LDFLAGS = $(if $(filter Linux,$(shell uname -s 2>/dev/null)),-lrt,)
ifeq ($(OS),Windows_NT)
TAPESISTER_LDFLAGS += -Wl,--stack,16777216
MIDI_CPP_OBJS = third_party/rtmidi/RtMidi.o third_party/rtmidi/rtmidi_c.o
MIDI_CPPFLAGS = -DTAPESISTER_HAS_MIDI -D__WINDOWS_MM__
MIDI_LDFLAGS = -lstdc++ -lwinmm
else
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
MIDI_CPP_OBJS = third_party/rtmidi/RtMidi.o third_party/rtmidi/rtmidi_c.o
MIDI_CPPFLAGS = -DTAPESISTER_HAS_MIDI -D__MACOSX_CORE__
MIDI_LDFLAGS = -lc++ -framework CoreMIDI -framework CoreFoundation
else ifeq ($(shell command -v pkg-config >/dev/null 2>&1 && pkg-config --exists alsa && echo yes),yes)
MIDI_CPP_OBJS = third_party/rtmidi/RtMidi.o third_party/rtmidi/rtmidi_c.o
MIDI_CPPFLAGS = -DTAPESISTER_HAS_MIDI -D__LINUX_ALSA__
MIDI_LDFLAGS = -lstdc++ $(shell pkg-config --libs alsa) -ldl -pthread
endif
endif

.PHONY: all bundled-release dependencies test test_audio_hardening_structure test_live_link_structure test_companion_focus_structure test_identity_packaging stress-sister benchmark-sister screenshot screenshot-sister-spirit runtime-assets clean

dependencies: $(MINIAUDIO_DIR)/miniaudio.h

$(MINIAUDIO_DIR)/miniaudio.h:
	@mkdir -p .deps
	@git clone --quiet --filter=blob:none https://github.com/mackron/miniaudio.git $(MINIAUDIO_DIR)
	@git -C $(MINIAUDIO_DIR) checkout --quiet $(MINIAUDIO_COMMIT)

$(DECODER_OBJ): src/ts_audio_import.c include/tapesister/audio_import.h $(MINIAUDIO_DIR)/miniaudio.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c src/ts_audio_import.c -o $@

all: bundled-release

bundled-release:
	+@TAPESISTER_BUILD_JOBS="$${TAPESISTER_BUILD_JOBS:-2}" bash ./build.sh

tapesister: $(CORE) $(SDL_MAIN) $(DIAG) $(MIDI_C) $(MIDI_CPP_OBJS) src/main_sdl_portal.inc src/ts_cdp_portal_ui.inc src/ts_cdp_portal_factory.inc include/tapesister/cdp_portal.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MIDI_CPPFLAGS) $(shell sdl2-config --cflags) $(CORE) $(SDL_MAIN) $(DIAG) $(MIDI_C) $(MIDI_CPP_OBJS) -o $@ $(shell sdl2-config --libs) -lm $(TAPESISTER_LDFLAGS) $(MIDI_LDFLAGS)

third_party/rtmidi/%.o: third_party/rtmidi/%.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(MIDI_CPPFLAGS) -c $< -o $@

runtime-assets:
	@test ! -f assets/tapesister_welcome.wav || test -s assets/tapesister_welcome.wav

tapesister_core_tests: $(CORE) tests/test_core.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_render_demo: $(CORE) tests/render_demo.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_sister_spirit_demo: $(CORE) tests/render_sister_spirit.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

screenshot-sister-spirit: tapesister_sister_spirit_demo tapesister_render_demo
	@mkdir -p artifacts
	./tapesister_render_demo artifacts/sister-portal-normal.ppm portal
	./tapesister_render_demo artifacts/sister-portal-hover.ppm portal-hover
	./tapesister_render_demo artifacts/sister-portal-pressed.ppm portal-pressed
	./tapesister_render_demo artifacts/project-save-confirm.ppm save-confirm
	./tapesister_render_demo artifacts/project-saving.ppm saving
	./tapesister_sister_spirit_demo off artifacts/sister-spirit-off.ppm
	./tapesister_sister_spirit_demo flash artifacts/sister-spirit-flash.ppm
	./tapesister_sister_spirit_demo capture artifacts/sister-capture-progress.ppm
	./tapesister_sister_spirit_demo capture-effects artifacts/sister-effects-capture-progress.ppm
	./tapesister_sister_spirit_demo capture-fallout artifacts/sister-fallout-capture-progress.ppm
	./tapesister_sister_spirit_demo fallout artifacts/sister-fallout.ppm
	./tapesister_sister_spirit_demo fx-presets artifacts/sister-fx-preset-manager.ppm
	./tapesister_sister_spirit_demo fallout-presets artifacts/sister-fallout-preset-manager.ppm

test: test_sister_resize
test: tapesister_portal_tests

tapesister_portal_tests: tests/test_portal_instruments.inc src/ts_cdp_portal_factory.inc $(CORE) tests/test_cdp_portal.c tests/test_portal_chain_recipes.inc tests/test_portal_factory.inc
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CORE) tests/test_cdp_portal.c -o $@ -lm

# Optional source-built CDP parameter and signal checks for the final simple batch.
tapesister_portal_final_tests: $(CORE) tests/test_portal_final_batch.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

clean: clean_portal_final
.PHONY: clean_portal_final
clean_portal_final:
	rm -f tapesister_portal_final_tests

# Optional real-CDP/SDL lifecycle harness; use TS_TEST_CDP_BIN to select runtime.
tapesister_portal_controller_tests: tests/test_portal_instrument_controller.inc src/ts_cdp_portal_factory.inc $(CORE) tests/test_portal_controller.c tests/test_portal_chains.inc tests/test_portal_factory_controller.inc tests/test_portal_workflow_ui.inc src/main_sdl.c src/main_sdl_portal.inc src/tape_link.c src/tape_companion.c $(DIAG) $(MIDI_C)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell sdl2-config --cflags) $(CORE) tests/test_portal_controller.c src/tape_link.c src/tape_companion.c $(DIAG) $(MIDI_C) -o $@ $(shell sdl2-config --libs) -lm $(LIVE_LINK_LDFLAGS)
test: tapesister_audio_import_tests

tapesister_preview_loop_tests: $(CORE) tests/test_preview_loop_selection.c src/main_sdl.c src/main_sdl_portal.inc src/tape_link.c src/tape_companion.c $(DIAG) $(MIDI_C)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell sdl2-config --cflags) $(CORE) tests/test_preview_loop_selection.c src/tape_link.c src/tape_companion.c $(DIAG) $(MIDI_C) -o $@ $(shell sdl2-config --libs) -lm $(LIVE_LINK_LDFLAGS)

# Native SDL/output-file workflow harness; optional dummy-device tests.
tapesister_stereo_gesture_controller_tests: $(CORE) tests/test_stereo_gesture_controller.c src/main_sdl.c src/main_sdl_portal.inc $(wildcard src/main_sdl_audio*.inc) src/tape_link.c src/tape_companion.c $(DIAG) $(MIDI_C)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell sdl2-config --cflags) $(CORE) tests/test_stereo_gesture_controller.c src/tape_link.c src/tape_companion.c $(DIAG) $(MIDI_C) -o $@ $(shell sdl2-config --libs) -lm $(LIVE_LINK_LDFLAGS)

tapesister_canvas_recording_tests: $(CORE) tests/test_canvas_recording.c src/main_sdl.c src/main_sdl_portal.inc $(wildcard src/main_sdl_audio*.inc) src/tape_link.c src/tape_companion.c $(DIAG) $(MIDI_C)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell sdl2-config --cflags) $(CORE) tests/test_canvas_recording.c src/tape_link.c src/tape_companion.c $(DIAG) $(MIDI_C) -o $@ $(shell sdl2-config --libs) -lm $(LIVE_LINK_LDFLAGS)

tapesister_keyboard_sustain_tests: $(CORE) tests/test_keyboard_sustain.c src/main_sdl.c src/main_sdl_portal.inc $(wildcard src/main_sdl_audio*.inc) src/tape_link.c src/tape_companion.c $(DIAG) $(MIDI_C)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell sdl2-config --cflags) $(CORE) tests/test_keyboard_sustain.c src/tape_link.c src/tape_companion.c $(DIAG) $(MIDI_C) -o $@ $(shell sdl2-config --libs) -lm $(LIVE_LINK_LDFLAGS)

test: test_audio_hardening_structure
test: test_live_link_structure
test: test_companion_focus_structure
test: test_identity_packaging
test: test_live_link

test: tapesister_core_tests tapesister_audio_frame_tests test_audio_mixer test_audio_lifecycle test_note_bank_stereo test_performance_stereo test_capture_stereo test_performance_recorder test_external_input_channels test_input_ownership test_realtime_diagnostics test_sister_buffer test_sister_heads test_sister_transport test_sister_modulation test_sister_feedback test_sister_ghost_tone test_sister_stereo test_sister_weave test_sister_effect_routing test_sister_post_fx test_sister_fallout test_sister_limiter test_sister_duck_filter test_sister_snapshot test_sister_routes test_sister_runtime test_sister_source_mask test_sister_performance_sources test_sister_capture test_sister_recursion test_sister_lifecycle test_sister_visibility test_sister_ui_model test_sister_wave_snapshot test_waveform_display_modes test_sister_source_ui test_sister_capture_ui test_sister_palette test_sister_preset test_sister_project_state test_sister_pathological tapesister_sample_channels_tests tapesister_tsr27_tests tapesister_wav_channels_tests tapesister_smear_tests tapesister_tear_tests tapesister_bank_tests tapesister_editor_contract_tests tapesister_drone_tests tapesister_canvas_tests tapesister_capture_tests tapesister_performance_tests tapesister_midi_tests tapesister_external_record_tests tapesister_input_monitor_tests tapesister_capture_archive_tests tapesister_sample_pages_tests tapesister_audio_config_tests tapesister_transform_tests tapesister_chain_stamp_tests tapesister_exchange_tests tapesister_render_damage_tests tapesister_waveform_cache_tests tapesister_render_efficiency_tests
	./tapesister_core_tests
	./tapesister_portal_tests
	./tapesister_audio_frame_tests
	./test_audio_mixer
	./test_audio_lifecycle
	./test_live_link
	./test_note_bank_stereo
	./test_performance_stereo
	./test_capture_stereo
	./test_performance_recorder
	./test_external_input_channels
	./test_input_ownership
	./test_realtime_diagnostics
	./test_sister_buffer
	./test_sister_resize
	./test_sister_heads
	./test_sister_transport
	./test_sister_modulation
	./test_sister_feedback
	./test_sister_ghost_tone
	./test_sister_stereo
	./test_sister_weave
	./test_sister_effect_routing
	./test_sister_post_fx
	./test_sister_fallout
	./test_sister_limiter
	./test_sister_duck_filter
	./test_sister_snapshot
	./test_sister_routes
	./test_sister_runtime
	./test_sister_source_mask
	./test_sister_performance_sources
	./test_sister_capture
	./test_sister_recursion
	./test_sister_lifecycle
	./test_sister_visibility
	./test_sister_ui_model
	./test_sister_wave_snapshot
	./test_waveform_display_modes
	./test_sister_source_ui
	./test_sister_capture_ui
	./test_sister_palette
	./test_sister_preset
	./test_sister_project_state
	./test_sister_pathological
	./tapesister_sample_channels_tests
	./tapesister_tsr27_tests
	./tapesister_wav_channels_tests
	./tapesister_audio_import_tests
	./tapesister_smear_tests
	./tapesister_tear_tests
	./tapesister_bank_tests
	./tapesister_editor_contract_tests
	./tapesister_drone_tests
	./tapesister_canvas_tests
	./tapesister_capture_tests
	./tapesister_performance_tests
	./tapesister_midi_tests
	./tapesister_external_record_tests
	./tapesister_input_monitor_tests
	./tapesister_capture_archive_tests
	./tapesister_sample_pages_tests
	./tapesister_audio_config_tests
	./tapesister_transform_tests
	./tapesister_chain_stamp_tests
	./tapesister_exchange_tests
	./tapesister_render_damage_tests
	./tapesister_waveform_cache_tests
	./tapesister_render_efficiency_tests

tapesister_smear_tests: $(CORE) tests/test_smear.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_audio_frame_tests: $(CORE) tests/test_audio_frame.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_audio_mixer: $(CORE) tests/test_audio_mixer.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_audio_lifecycle: src/ts_audio_lifecycle.c tests/test_audio_lifecycle.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_audio_hardening_structure:
	python3 tests/test_audio_hardening_structure.py

test_live_link_structure:
	python3 tests/test_live_link_structure.py

test_companion_focus_structure:
	python3 tests/test_companion_focus_structure.py

test_identity_packaging:
	python3 tests/test_identity_packaging.py

test_note_bank_stereo: $(CORE) tests/test_note_bank_stereo.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_performance_stereo: $(CORE) tests/test_performance_stereo.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_capture_stereo: $(CORE) tests/test_capture_stereo.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_performance_recorder: $(CORE) tests/test_performance_recorder.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_external_input_channels: $(CORE) tests/test_external_input_channels.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_input_ownership: $(CORE) tests/test_input_ownership.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_realtime_diagnostics: $(CORE) tests/test_realtime_diagnostics.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_buffer: $(CORE) tests/test_sister_buffer.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_resize: $(CORE) tests/test_sister_resize.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_heads: $(CORE) tests/test_sister_heads.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_transport: $(CORE) tests/test_sister_transport.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_modulation: $(CORE) tests/test_sister_modulation.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_feedback: $(CORE) tests/test_sister_feedback.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_ghost_tone: $(CORE) tests/test_sister_ghost_tone.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_stereo: $(CORE) tests/test_sister_stereo.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_weave: $(CORE) tests/test_sister_weave.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_effect_routing: $(CORE) tests/test_sister_effect_routing.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_post_fx: $(CORE) tests/test_sister_post_fx.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_fallout: $(CORE) tests/test_sister_fallout.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_limiter: $(CORE) tests/test_sister_limiter.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_duck_filter: $(CORE) tests/test_sister_duck_filter.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_snapshot: $(CORE) tests/test_sister_snapshot.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_routes: $(CORE) tests/test_sister_routes.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_runtime: $(CORE) tests/test_sister_runtime.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_source_mask: $(CORE) tests/test_sister_source_mask.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_performance_sources: $(CORE) tests/test_sister_performance_sources.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_capture: $(CORE) tests/test_sister_capture.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_recursion: $(CORE) tests/test_sister_recursion.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_lifecycle: $(CORE) tests/test_sister_lifecycle.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_visibility: $(CORE) tests/test_sister_visibility.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_ui_model: $(CORE) tests/test_sister_ui_model.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_wave_snapshot: $(CORE) tests/test_sister_wave_snapshot.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_waveform_display_modes: $(CORE) tests/test_waveform_display_modes.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_source_ui: $(CORE) tests/test_sister_source_ui.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_live_link: tests/test_live_link.c src/tape_link.c
	$(CC) $(CFLAGS) -Isrc $^ -o $@ -lm $(LIVE_LINK_LDFLAGS)

test_sister_capture_ui: $(CORE) tests/test_sister_capture_ui.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_palette: $(CORE) tests/test_sister_palette.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_preset: $(CORE) tests/test_sister_preset.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_project_state: $(CORE) tests/test_sister_project_state.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_pathological: $(CORE) tests/test_sister_pathological.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

stress-sister: test_sister_pathological
	./test_sister_pathological --certify

benchmark_sister_callback: $(CORE) tests/benchmark_sister_callback.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

benchmark-sister: benchmark_sister_callback
	./benchmark_sister_callback 2000000 256

tapesister_sample_channels_tests: $(CORE) tests/test_sample_channels.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test: test_stereo_gestures
test_stereo_gestures: tapesister_stereo_gesture_tests
	./tapesister_stereo_gesture_tests

tapesister_stereo_gesture_tests: $(CORE) tests/test_stereo_gestures.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

clean: clean_stereo_gestures
.PHONY: test_stereo_gestures clean_stereo_gestures
clean_stereo_gestures:
	rm -f tapesister_stereo_gesture_tests tapesister_stereo_gesture_controller_tests test-stereo-gestures.tsr

tapesister_tsr27_tests: $(CORE) tests/test_tsr27.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_wav_channels_tests: $(CORE) tests/test_wav_channels.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_audio_import_tests: $(CORE) tests/test_audio_import.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_tear_tests: $(CORE) tests/test_tear.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_bank_tests: $(CORE) tests/test_bank.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_editor_contract_tests: $(CORE) tests/test_editor_contract.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_drone_tests: $(CORE) tests/test_drone.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_canvas_tests: $(CORE) tests/test_canvas.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_capture_tests: $(CORE) tests/test_capture.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_performance_tests: $(CORE) tests/test_performance.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_midi_tests: $(CORE) $(MIDI_C) tests/test_midi.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_external_record_tests: $(CORE) tests/test_external_record.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_input_monitor_tests: $(CORE) tests/test_input_monitor.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_capture_archive_tests: $(CORE) tests/test_capture_archive.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_sample_pages_tests: $(CORE) tests/test_sample_pages.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_audio_config_tests: $(CORE) tests/test_audio_config.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_transform_tests: $(CORE) tests/test_transform.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_cdp_native_smoke: $(CORE) tests/test_cdp_native_smoke.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_chain_stamp_tests: $(CORE) tests/test_chain_stamp.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_exchange_tests: $(CORE) tests/test_exchange.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_render_damage_tests: $(CORE) tests/test_render_damage.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_waveform_cache_tests: $(CORE) tests/test_waveform_cache.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_render_efficiency_tests: $(CORE) tests/test_render_efficiency.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

screenshot: tapesister_render_demo
	./tapesister_render_demo artifacts/tapesister-independent-tiles.ppm

clean:
	rm -f tapesister_canvas_recording_tests tapesister_keyboard_sustain_tests
	rm -f tapesister benchmark_sister_callback tapesister_core_tests tapesister_audio_frame_tests test_audio_mixer test_note_bank_stereo test_performance_stereo test_capture_stereo test_external_input_channels test_input_ownership test_realtime_diagnostics test_sister_buffer test_sister_heads test_sister_transport test_sister_modulation test_sister_feedback test_sister_ghost_tone test_sister_stereo test_sister_weave test_sister_effect_routing test_sister_post_fx test_sister_fallout test_sister_limiter test_sister_duck_filter test_sister_snapshot test_sister_routes test_sister_runtime test_sister_source_mask test_sister_performance_sources test_sister_capture test_sister_recursion test_sister_lifecycle test_sister_visibility test_sister_preset test_sister_project_state test_sister_pathological tapesister_sample_channels_tests tapesister_tsr27_tests tapesister_wav_channels_tests tapesister_audio_import_tests tapesister_smear_tests tapesister_tear_tests tapesister_bank_tests tapesister_editor_contract_tests tapesister_drone_tests tapesister_canvas_tests tapesister_capture_tests tapesister_performance_tests tapesister_midi_tests tapesister_external_record_tests tapesister_input_monitor_tests tapesister_capture_archive_tests tapesister_sample_pages_tests tapesister_audio_config_tests tapesister_transform_tests tapesister_cdp_native_smoke tapesister_chain_stamp_tests tapesister_exchange_tests tapesister_render_damage_tests tapesister_waveform_cache_tests tapesister_render_efficiency_tests tapesister_render_demo tapesister_sister_spirit_demo third_party/rtmidi/*.o test-family.tsr test-roundtrip.wav test-external-stereo.wav test-tear.tsr test-bank-independent.tsr test-drone.ini test-drone.tsr test-canvas.tsr test-transform.tsr test-audio-config.ini test-audio-config-blank.ini test-audio-config-legacy.ini artifacts/*.ppm artifacts/*.png
	rm -f test_performance_recorder test_sister_ui_model test_sister_wave_snapshot test_waveform_display_modes test_sister_source_ui test_sister_capture_ui test_sister_palette test-sister-palette.pal test-sister-palette-legacy.pal
	rm -f test_audio_lifecycle test-audio-config-backend.ini
	rm -f test_sister_resize
	rm -f test_live_link
