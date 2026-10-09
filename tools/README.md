# Native composition utility — Black Snow

`tapesister_compose` is an optional offline driver for the first original
TapeSister composition, **Black Snow**. It uses TapeSister's FM generator,
CDP Portal adapter, Sister Machine, native project serializer, embedded
TrackSister player, shared sends and actual application audio callback.
There is no alternate synthesizer or mixer. Normal application builds and
the project format are unchanged.

Build with the application's normal C compiler, CMake and SDL2 dependencies:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target tapesister_compose -j 4
```

Supply a working CDP8 binary directory containing `pvoc`, `stretch`, `blur`,
`modify` and `glisten` (the bundled runtime is under `cdp/bin`). This assignment
built those executables from the supplied CDP8 source. CDP is only needed to
regenerate the four CDP-derived assets; the fifth derived asset uses Sister
Machine. Opening and rendering the finished project requires no CDP install.

```sh
mkdir -p output
python3 tools/black_snow_score.py output/score.csv
build/tapesister_compose create output/Black-Snow/Black-Snow.tsr output/score.csv /absolute/path/to/cdp8/bin
build/tapesister_compose render output/Black-Snow/Black-Snow.tsr output/Black-Snow.wav 305.333333333
```

Create the parent `output` directory, but let TapeSister create its named
`Black-Snow` project folder. The serializer intentionally refuses to replace
an unrelated folder. `create` validates score and tile identity, exports the
embedded tracker score, saves through the native project API, reloads, and
compares score/sample hashes, FM recipe presence, routing and effect settings.
It writes reusable CDP Portal recipes alongside the project.

The renderer always loads the saved project and starts playback at order zero.
It writes 48 kHz / 24-bit stereo PCM directly from the native audio callback,
without external normalization, limiting, mixing or synthesis. `END_SECONDS`
limits the render; optional `START_SECONDS` discards the leading frames after
rendering them, preserving effect history for an excerpt:

```sh
build/tapesister_compose render output/Black-Snow/Black-Snow.tsr output/Preview.wav 245 200
```

`black_snow_score.py --mode groove` creates an eight-bar percussion/bass study;
`--mode section` creates a sixteen-bar development study. The full score uses
88 musical bars plus a silent terminal order containing a B16 jump back to
itself. This avoids the embedded player's ordinary whole-song wrap, while
allowing the effects to decay. The terminal order is intentionally silent.

The text score contains pattern, row, lane, tile alias, MIDI note, volume,
ASCII effect command and value. No private binary offsets are used.
The source preserves every FM patch and the exact native/CDP bake settings.
All note choices and rhythmic variations are deliberate and deterministic.

For technical isolation, `TS_COMPOSE_SOLO=0` through `15` mutes all other
tracker lanes during rendering, retaining that lane's native sends. This
does not change the saved project. Do not set it for the final mix.

Validation for this composition includes the existing FM-direction, source
routing and native tracker tests, repeated native save/reload checks, full
renders, a relocated-project render comparison, and audio measurements.
Numerical checks establish technical behavior, not artistic quality. No
audio-listening capability was available during this production run.

This tool is scoped to these fixed-tempo authored scores; its duration handling
is not a general renderer for imported modules with tempo changes, arbitrary
loops or jumps. It deliberately does not add generalized sidechain compression
or change the application's UI or real-time engine.

## Threshold performance draft

`threshold_score.py` writes an original 24-minute performance at 75 BPM in
D minor. It retains the Black Snow drum/bass timbres, adds one sustained FM
choir and three CDP transformations, and builds from long drones to a clear
backbeat and an ending for a DJ handoff. Its lead attacks use eighth-note
subdivisions, and the performance mixer reduces lead sends. The shared delay
and Sister echo use 600 ms (a dotted eighth at 75 BPM).

```sh
python3 tools/threshold_score.py output/threshold-score.csv
build/tapesister_compose create-performance output/Threshold/Threshold.tsr output/threshold-score.csv /absolute/path/to/cdp8/bin
build/tapesister_compose render output/Threshold/Threshold.tsr output/Threshold.wav 1440
```

This mode has 23 tiles, 15 retained FM recipes, seven CDP transformations and
one Sister Machine bake. Both modes keep their own sound/mix settings; the
original `create` command remains the 72 BPM Black Snow composition.
`#@pattern INDEX NAME` score comments supply optional native pattern names.

Threshold has 448 musical bars (23:53.6) and one silent terminal pattern;
the 24:00 render includes 6.4 seconds of effects tail. Pattern names include
decimal indices. The handoff starts at pattern 100 (21:20), harmonic layers
clear at 104 (22:11.2), and bass clears at 108 (23:02.4). Patterns 108–111
provide sixteen bars of rhythm for mixing. The final pattern 112 contains
a B70 jump to itself, not an automatic loop of the closing groove.

The composition stays at 75 BPM throughout. Its apparent pace changes through
arrangement and subdivisions. All fades and kick-related bass ducking are
editable tracker volume cells. There is no audio-triggered sidechain, external
mixing/mastering, copied reference melody or reference-track audio. Native
save/reload checks cover every tile, the score and the shared routing state.
Listening and rehearsal on the performance machine remain essential: this is
an unauditioned composition draft, not a claim of a rehearsed live show.
