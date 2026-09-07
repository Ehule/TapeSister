# CDP Portal — explore, save, and manage your tools

![Native CDP Portal rendering with a real grain-density result](images/cdp-portal.png)

CDP Portal is the exploratory workbench inside TapeSister. The original 32
curated CDP instruments remain unchanged. The Portal uses a separate, stable-ID
process registry and separate recipe/pin storage.

## Open and explore

Load or generate a mono tile. On the CDP panel, click **PORTAL**, or press
**Ctrl+Shift+P** from the main workspace. The Portal takes an immutable snapshot
of Current (the current selection when one exists). **SOURCE/SEL** toggles
between whole-tile and main-canvas selection scope; **RELOAD** refreshes the
snapshot. Failed source loading clears the previous snapshot.

The waveset family exposes 12 source-verified modes of CDP's `distort` program:
cycle reverse, repeat, repeat2, interpolate, multiply, divide, omit, average,
delete modes 1/2/3, and reform mode 5. Four spectral, four time/tape, six filter, and four granular modes bring
the Portal to 30 processes. Search matches names, stable command IDs, descriptions, and
families. **ALL**, **SAVE**, and **PINS** switch the left browser between
processes, saved recipes, and user process pins. Scroll that column with the
mouse wheel. The family button below the tabs cycles **ALL FAMILIES**,
**WAVESET**, **SPECTRAL**, **TIME / TAPE**, **FILTER**, and **GRAINS**. Family and text filters combine, including in
saved recipes and pins. Clearing the search and choosing ALL FAMILIES restores
the complete list. Filtering never renumbers stored slots.

These 30 Portal modes accept mono input only; stereo is rejected explicitly.
Multi-input/multichannel, breakpoint-file, and text-file workflows remain future
work. The factory bank retains its original 32 curated instruments.

## Spectral family

| Process | Native CDP identity | Controls |
| --- | --- | --- |
| Spectral Blur | `blur.blur` | 1–4096 spectral windows, bounded by source length |
| Suppress Partials | `blur.suppress` | Remove 1–513 loudest partials per frame |
| Spectral Chorus | `blur.chorus.5` | Amplitude scatter 1–1028; frequency scatter 1–4 |
| Spectral Time | `stretch.time.1` | Duration ratio 0.25–16, bounded by Portal memory limit |

Each preview automatically runs **PVOC analysis → process → PVOC synthesis**.
You bring in audio and receive audio; intermediate analysis files stay in the
isolated temporary job directory and are cleaned up afterward. This batch uses
1024-point analysis, CDP overlap setting 3, a 128-frame hop, and 513 spectral
bins. Analysis settings are fixed in these version-1 process definitions.
Sources need at least 2048 frames and 40 ms. Blur rejects window counts longer
than the source supports rather than silently changing the recipe.

These are native scalar controls. CDP can also accept breakpoint files for some
parameters; this batch does not expose that input. The time-ratio and blur caps
are Portal bounds, not claims about CDP's maximum capability.

Chorus uses randomness, so an exact pin preserves the settings rather than
guaranteeing identical audio on each render. Spectral processing can produce
HOT results or intentional silence (especially strong partial suppression).
The existing peak report and audition limiter remain available. PVOC padding
can slightly extend duration even when a process does not stretch time.

## Time / tape family

| Process | Native CDP identity | Controls |
| --- | --- | --- |
| Tape Speed | `modify.speed.1` | Speed multiplier 0.125–8 |
| Tape Transpose | `modify.speed.2` | Fractional semitone shift −36 to +36 |
| Tape Vibrato | `modify.speed.6` | Rate 0–120 Hz; depth 0–24 semitones |
| Sound Reverse | `modify.radical.1` | Whole-snapshot reversal, no numeric parameters |

These processes operate directly on the audio waveform. Speed and transposition
change pitch and duration together; vibrato continuously varies playback speed.
Compare them with Spectral Time, which changes duration while retaining pitch,
or Cycle Reverse, which reverses small groups instead of the whole sound.

