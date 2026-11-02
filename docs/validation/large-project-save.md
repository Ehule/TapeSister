# Large portable project saving

## Scope and evidence

Audited the connected repository, not the uploaded source archives. `main` was
`4a4c8468274685b6fa476a3bc6c8835de971fb5b`; open PR #171
(`feature/mosaic-gallery`) was at
`bd06f94c34ffa69b8a69110ddd8ede0d6027d0c7`. Folder import belongs to that PR,
so this fix is based on it. The CDP8 archive is the external DSP tool collection;
SoundThread is a separate Godot CDP interface. Their ZIP entry dates are June 8
and January 30, 2026, respectively. Neither is the current native TapeSister save
implementation or a verified match for the affected executable.

The supplied JSONL contains 16,275 valid records over 51.845 minutes. Its two save
requests are 527.919 seconds apart, both with 28 stopped Mosaic cards and the same
Mosaic hash. Peak resident memory is 6.71 GiB, with about 11.36 GiB of system memory
still available. This does **not** establish global memory exhaustion. Audio
records continue; there is no saved-result event identifying the failed operation.
The absent session-end record does not establish a crash.

The JSONL build string is `Oct 31 2026 16:48:52`; the separately pasted report says
`Oct 31 2026 16:49:52`. That one-minute difference is preserved and does not prove
different revisions. The earlier report's running eight-card arrangement is a
different moment from the stopped 28-card save requests.

**The exact terminal error in the original Windows session is still unknown.**
There is no evidence establishing a 1 GiB limit, a particular disk/permission
error, allocation failure, or a specific rename failure in that session.

## Confirmed defects and changes

| Source path / function | Confirmed behavior before this change | Resulting change |
| --- | --- | --- |
| `src/main_sdl.c`, `run_pending_file_operation` | The UI thread executes the complete save synchronously. Its animation is presented before entering the blocking operation. | Worker uses a metadata snapshot and a pinned immutable document; input/controller mutations pause until join. Disk work and serialization do not run under the audio lock. |
| `src/ts_sample.c`, `put_sample_block`, `put_float`, `put32`, `put16` | Every float becomes four individual `fputc` calls, followed by another checksum scan. | Bounded 64 KiB little-endian blocks retain identical wire bytes. A bounded 256-entry checksum cache belongs to one immutable save snapshot and is discarded before edits resume. State hashing is reported and cancellable before commit. |
| `src/ts_sample_pages.c`, `validate_saved_instrument` | Validation reloads a whole decoded bank and materializes its selected tile. | The same native metadata/parser checks stream PCM/checksums without allocating decoded validation audio or rendering a selected tile. |
| `src/ts_sample_pages.c`, failure and retry paths | Ordinary failure removes the owned staging directory; retry can delete an owned interrupted stage. Temporary bank output can also be deleted after a failed publication. | Retain incomplete output under a fresh failed-attempt name. Preserve earlier interrupted stages before retry. Keep backup/stage protection for failed rollback and user-file moves. Surface retained cleanup locations after a successful publication. |
| `src/ts_sample.c`, WAV16 finish | Prior stream errors are not checked independently of `fclose`. | Check writes, stream error and close; surface I/O details. Fault injection covers an error flag with an otherwise successful close. |
| `src/main_sdl.c`, save completion | A failed Save & Exit clears exit flags only in the browser-open error branch. | Clear busy/exit-after-save flags on failure with a closed browser too. Close during saving cancels before commit or finishes a successful commit; unsaved work receives confirmation. Later state changes remain dirty. |
| `src/main_sdl.c` and `src/main_sdl_project.inc`, Close/New prompts | Dirty checking rescans every PCM sample on the UI thread. | Use the revision fingerprint already used by the project title. |
| Native sample loader/writer | Loader limits a sound to 100,000,000 frames, while the writer does not preflight that constraint. | Check banks before writing and identify the offending tile. This existing per-sound constraint is separate from aggregate file size. |

These source defects explain blocking behavior and possible disappearance of
staged output. They do not identify which error caused the user's original save
to fail. The new independent Health journal records that distinction for future
reproductions: start, stage, asset, progress, result, error and recovery location,
including snapshot/thread startup failure. Session recording need not be enabled.

## Portable format and integrity

TSR41 already stores full float PCM for every occupied tile, its edit baseline
and applicable patches. Project saving also produces the existing PCM16 sample
collection, Mosaic assets, tracker data and Sister state. This explains both a
large TSR and an initially empty sample directory: the banks are serialized and
validated before the separate WAV collection is written. Neither observation
alone demonstrates a bug.

All imported sounds remain included, even when unused by the arrangement.
Original import files remain untouched. The TSR41/project-manifest versions are
unchanged. New optional `reuse=` records supplement the existing managed-file
inventory; older readers ignore them. Reuse requires an exact source signature
and a verified old managed-file fingerprint. Sub-24-bit float edits are tested;
the quantized audition/native checksum is not the asset-reuse identity.

