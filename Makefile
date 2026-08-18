CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Iinclude -Ithird_party
CORE = src/ts_sample.c src/ts_audition.c src/ts_note_bank.c src/ts_capture.c src/ts_browser.c src/ts_config.c src/ts_recipe.c src/ts_dsp_recipe.c src/ts_palette.c src/ts_cdp_recipe.c src/ts_cdp_adapter.c src/ts_transform.c src/ts_dsp_transform.c src/ts_exchange.c src/ts_ui.c
DIAG = src/ts_startup_diag.c
TAPESISTER_LDFLAGS =
ifeq ($(OS),Windows_NT)
TAPESISTER_LDFLAGS += -Wl,--stack,16777216
endif

.PHONY: all test screenshot runtime-assets clean

all: tapesister runtime-assets

tapesister: $(CORE) src/main_sdl.c $(DIAG)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell sdl2-config --cflags) $^ -o $@ $(shell sdl2-config --libs) -lm $(TAPESISTER_LDFLAGS)

runtime-assets:
	@test ! -f assets/tapesister_welcome.wav || test -s assets/tapesister_welcome.wav

tapesister_core_tests: $(CORE) tests/test_core.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_render_demo: $(CORE) tests/render_demo.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test: tapesister_core_tests tapesister_smear_tests tapesister_tear_tests tapesister_bank_tests tapesister_editor_contract_tests tapesister_drone_tests tapesister_canvas_tests tapesister_capture_tests tapesister_transform_tests tapesister_chain_stamp_tests tapesister_exchange_tests
	./tapesister_core_tests
	./tapesister_smear_tests
	./tapesister_tear_tests
	./tapesister_bank_tests
	./tapesister_editor_contract_tests
	./tapesister_drone_tests
	./tapesister_canvas_tests
	./tapesister_capture_tests
	./tapesister_transform_tests
	./tapesister_chain_stamp_tests
	./tapesister_exchange_tests

tapesister_smear_tests: $(CORE) tests/test_smear.c
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

tapesister_transform_tests: $(CORE) tests/test_transform.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_chain_stamp_tests: $(CORE) tests/test_chain_stamp.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_exchange_tests: $(CORE) tests/test_exchange.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

screenshot: tapesister_render_demo
	./tapesister_render_demo artifacts/tapesister-independent-tiles.ppm

clean:
	rm -f tapesister tapesister_core_tests tapesister_smear_tests tapesister_tear_tests tapesister_bank_tests tapesister_editor_contract_tests tapesister_drone_tests tapesister_canvas_tests tapesister_capture_tests tapesister_transform_tests tapesister_chain_stamp_tests tapesister_exchange_tests tapesister_render_demo test-roundtrip.wav test-tear.tsr test-bank-independent.tsr test-drone.ini test-drone.tsr test-canvas.tsr test-transform.tsr artifacts/*.ppm