This batch requires at least 40 ms of mono audio. Speed, transposition, and
depth use bounded Portal ranges within CDP's native limits. Requests whose
estimated output exceeds eight million frames are rejected before launching.
Vibrato uses a conservative estimate based on its slowest permitted speed.

## Filter family

| Process | Native CDP identity | Controls |
| --- | --- | --- |
| Notch Filter | `filter.variable.1` | Acuity, output gain, frequency, tail |
| Band Pass | `filter.variable.2` | Acuity, output gain, frequency, tail |
| Low Pass | `filter.variable.3` | Acuity, output gain, frequency, tail |
| High Pass | `filter.variable.4` | Acuity, output gain, frequency, tail |
| Sweeping Band | `filter.sweeping.2` | Acuity, output gain, low/high frequency, sweep rate, tail, start phase |
| Phasing | `filter.phasing.2` | Phasing gain, fixed delay, tail |

Try Low Pass and High Pass on the same source to hear which layers each reveals.
Band Pass isolates a region; Notch cuts a region out. **Acuity** is CDP's native
control: smaller values make a narrower, more resonant filter. Its Portal range
is 0.05–1. Output gain is a linear multiplier from 0.01–1; resonance can still
boost the result, so watch the peak report and compare Source/Result levels.

Sweeping Band moves between **LOW HZ** and **HIGH HZ** at 0–20 cycles per second.
Low must be below High. Start phase 0 begins low, 0.5 begins high, and 1 returns
low. A zero sweep rate holds the starting position. **Wheel over the parameter
labels** to reach the remaining controls; the footer shows which controls are
visible. Wheeling a slider or number makes fine changes to that value instead.
All seven controls can be typed exactly, saved, or exposed as pin macros.

Frequency controls span 20–6000 Hz and must also fit within one sixth of the
source sample rate. This is a conservative Portal bound for CDP's state-variable
filter, whose recurrence is not stable all the way up to Nyquist. Settings are
rejected with an explanation when the source rate is too low; saved recipes are
never silently adjusted. These bounds are not CDP's full nominal parameter range.

Phasing mixes the source with a delayed allpass signal. **Phasing Gain** is its
feedback coefficient (−0.95 to +0.95), not an output-volume slider. Delay ranges
from 0.1–50 ms and must fit between one source sample and half the source duration.
This version holds the delay fixed; Sweeping Band provides automatic motion.

All six modes work directly on mono WAV audio of at least 40 ms. **TAIL SECONDS**
appends 0.01–2 seconds (default 0.25) for decay. Zero is deliberately excluded:
CDP uses it to request an automatic tail of unknown duration. The source plus
explicit tail must fit the Portal's eight-million-frame limit. The tail is part
of the rendered result when applied or copied to a new tile. No breakpoint files
are needed for this batch.

## Grains family

| Process | Native CDP identity | Controls |
| --- | --- | --- |
| Granular Pitch | `modify.brassage.1` | Semitone shift −24 to +24 |
| Granular Time | `modify.brassage.2` | Input velocity 0.125–8 |
| Grain Scramble | `modify.brassage.4` | Grain length 12–250 ms; lookback 0–2000 ms |
| Grain Density | `modify.brassage.5` | Grain overlap 0.125–2 |

These processes cut grains out of the source rather than relying on quiet gaps
to detect pre-existing grains. They work with sustained sounds, drones, and
ordinary recordings. They are offline CDP processes; their controls are separate
from the live granular pedalboard effect.

**Granular Pitch** shifts the contents of overlapping grains while keeping roughly
the same overall duration. Compare it with Tape Transpose, where pitch and duration
change together. **Granular Time** keeps pitch while changing duration, using CDP's
native **velocity** parameter: 0.5 makes roughly twice the duration; 2 makes roughly
half. Compare its grain texture with Spectral Time on a sound with clear attacks.
Zero velocity is excluded because it requires an explicit output-duration workflow.

