# CDP Portal — explore, save, and manage your tools

![Native CDP Portal rendering with a real tape-vibrato result](images/cdp-portal.png)

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
delete modes 1/2/3, and reform mode 5. Four spectral and four time/tape modes bring
the Portal to 20 processes. Search matches names, stable command IDs, descriptions, and
families. **ALL**, **SAVE**, and **PINS** switch the left browser between
processes, saved recipes, and user process pins. Scroll that column with the
mouse wheel. The family button below the tabs cycles **ALL FAMILIES**,
**WAVESET**, **SPECTRAL**, and **TIME / TAPE**. Family and text filters combine, including in
saved recipes and pins. Clearing the search and choosing ALL FAMILIES restores
the complete list. Filtering never renumbers stored slots.

These 20 Portal modes accept mono input only; stereo is rejected explicitly.
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
`dev/modify/ap_modify.c`, `dev/cdp2k/tklib1.c`, `dev/include/speccon.h`,
`dev/include/modicon.h`, and `dev/pv/pvoc.c`). The process/mode IDs and
names are distinct from the illustrative SCRAMBLE controls in the concept art.
`distort`, `pvoc`, `blur`, `stretch`, and `modify` are already in the bundled runtime closure; no runtime dependency
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