Verified files use sibling hard links when available, otherwise bounded copying.
The saver never writes through a reused link. Bank metadata changes can still
rewrite that bank's embedded PCM. The existing one-write-per-shared-Mosaic-source
behavior remains; there is no new cross-bank asset-store format. Repeated saves
still perform integrity reads and hashing, so zero rewritten audio does not mean
zero work. This is a compatible save fix rather than a new persistence format.

The old valid bundle is retained until staging validates and closes. User-added
entries retain the existing exact ownership/conflict checks and rollback rules.
No arbitrary selected user directory is recursively removed. Ordinary failed
attempts are marked incomplete and retained; a retained folder is not claimed to
be a completed project. Rollback ambiguity keeps both protected locations.

## Executed validation

Linux Release application/core built with SDL and system FFmpeg. Bundled CDP/video
builds and physical MIDI were disabled locally. The PR manifest registers 38
regressions on Windows and Linux; local execution passes all 38. Native release
CI also includes the added codec, portable-save and worker tests. Check the PR's
CI results for Windows/release status; the measurements below are Linux results.

The explicit large fixture imports 75 stereo sounds with **1,650,000,000 decoded
PCM bytes**, five banks, 28 cards and four independent loop cards sharing one
Mosaic source. These are synthetic assets, not a reconstruction of the unknown
original file formats. After saving, the source model is freed and the bundle is
relocated before reopening and checking all 75 exact audio signatures and Mosaic
state. No original import directory is available or needed.

Representative measurements on this host:

| Case | Wall time | Encoded audio payload written |
| --- | ---: | ---: |
| Initial core transaction | 22.185 s | 4,147,000,000 bytes |
| Unchanged core save | 24.576 s | 0 bytes |
| Small arrangement edit | 24.109 s | 0 bytes |
| One exact sample edit | 27.962 s | 484,000,000 bytes |
| Full application save worker, including state fingerprinting | 44.022 s | Complete portable bundle |

The payload counter excludes small headers/manifests/state files; it is not total
filesystem traffic. The sample edit rewrites its containing native bank. Initial
core fixture peak RSS is 3,547,060 KiB; the application worker run's kernel child
peak is 3,595,924 KiB (about 3.43 GiB). These are different workloads from the
original Windows session and are not a claimed like-for-like RAM reduction.

The worker fixture runs the actual application callback through the logical dummy
audio backend at 48 kHz / 512 frames. Its large run advances 4,448 callbacks while
servicing the controller and software framebuffer. Maximum observed control hold
is 4.879 ms, acquisition wait 0.002 ms; the longest call site is snapshot capture.
Snapshot destination pages are now touched before entering that short lock.
The worst controller/paint iteration under concurrent host load is 37.233 ms;
this is responsive servicing, not a 44-second blocked UI call. This test is not a
hardware dropout guarantee or a WASAPI/MOTU reproduction.

Additional executed checks:

- Native TSR files of **2,147,490,396** and **4,294,980,668 bytes** stream and
  validate correctly. The fixture borrows one 128 MiB PCM buffer across distinct
  slots to exercise real large-file I/O without requiring enormous test RAM.
- An **unmodified PR #171 reader** opens the new 75-sound bundle: five banks,
  all 75 sounds and the saved Mosaic hash. Existing legacy/project-reopen,
  Gallery, loop, folder-import and tracker regressions also pass.
- Stream/write/close failure injection, ENOSPC write failure, validation
  corruption, prepare/publish failure, rollback failure/ambiguity, interrupted
  user-entry moves, protected user files/folders, links and retained retries.
- Cancellation at bounded codec blocks, repeated Save, Close/cancel, active
  callbacks, failure with browser closed, independent result journals, and a
  state change after snapshot capture remaining dirty instead of being discarded.

Large fixtures run in isolated temporary storage. A preliminary run in the
automatically synchronized workspace encountered recreated staging directories
and rsync temporary files; it was excluded from these measurements. Copied test
executables/working directories were also used to avoid synchronization effects
on the local regression harness.

Reproduce the explicit stress checks in disposable directories:

```sh
tapesister_portable_save_tests --large
tapesister_save_codecs_tests --boundaries
tapesister_new_project_tests --save-worker
tapesister_new_project_tests --save-large-worker /path/to/relocated/portable.tsr
tapesister_new_project_tests --profile-library /path/to/relocated/portable.tsr
```

## Separate Mosaic performance investigation

For the same large imported library and fixed small arrangement, 200 software
paint + revision-fingerprint iterations average **0.826 ms** (peak 1.331 ms).
Restricting the fingerprint to one bank averages **0.728 ms** (peak 1.151 ms).
The large project's Close confirmation takes **1.013 ms** with revision checking.
Inspection finds no per-frame scan of all imported PCM in this path. The saved
arrangement's source peaks are independent of the inactive bank collection.

This does not reproduce or explain the milder Windows rendering slowdown. The
software-framebuffer fixture does not measure that machine's texture upload,
GPU/present/VSync path or memory scheduling. Target-machine profiling of those
stages remains separate work; no shared cause with the save failure is asserted.
Windows MOTU/WASAPI behavior, original drive characteristics/free space and the
original process's allocation constraints still require target evidence.