**Grain Scramble** chooses grain material from behind the advancing source position.
Grain length controls the size of each fragment. Lookback controls how far into the
past it can reach; zero keeps the normal source progression, with grain scatter
still active. The lookback range must fit within twice the source duration, CDP's
native bound. This is backward local searching, not a full-file random permutation.

**Grain Density** changes how closely the grains sit together. Values below one
leave gaps; larger values overlap them. Try 0.25 and inspect the result waveform,
then hold a QWERTY note with Loop enabled. Compare against 2 for a denser texture.
Density changes overlap and gaps, rather than acting as a duration multiplier.

Pitch, Time, and Density use CDP's fixed 50 ms grains. All four modes use 5 ms
start/end splices and native random scatter of 0.5 of the output hop. These are
fixed settings of the selected CDP modes, not hidden adjustable Portal controls.
An exact recipe or pin preserves parameter settings, **not an identical random
render**. Overlapping grains can boost the level; check the existing peak report
and use Source/Result auditioning to compare.

Input must be mono, at least 40 ms, and long enough to supply a complete grain.
Pitching upward reads more source frames for each output grain, so it may need a
longer selection. Grain Scramble requires a grain shorter than the source, with
room for its splices. Invalid combinations report an explanation before CDP runs.
Output length is checked using CDP's rounded input/output hops, final grain, and
maximum scatter against the eight-million-frame Portal limit. Grain boundaries,
source-end handling, and scatter mean durations are approximate, including for
pitch-only processing. The slider bounds above are Portal limits within CDP's
native ranges. Breakpoint files and stereo spatialisation remain future work.

## Preview and learn

- **PREVIEW / Enter** runs CDP in the background. The same button cancels a job.
- Drag sliders; wheel a parameter for fine increments; click its number to type
  an exact value and press Enter. Escape abandons number editing.
- **PLAY / Space** starts or stops auditioning. After completion, Space restarts.
- **SOURCE / RESULT / A/B / Tab** select or switch audition sources. Switching
  aligns elapsed time in seconds; it does not pretend unequal files have equal
  duration. An out-of-range position restarts in the destination's valid range.
- **LOOP** repeats the current audition range.
- **QWERTY notes:** the same two keyboard rows as Main play the selected
  **SOURCE** or **RESULT**, with up to five simultaneous notes. **C4 plays the
  original pitch**, C5 is an octave up. **F1–F8** select keyboard octaves
  (F5 selects C4); held notes keep their pitch when the octave changes.
  Each note starts at the selection's beginning, or the whole sound's beginning
  without a selection. It stops on key release or at the end when Loop is off.
  With Loop on, it repeats while held. Starting a note replaces ordinary Play
  auditioning; Space/Stop clears the whole preview chord.
- **A/B with held notes** preserves each note's pitch and matches elapsed source
  position, clamping to the destination selection when needed. Live loop-range
  editing also applies to all held preview notes. The playhead follows the most
  recently started active note. The KEYS line shows octave and active voice count.
- Search, name, numeric fields, and the collection manager consume typing without
  starting notes. Releasing a held key still stops its note after focus moves
  into one of these fields. Losing window focus, closing the Portal, replacing
  its source/result, and changing process settings release preview voices.
  Playing notes does not alter rendered audio or saved recipe parameters.
- Click a waveform to place its playhead; drag to select an audition range and
  move the playhead to its start; right-click clears that range.
- **Alt+wheel** over a selection expands (up) or contracts (down) the edge on
  that side of its center, using the canvas's zero-crossing steps. While looping,
  wheel resizing and dragging update the audible range without stopping or
  restarting the voice. The playhead stays where it is while inside the new
  range; otherwise it moves to the new start. Clearing the range keeps the whole
  waveform looping. Editing a stopped preview does not start playback.
