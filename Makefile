CC ?= cc
CXX ?= c++
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CXXFLAGS ?= -std=c++11 -O2
CPPFLAGS ?= -Iinclude -Ithird_party
CORE = src/ts_sample.c src/ts_fm.c src/ts_audition.c src/ts_note_bank.c src/ts_note_event.c src/ts_performance.c src/ts_audio_mixer.c src/ts_sister_effects.c src/ts_sister_post_fx.c src/ts_sister_machine.c src/ts_sister_wave_snapshot.c src/ts_sister_runtime.c src/ts_sister_ui.c src/ts_sister_preset.c src/ts_sister_project_state.c src/ts_capture.c src/ts_capture_archive.c src/ts_input_monitor.c src/ts_input_ownership.c src/ts_sample_pages.c src/ts_browser.c src/ts_config.c src/ts_audio_config.c src/ts_recipe.c src/ts_dsp_recipe.c src/ts_palette.c src/ts_cdp_recipe.c src/ts_cdp_adapter.c src/ts_transform.c src/ts_dsp_transform.c src/ts_exchange.c src/ts_render_damage.c src/ts_waveform_cache.c src/ts_waveform_display.c src/ts_ui.c
SDL_MAIN = src/main_sdl.c
DIAG = src/ts_startup_diag.c
MIDI_C = src/ts_midi_input.c
MIDI_CPP_OBJS =
MIDI_CPPFLAGS =
MIDI_LDFLAGS =
TAPESISTER_LDFLAGS =
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

.PHONY: all test screenshot runtime-assets clean

all: tapesister runtime-assets

tapesister: $(CORE) $(SDL_MAIN) $(DIAG) $(MIDI_C) $(MIDI_CPP_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MIDI_CPPFLAGS) $(shell sdl2-config --cflags) $(CORE) $(SDL_MAIN) $(DIAG) $(MIDI_C) $(MIDI_CPP_OBJS) -o $@ $(shell sdl2-config --libs) -lm $(TAPESISTER_LDFLAGS) $(MIDI_LDFLAGS)

third_party/rtmidi/%.o: third_party/rtmidi/%.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(MIDI_CPPFLAGS) -c $< -o $@

runtime-assets:
	@test ! -f assets/tapesister_welcome.wav || test -s assets/tapesister_welcome.wav

tapesister_core_tests: $(CORE) tests/test_core.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_render_demo: $(CORE) tests/render_demo.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test: test_sister_resize

test: tapesister_core_tests tapesister_audio_frame_tests test_audio_mixer test_note_bank_stereo test_performance_stereo test_capture_stereo test_external_input_channels test_input_ownership test_sister_buffer test_sister_heads test_sister_transport test_sister_modulation test_sister_feedback test_sister_ghost_tone test_sister_stereo test_sister_weave test_sister_effect_routing test_sister_post_fx test_sister_duck_filter test_sister_snapshot test_sister_routes test_sister_runtime test_sister_source_mask test_sister_performance_sources test_sister_capture test_sister_recursion test_sister_lifecycle test_sister_visibility test_sister_ui_model test_sister_wave_snapshot test_waveform_display_modes test_sister_source_ui test_sister_capture_ui test_sister_palette test_sister_preset test_sister_project_state tapesister_sample_channels_tests tapesister_tsr27_tests tapesister_wav_channels_tests tapesister_smear_tests tapesister_tear_tests tapesister_bank_tests tapesister_editor_contract_tests tapesister_drone_tests tapesister_canvas_tests tapesister_capture_tests tapesister_performance_tests tapesister_midi_tests tapesister_external_record_tests tapesister_input_monitor_tests tapesister_capture_archive_tests tapesister_sample_pages_tests tapesister_audio_config_tests tapesister_transform_tests tapesister_chain_stamp_tests tapesister_exchange_tests tapesister_render_damage_tests tapesister_waveform_cache_tests tapesister_render_efficiency_tests
	./tapesister_core_tests
	./tapesister_audio_frame_tests
	./test_audio_mixer
	./test_note_bank_stereo
	./test_performance_stereo
	./test_capture_stereo
	./test_external_input_channels
	./test_input_ownership
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
	./tapesister_sample_channels_tests
	./tapesister_tsr27_tests
	./tapesister_wav_channels_tests
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

test_note_bank_stereo: $(CORE) tests/test_note_bank_stereo.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_performance_stereo: $(CORE) tests/test_performance_stereo.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_capture_stereo: $(CORE) tests/test_capture_stereo.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_external_input_channels: $(CORE) tests/test_external_input_channels.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_input_ownership: $(CORE) tests/test_input_ownership.c
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

test_sister_capture_ui: $(CORE) tests/test_sister_capture_ui.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_palette: $(CORE) tests/test_sister_palette.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_preset: $(CORE) tests/test_sister_preset.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test_sister_project_state: $(CORE) tests/test_sister_project_state.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_sample_channels_tests: $(CORE) tests/test_sample_channels.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_tsr27_tests: $(CORE) tests/test_tsr27.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_wav_channels_tests: $(CORE) tests/test_wav_channels.c
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
	rm -f tapesister tapesister_core_tests tapesister_audio_frame_tests test_audio_mixer test_note_bank_stereo test_performance_stereo test_capture_stereo test_external_input_channels test_input_ownership test_sister_buffer test_sister_heads test_sister_transport test_sister_modulation test_sister_feedback test_sister_ghost_tone test_sister_stereo test_sister_weave test_sister_effect_routing test_sister_post_fx test_sister_duck_filter test_sister_snapshot test_sister_routes test_sister_runtime test_sister_source_mask test_sister_performance_sources test_sister_capture test_sister_recursion test_sister_lifecycle test_sister_visibility test_sister_preset test_sister_project_state tapesister_sample_channels_tests tapesister_tsr27_tests tapesister_wav_channels_tests tapesister_smear_tests tapesister_tear_tests tapesister_bank_tests tapesister_editor_contract_tests tapesister_drone_tests tapesister_canvas_tests tapesister_capture_tests tapesister_performance_tests tapesister_midi_tests tapesister_external_record_tests tapesister_input_monitor_tests tapesister_capture_archive_tests tapesister_sample_pages_tests tapesister_audio_config_tests tapesister_transform_tests tapesister_chain_stamp_tests tapesister_exchange_tests tapesister_render_damage_tests tapesister_waveform_cache_tests tapesister_render_efficiency_tests tapesister_render_demo third_party/rtmidi/*.o test-roundtrip.wav test-external-stereo.wav test-tear.tsr test-bank-independent.tsr test-drone.ini test-drone.tsr test-canvas.tsr test-transform.tsr test-audio-config.ini test-audio-config-blank.ini test-audio-config-legacy.ini artifacts/*.ppm
	rm -f test_sister_ui_model test_sister_wave_snapshot test_waveform_display_modes test_sister_source_ui test_sister_capture_ui test_sister_palette test-sister-palette.pal test-sister-palette-legacy.pal
	rm -f test_sister_resize
