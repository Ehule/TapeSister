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

## Threshold / Rupture revision

`threshold_rupture_score.py` revises the long performance after listening
feedback: stronger trap phrasing, contrasting peaks and withdrawals, a more
exposed sustained choir, and a shorter closing DJ handoff. It runs at 150 BPM
with a 75 BPM half-time relationship. The original first 5:07.2 has identical
note and volume event times on the finer grid. The revision has 896 bars at
150 BPM (448 paired phrase bars), 225 patterns/orders including the silent
terminal order, and a 24:00 reference duration.

The new mode reads an existing, complete Threshold project, preserves its
23 tiles and CDP recipes, adds four native FM voices, then writes a separate
project through the normal serializer. The source project is never saved over.
It requires no CDP executable because its seven earlier CDP transformations
are already present. To recreate everything from source, first run the
`create-performance` command above, then:

```sh
python3 tools/threshold_rupture_score.py output/threshold-rupture-score.csv
build/tapesister_compose revise-performance output/Threshold-Rupture/Threshold-Rupture.tsr output/threshold-rupture-score.csv output/Threshold/Threshold.tsr
build/tapesister_compose render output/Threshold-Rupture/Threshold-Rupture.tsr output/Threshold-Rupture.wav 1440
```

The first rupture is at 8:32 (pattern 080), the first aftermath at 11:31.2
(108), another drop at 14:56 (140), and the principal peak at 18:46.4 (176).
A second wave starts at 20:28.8 (192), followed by resolution at 21:20 (200).
The handoff begins at 22:36.8 (212); the bass is out at 23:02.4 (216).
Pattern 224 is a silent terminal order with a BE0 jump to itself.

Track 10 remains a separate guide for live Terra playing. Track 13's bells
and Track 14's high choir answers are optional additional mutes. The suggested
tuning remains D4 E4 F4 G4 A4 Bb4 C5 D5 E5 F5 G5 A5. The lead leaves two
phrase bars free out of each eight for live responses.

`TS_COMPOSE_MUTE` takes comma-separated, **zero-based** lanes for an optional
rehearsal render. For example, mute only user-facing Track 10:

```sh
TS_COMPOSE_MUTE=9 build/tapesister_compose render output/Threshold-Rupture/Threshold-Rupture.tsr output/Threshold-Rupture-Terra-Backing.wav 1440
```

This applies native channel mutes before playback, retains the other lanes'
shared effects, and does not modify the saved project. No audio subtraction
or external mixing is used. Ordinary `render` and both original creation
modes retain their behavior. The added voices and authored score do not change
the application UI, live audio engine or file format.

Timing note: the current embedded player retains FT2's 44 kHz reference-clock
rounding. At nominal 150 BPM its actual tempo is approximately 150.0682 BPM
(75.0341 half-time), a 0.0455% difference. Nominal cues above therefore run
slightly early in the native render: the main entry is about 18:45.9, the
handoff 22:36.2, and the terminal order 23:52.95. The WAV still lasts 24:00,
including its tail. Use these actual cues or match the audible downbeat when
beatmatching. This utility deliberately retains the same clock as the live
application; it does not silently make the offline render run at another rate.

Revision validation: all 72,973 authored cells match the saved native score
(including the player's standard EC0 representation of note cuts), all
27 tile hashes survive reload, and all 23 original exported sound assets
are unchanged. The original and relocated opening match exactly over 51.2
seconds. FM directions, shared-send routing and embedded-tracker tests pass.
Both complete renders are finite with no output-guard contacts. Main mix:
-14.9 LUFS, approximately -1.2 dBTP; Track-10-muted backing: -15.0 LUFS,
approximately -2.3 dBTP. The main mix's loudness range is 18.1 LU.
These are numerical/headless Linux checks; the composition was not auditioned
and this pass did not test the optional utility on Windows or live hardware.