- Wheel over either waveform to zoom; Shift+wheel pans; **FIT** restores both
  full views. These waveform selections are audition-only. Processing uses the
  snapshot identified by SOURCE/SEL and the status line.
- **APPLY** replaces the original snapshot range, preserving normal tile undo.
- **NEW TILE** copies the result into an empty slot on the current sample page.
  A full page is reported without overwriting anything. Like the existing
  copy-to-new-tile helper, the new tile receives a whole-sample forward loop.
- **MAIN: CTRL+Z** returns to Main, where Ctrl+Z undoes the last tile edit.

Rendering never changes audio automatically except when explicitly invoking a
main-page process pin's left-click quick apply. Edited parameters invalidate the
result and cancel an outstanding job. Apply checks the original tile, sample
page, and audio hash; it rejects a stale result. Preview auditioning uses the
existing playback path and global output/limiter controls.

The history strip retains up to four rendered variants for the current source.
Click one to restore its settings and audio. History is session-only and resets
on source reload. Aggregate history storage is bounded to eight million mono
frames (32 MB); oldest results are evicted first. Each source and result is also
bounded to eight million frames. This permits about 181 seconds at 44.1 kHz;
choose a smaller main-canvas selection for longer recordings. Expanding
processes also preflight their output estimate. CDP timeouts remain enforced.

## Save a recipe or make an instrument

Click the name field to name a recipe. **SAVE AS** adds its exact current
settings to the saved browser. A saved recipe contains no source audio or
source file paths, so it can be applied to a different waveform.

The **PIN** checkboxes beside parameters select which controls the user-made
instrument exposes. Choose **PIN: CHECKED MACROS** or **PIN: EXACT RECIPE**,
select an empty pin slot, then click **PIN PROCESS TILE**. An exact pin fixes all
values; a macro pin exposes only the checked controls. **FULL** reveals the
underlying controls again. Macro names are the real CDP parameter names in this
first release; custom macro naming, range remapping, and multi-parameter macros
are future work.

Back on the main CDP panel, **PINS** switches between factory instruments and a
separate pair of 16-slot user-pin pages. Left-click a filled pin to render and
apply its stored settings (the Portal opens so cancellation/errors remain
visible). Middle-click to explore its exposed controls before rendering.
The factory bank and sample tiles are not overwritten by saving pins.

There are 32 saved-recipe slots and 32 process-pin slots. **SAVE AS** still adds
to an empty saved slot; pinning to an occupied slot directs you to the manager.

### Manage the collection

![Native collection manager showing an explicit update of pin 03](images/cdp-portal-manager.png)

Click **MANAGE TOOLS** below the process list. Choose **SAVED** or **PINS**, then
a numbered destination. Wheel the list or use Previous/Next to reach all 32
slots. If you loaded a saved recipe or pin, its slot is selected initially.
The manager takes a snapshot of your current working recipe when it opens.

- **Rename:** click the name field, edit it (Ctrl+A clears it), then Rename.
  This changes only the destination's name.
- **Update:** copy current parameter values and macro choices into the selected
  slot while keeping its name. The process must match; for a different process,
  use Replace. Edit controls on the main Portal page before opening the manager.
- **Replace:** put the complete working recipe, including its name and process,
  into the selected slot. This also fills an empty slot.
- **Remove:** empty just that slot. Other slots keep their numbers; no audio or
  factory instruments are removed.

For pins, Update and Replace honor **EXACT RECIPE / CHECKED MACROS** from the
Portal page. Each action shows its destination and requires **CONFIRM**.
**CANCEL** or Escape abandons the pending action; Close returns to the Portal.
Changes are persisted before the in-memory collection is updated. If saving
fails, both the existing collection and the previous file remain intact.

`cdp-portal.recipes` is saved beside the active `tapesister.ini`, using a temporary
file and atomic replacement. It is an application-level personal collection,
not embedded in a `.tsr` project. Copy this file alongside the INI when moving
your personal configuration. Source audio, rendered history, and temporary
analysis files are not included. Invalid/unknown-version files are rejected
transactionally rather than partially loaded.
Saving is blocked after an unsuccessful load so an unreadable existing collection
cannot be overwritten accidentally. Back up and repair that file, then restart.

