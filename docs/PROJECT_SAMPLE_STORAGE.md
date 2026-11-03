# Shared project audio

## Project format

New project saves use **TSR42**, project manifest **3**, Mosaic **19** and pattern
asset manifest **3**. The existing bank/editor, routing, undo/redo, patch, Gallery,
loop, tracker and Sister metadata are retained. Audio blocks contain dimensions,
the per-use sample name and a 64-character SHA-256 asset ID instead of inline PCM.

```
Example/
  Example.tsr
  manifest.txt
  sister-state.ini
  project-data/
    page-002.tsr
    record-bank.tsr
    mosaic.tsm
    mosaic-000.tsa
    sister-tracker.tst
  samples/
    audio/<sha256>.wav
    page-001/01_Sound.tsa
```

The layout above is illustrative: optional banks/canvases and their assets exist
only when needed. A `.tsa` is a small binary reference, not another audio copy.
Canonical WAVs are ordinary interleaved IEEE float32 WAV files. Their SHA-256
identities cover their complete canonical little-endian WAV bytes, including
rate, channels and frame count. Exact float differences and signed zero survive;
non-finite values retain the existing writer's finite-zero sanitization.

Names, tuning, loops and routing belong to each use and do not prevent identical
audio from sharing an asset. A bank's rendered sound, baseline, patches, Mosaic
sources, editable EQ originals and pattern audio all use the same project store.
Genuinely different edited audio versions remain separate. Every imported bank
sound is retained, including unused sounds. Original import files are untouched.
The sample manifest remains an index of per-tile names, tuning and loop metadata.

## Incremental save and integrity

Bank files, arrangement data and small reference files are regenerated. Only new
audio identities encode new PCM. Unchanged managed WAVs are verified against the
previous ownership inventory and reused through sibling hard links, with bounded
copying on filesystems without hard links. Never modify a linked asset in place.
Source identities and exact-buffer comparisons are cached only for one pinned,
immutable save snapshot. They are not cached across edits.

Newly written WAVs are checked against the source's canonical header, dimensions
and exact-byte file checksum. Reuse requires that same checksum to match both
the old source record and the verified managed file. Opening checks the full
SHA-256 of every distinct referenced WAV once per load and rejects missing,
truncated, changed or non-finite audio. IDs accept only 64 lowercase hexadecimal
characters; file paths are constructed inside the project's `samples/audio`.
The transaction reuses the existing ownership, rollback, cancellation and retained
failed-attempt protections. A version 3 bundle requires its managed-file inventory
for replacement. A TSR42 project without its version 3 manifest is rejected,
preventing an accidental page-one-only open.

Zero encoded payload does not mean zero work: saving still fingerprints source
PCM and checks existing files. The bounded snapshot cache avoids hashing identical
cloned buffers repeatedly. On filesystems without hard links, reuse must copy the
WAV bytes into staging. The progress payload counter includes those fallback
copies, but excludes small metadata files, headers and integrity reads.

## Legacy compatibility

The original TSR6–TSR41 decoding paths remain. Old standalone banks, flat projects
with `.tsr.samples` companions, project-manifest-2 bundles and pre-Gallery Mosaic
formats open through those readers. Opening does not convert or overwrite files.
The next project Save writes the shared format; Save As keeps the old file/bundle
as a separate copy. In-place conversion uses the same validated transaction as a
normal save. A failure keeps the previous valid project and retained recovery
material.

New project saves require this version of TapeSister. Earlier executables do not
understand TSR42. The low-level standalone bank export remains self-contained
TSR41. Move/copy an entire new project folder, rather than only its small TSR.
Gallery/canvas import, export, recovery copies and Sister state lookup support
both project manifest versions.

## Loading and live playback

This migration changes persistent storage and incremental saving. Samples are
still preloaded into independently owned RAM buffers before publishing a project;
there is no new disk access on the audio callback. It does not introduce disk
streaming, lazy bank loading or shared writable PCM ownership. Those require a
separate bounded preloading/cache design for live, reverse and granular access.

## Validation

`tapesister_shared_audio_tests` explicitly migrates TSR31, 34, 35, 37, 38, 39,
40 and 41 into TSR42 and reopens them, checking exact stereo samples, signed zero,
sub-24-bit edits, baseline audio, stable tile IDs and unchanged original files.
It also checks an independent SHA-256 known answer, shared bank/Mosaic/collection
audio, arrangement-only reuse, one-sound incremental writes, standalone export,
patch audio and working undo/redo after reopening, and transactional failure on
missing/corrupt audio or a missing project manifest.
The pre-existing flat/split-project, routing, editor, Gallery, canvas transfer,
pattern, worker, cancellation and filesystem fault tests remain regression gates.

The 75-sound synthetic fixture contains 1,650,000,000 bytes of distinct stereo
PCM, five banks and 28 Mosaic cards sharing one source. A Linux run measured:

| Save | Wall time | Encoded audio payload |
| --- | ---: | ---: |
| Initial | 13.004 s | 1,650,000,000 bytes |
| Unchanged | 16.248 s | 0 bytes |
| Arrangement edit | 15.232 s | 0 bytes |
| One sample edit | 16.152 s | 22,000,000 bytes |

After freeing the model, the test relocates the bundle and verifies all 75 sounds,
including unused sounds, and the Mosaic state. Peak RSS is 3,546,996 KiB: this is
not a RAM-loading optimization. The earlier TSR41 run of the same fixture wrote
4,147,000,000 bytes initially and 484,000,000 bytes for the sample edit. The
payload reduction is about 60%; these host timings are not Windows benchmarks or
a predicted duration for the user's project. Run `tapesister_portable_save_tests
--large` in a disposable local directory to reproduce.
