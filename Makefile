CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Iinclude -Ithird_party
CORE = src/ts_sample.c src/ts_audition.c src/ts_note_bank.c src/ts_browser.c src/ts_config.c src/ts_recipe.c src/ts_ui.c

.PHONY: all test screenshot runtime-assets clean

all: tapesister runtime-assets

tapesister: $(CORE) src/main_sdl.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell sdl2-config --cflags) $^ -o $@ $(shell sdl2-config --libs) -lm

runtime-assets:
	@test ! -f assets/tapesister_welcome.wav || test -s assets/tapesister_welcome.wav

tapesister_core_tests: $(CORE) tests/test_core.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_render_demo: $(CORE) tests/render_demo.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test: tapesister_core_tests tapesister_smear_tests tapesister_tear_tests
	./tapesister_core_tests
	./tapesister_smear_tests
	./tapesister_tear_tests

tapesister_smear_tests: $(CORE) tests/test_smear.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_tear_tests: $(CORE) tests/test_tear.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

screenshot: tapesister_render_demo
	./tapesister_render_demo artifacts/tapesister-parent-current.ppm

clean:
	rm -f tapesister tapesister_core_tests tapesister_smear_tests tapesister_tear_tests tapesister_render_demo test-roundtrip.wav test-tear.tsr artifacts/*.ppm