## Architecture and verification

- `cdp_portal.h` / `ts_cdp_portal.c`: stable process IDs, typed parameter metadata,
  validation, shell-free command generation, recipe persistence, waveform model.
- `ts_cdp_adapter.c`: Portal and factory jobs share the isolated process runner,
  cancellation, timeout, output probing, and diagnostic/cleanup handling. Only
  compile-time registered commands enter the runner; stored recipes cannot name
  arbitrary executables, paths, or shell expressions. Existing 16-bit WAV
  staging is retained; this is not a new high-resolution import/export path.
- `ts_cdp_portal_ui.inc`: native 640×400 renderer, palette accents, bounded labels.
- `main_sdl_portal.inc`: immutable worker ownership, history, audition pointers,
  input handling, main-panel pin actions, stale apply prevention.

Registry metadata is checked against the supplied CDP8 source
(`dev/distort/ap_distort.c`, `dev/blur/ap_blur.c`, `dev/stretch/ap_stretch.c`,
`dev/modify/ap_modify.c`, `dev/modify/brapcon.c`, `dev/modify/granula1.c`,
`dev/filter/ap_filter.c`, `dev/filter/filters0.c`,
`dev/filter/fltpcon.c`, `dev/include/filtcon.h`, `dev/cdp2k/tklib1.c`, `dev/include/speccon.h`,
`dev/include/modicon.h`, and `dev/pv/pvoc.c`). The process/mode IDs and
names are distinct from the illustrative SCRAMBLE controls in the concept art.
`distort`, `pvoc`, `blur`, `stretch`, `modify`, and `filter` are already in the bundled runtime closure; no runtime dependency
or audio backend change is needed. No SoundThread code or descriptions are
copied into this implementation.

Tests:

```sh
make test
make tapesister_portal_controller_tests
make tapesister_preview_loop_tests
./tapesister_preview_loop_tests
TS_TEST_CDP_BIN=/absolute/path/to/cdp/bin ./tapesister_portal_tests
TS_TEST_CDP_BIN=/absolute/path/to/cdp/bin ./tapesister_portal_controller_tests
```

The core tests exercise every registered mode, recipe validation/persistence,
waveform bounds, and (with TS_TEST_CDP_BIN) real renders and cancellation.
The controller harness exercises the actual SDL application controller,
including stale page/audio rejection, apply/undo, control changes during a job,
selection boundaries, close/cancel, history eviction, pointer detachment, occupied-pin preservation,
new-tile copying, and stale-source clearing. Without TS_TEST_CDP_BIN the
controller harness prints that real-CDP checks were skipped.
The standalone preview-loop test requires SDL but no CDP installation. It covers
live Alt+wheel and drag edits, callback continuity, range clearing, tiny loops,
and stopped/non-looping behavior in Portal and mono/stereo import previews.
It also exercises native input routing (typed values, waveform drag, middle-click
pin editing, and left-click quick apply) and audition range/A-B ownership using
paused dummy SDL devices; it does not open physical audio hardware.

To render a screenshot of actual CDP output:

```sh
TS_TEST_CDP_BIN=/absolute/path/to/cdp/bin ./tapesister_portal_tests portal.ppm manager.ppm
```

### Grains-family verification record

All 30 process defaults and the four new modes' parameter endpoints rendered
through source-built CDP at 44.1 and 48 kHz. Steady-tone measurements verify
up/down granular transposition with approximately preserved length, and time
stretch/compression with retained pitch. Sparse-density renders contain more
measured silence than dense renders. Short-source and 22.05 kHz renders also
passed. Validation covers grain/splice fit, pitch-dependent source length,
lookback limits, finite input, positive velocity, and bounded output length.
Recipes and exact/macro pins round-trip through the existing collection format.

