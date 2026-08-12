CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Iinclude
CORE = src/ts_sample.c src/ts_ui.c

.PHONY: all test screenshot clean

all: tapesister

tapesister: $(CORE) src/main_sdl.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(shell sdl2-config --cflags) $^ -o $@ $(shell sdl2-config --libs) -lm

tapesister_core_tests: $(CORE) tests/test_core.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

tapesister_render_demo: $(CORE) tests/render_demo.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ -lm

test: tapesister_core_tests
	./tapesister_core_tests

screenshot: tapesister_render_demo
	./tapesister_render_demo artifacts/tapesister-first-slice.ppm

clean:
	rm -f tapesister tapesister_core_tests tapesister_render_demo test-roundtrip.wav artifacts/*.ppm
