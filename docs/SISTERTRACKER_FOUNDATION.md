# SisterTracker: stage 1 foundation

This document describes the original native score/identity foundation. F10 now
uses the embedded TapeHead application described in
[SisterTracker playback](SISTERTRACKER_PLAYBACK.md). The stable model remains the
host project boundary; a version-2 extension retains the original tracker bytes.
The earlier notes about pending Song/M/N/FX execution are superseded by the
embedded replayer.

## Model

`TsSamplePages` owns a `TsSisterTracker`. A new or legacy project starts with an
empty song, BPM 125, six ticks per line, Loop enabled, no CONTROL lane, unity
ratios, and Main routing. There is no running state in the saved model.

- Eight song-wide lane definitions: name, LEN OFF/1–256, Standard/Pattern/Song,
  one of the frozen 17 ratios, direction, 0–200% trim, mute, CUT/OVERLAP, and
  logical output assignment. Solo will belong to transient runtime state.
- Up to 256 patterns and 256 order entries. Pattern IDs increase independently
  of their list position and are not reused after deletion. Repeated order
  entries share a pattern; copying makes a new pattern with independent storage.
  An ordered pattern cannot be deleted until its order references are removed.
- Patterns have 1–256 physical rows. All 256 rows remain stored when a pattern
  is shortened, copied, or saved. Create an ordinary pattern with 64 rows.
- Cells distinguish NONE, a MIDI pitch (including 0), OFF, and CUT. Tile ID,
  volume presence and 00–40 value, M/N tuning, and FX command/value are distinct
  fields. ASCII `0` and `Z00` remain actual commands. Valid but unsupported FX
  commands are preserved; no effect execution is enabled in this stage.
- Aliases 01–FF map to up to 255 referenced tiles; 00 is reserved. A referenced
  tile must have an alias, but it may be missing from the Sample bank. Missing
  IDs are saved unchanged and can never fall back to an occupied slot.

The model also saves initial tempo, ticks per line, restart order, Loop, CONTROL,
LEN options, random seed, and editor position/step/follow. Output assignments are
definitions only: Main owns channels 1/2 and a direct destination starts at 3.
Actual device capacity, Insert reservations, routing ramps, and voice overlap
limits belong to the later runtime/routing stages.

## Tile identity

Each occupied `TsBankSlot` owns a nonzero 64-bit `TsTileId`, separate from its
runtime audio generation, waveform revision, page, and slot. IDs are scoped to
the project in which they are resolved.

| Operation | Identity |
| --- | --- |
| Edit, crop, rendered replacement, import onto an occupied tile, FM Create/Apply onto an occupied tile | Retain |
| Create in an empty slot, capture to a new tile, duplicate, copy from another bank/project | Allocate fresh |
| Page switch, park/unpark, full instrument/history snapshot | Preserve |
| `ts_sample_pages_move_tile` into an empty slot, including another page | Preserve with content |
| Clear, then refill the same slot | Old ID becomes missing; refill gets a fresh ID |

The existing tile edit Undo/Redo keeps identity. A full prepared instrument
snapshot also retains identities for bank-level history restoration. This stage
does not add a new UI command or Undo history for clearing or moving tiles.

Cross-project/bank copying uses `ts_instrument_copy_bank_slot_from`, which
explicitly assigns a fresh destination ID. Importing score data from another
project will need an explicit source-to-destination remapping in a later stage;
foreign numeric IDs must never be resolved directly against the current project.

`ts_sample_pages_find_tile` resolves against all Sample pages, including the
parked bank during Record Bank mode or Mosaic event editing. Record Bank and
Mosaic event material are not substitute tracker sources. The registry checks
duplicate IDs across pages before save or load. This control-thread lookup is
deliberately uncached, so slot changes cannot leave a stale mapping; the audio
runtime will need prepared source handles, not calls to this lookup per sample.

New IDs come from a process-wide atomic allocator. Successfully loaded IDs and
missing score aliases reserve their range so allocation cannot resurrect a saved
reference. Zero and UINT64_MAX are invalid; exhausted allocation fails rather
than wrapping. Instrument cloning preserves IDs and is for snapshots/history,
not for importing independent tiles.

## Native format and migration

New native bank files use **TSR32**, adding an explicit little-endian tile ID
after each occupied-slot flag. Existing TSR6–TSR31 readers remain supported and
assign identities during migration. Audio, recipes, and existing edit histories
keep their earlier serialization. Older TapeSister binaries cannot read TSR32.

The project bundle adds `project-data/sister-tracker.tst`, with the eight-byte
`SISTRK`, version 1 header and explicit little-endian fields. No C struct layout,
padding, pointers, playback phases, live effect memory, or voices are serialized.
The manifest declares `sister_tracker=1`, making a missing sidecar a load error.
Older manifests and standalone native bank files without tracker data create
empty default definitions. Unsupported versions, trailing bytes, truncated data,
invalid commands, aliases, IDs, row/order limits, editor positions, or nonfinite
trim values fail validation.

Save validates definitions and the tile registry before staging, writes and
reloads tracker data inside the existing bundle transaction, then replaces the
bundle. Load prepares all pages, record material, tracker data, and Mosaic before
replacing the active project. A tracker failure leaves current project content
intact. Missing sources are valid score references, not load errors. Loading
never starts SisterTracker or changes the audio device.

The canonical field encoder also computes the tracker dirty-state hash; tile
IDs participate in the existing instrument dirty-state hash. No new code is
called from the audio callback.

## Developer API and checks

See `include/tapesister/sister_tracker.h`, `tile_id.h`, and `sample_pages.h`.
Initialize owned models before clone/load; release them with the matching free
function. Model mutations, snapshots, validation, file I/O, and tile moves are
control-thread operations and may allocate. Direct field edits must be validated
before publication or save. The standalone tracker file writer is a codec; use
the project bundle API for transactional user saves.

`tapesister_sister_tracker_tests` covers shared and copied patterns, hidden rows,
resource limits, aliases, MIDI 0/Z00, typed validation, deterministic hashes,
copy/remap semantics, edit Undo/Redo, clear/refill, snapshot restoration, moves
with unsaved audio, page changes, parked/Mosaic lookup, multi-page round trips,
missing-source persistence, corrupt or missing sidecar rollback, duplicate tile
IDs, and TSR31 migration. Its checks and setup run in Release builds too.

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R 'sister_tracker|sample_pages|tsr27|core_tests'
```

The native Linux and Windows PR workflows include the SisterTracker test.