The actual SDL controller test selects the new family, renders Grain Density,
auditions its result as a note, saves an exact pin, and releases the note on
invalidation. Existing collection/history/apply and QWERTY/live-loop regressions
passed. Portal and controller code passed ASan/UBSan with leak detection disabled
because LeakSanitizer is unsupported here. Updated native 640×400 screenshots
show a real density render. Windows compilation and physical-device listening
remain native validation steps.

### Filter-family verification record

All 26 process defaults rendered with source-built CDP binaries. The six filter
modes passed scalar endpoint checks at 44.1 and 48 kHz, including explicit tail
lengths. Three-tone measurements distinguish notch, band-pass, low-pass, and
high-pass behavior; independent acuity/frequency corners also rendered. Sweep
rate/phase and phasing gain changes produced different audio. Validation covers
sample-rate limits, delay versus source duration, finite input, positive tails,
sweep ordering, and the aggregate output limit. Filter recipes and pin macros
round-trip through the existing collection format.

The actual SDL controller test covers reaching the new family, scrolling to
controls 5–7, exact phase entry, pin checkboxes, sparse macro mapping, and a real
sweeping-band preview. Existing collection/history/apply and QWERTY/live-loop
regressions passed. Portal and controller code passed ASan/UBSan; leak detection
was disabled because LeakSanitizer is unsupported here. The screenshots show
actual native 640×400 rendering. Windows compilation and physical-device listening
remain native validation steps.

### QWERTY / time-family verification record

The actual SDL controller/callback harness covers source/result notes, pitch
ratios and octave changes, five-note chords, key repeat, typing isolation,
releases after text/manager focus, A/B with held notes, live loop range edits,
Space, focus loss, process invalidation, and close. Preview audio uses the sample
bus; it is not classified as FM. A Current-tile sync cannot retarget the immutable
preview voices. The worker/history harness also checks notes started during a
render are detached before result storage changes.

All 20 process defaults and the four time/tape modes' scalar endpoints rendered
using source-built CDP binaries at 44.1 and 48 kHz. Duration ratios and reversed
sample ordering were checked. The Portal, controller, and keyboard/loop harnesses
passed ASan/UBSan with leak detection disabled. Core instrument, stereo note-bank,
MIDI, audio-hardening, and companion-focus regressions passed. Windows compilation
and listening through a physical audio device remain native validation steps.

### Collection / spectral verification record

The follow-up passed registry and persistence tests, actual SDL collection-manager
events, and the existing controller lifecycle suite. Checks cover duplicate names,
filtered slot identity, rename/update/replace/remove, cancellation, failed saves,
and loading the resulting collection. The previous live-preview-loop regression
also passed. Manager/controller, Portal core, and live-loop harnesses passed
AddressSanitizer/UndefinedBehaviorSanitizer with leak detection disabled because
LeakSanitizer is unsupported in this environment.

All 16 process defaults rendered using binaries built from the supplied CDP8
source. The four spectral processes also rendered at both parameter-range ends
at 44.1 and 48 kHz (blur's upper endpoint adjusted to the source-length limit).
Cancellation during the middle spectral stage and invalid final-output handling
both passed cleanup checks. Core instrument tests and audio-hardening / companion
focus structural checks passed. The images above were rendered by the native
640×400 UI with real CDP output, not a concept mockup.

### First-slice verification record

The Linux development run passed all 69 Make test executables and the four
structural/packaging checks. The 12 modes were additionally rendered using
`distort` compiled from the supplied, pinned CDP8 source. The native SDL
application compiled, and the controller harness passed both normally and
with AddressSanitizer/UndefinedBehaviorSanitizer. LeakSanitizer is unsupported
in this runner, so leak checking was disabled for that sanitizer run.

CMake, Windows packaging, and physical-device listening checks have not been
run in this environment. Before release, verify the Windows bundle and listen
to source/result A-B, selection joins, and pin quick apply on real hardware.
