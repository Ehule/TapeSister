# Audio Health session recording

TapeSister records a local diagnostic timeline automatically during ordinary work.
Open a project and use it normally. Health (F12) does not need to stay open, and
PROFILE does not need to be enabled. Graphics quality and audio processing are
not changed by the recorder.

## Collect a session

1. Work normally. If you hear a click, optionally press **Ctrl+Shift+F12** in any
   TapeSister host window, or **HEARD A CLICK** in Health. This records your
   observation; it does not automatically detect a click in the audio waveform.
   The separate visualizer window does not receive the host shortcut.
2. Save your TSR project. Each successful **Save** or **Save As** also saves a
   diagnostic snapshot in a **`logs` folder beside the actual `.tsr` file**, inside
   its project bundle. For example, `My Show/My Show.tsr` gets
   `My Show/logs/health-session-….saved.jsonl`. Save As uses the new location.
3. In Health, **SAVE SESSION** updates that snapshot without saving project edits
   and copies the log's file path to the clipboard when ready. Ordinary project
   saves leave the clipboard alone. **LOG FOLDER** opens the active project's
   `logs` folder. For an unsaved project both buttons use the recovery folder.
   Attach the `.saved.jsonl` file for analysis.
   Playback and the original live log continue after saving. Saving again updates
   that session's saved copy. Save once more at the end of your work to include
   later events. A different application session creates a new file. Copies beside
   projects are not automatically deleted; copy elsewhere for additional snapshots.

Health shows RECORDING, OFF, FULL, or WRITE ERROR and flags lost records. COPY
REPORT also includes recorder state, recovery file path, export status and last
destination, and lost-record counts.
Reset only resets Health's displayed measurements; a reset marker remains in the
session. Project loads, New, and device changes do not erase previous history.

The continuous recovery file uses SDL's writable per-user `TapeSister/Health` application-data directory
(typically `%APPDATA%` on Windows and `$XDG_DATA_HOME` or `~/.local/share` on Linux).
Each starts with `health-session-`, UTC date/time, process ID and a unique counter.
The log is flushed about once a second; after a crash, the last flush normally
remains available. An interrupted final JSON line can be ignored.
This recovery file is separate from `tapesister-diagnostic.log`, which is a startup
log and does not contain the session timeline. Loading or starting another project
does not split the timeline: a saved snapshot contains the application's history
up to that save, including earlier project activity in the same session.

`session_log=1` in `tapesister.ini` is the default. Set it to `0` to disable
recording on the next launch. The setting is a local preference, not project data.

## Contents and interpretation

- One-second audio summaries, including callback counts, actual sample rate and
  buffer size, average/peak processing, load, near-deadline and over-budget counts.
- Individual callback observations at 90% of budget or above, and significant
  start gaps. These retain callback start timestamps, duration and configuration.
- Slow control operations: holds of at least 1 ms or UI waits of at least 5 ms,
  with call site and separate wait/hold durations.
- Pedalboard/Sister action requests with control IDs, names where available and
  values; routing edits, Matrix window activity, group transport commands,
  project load requests, New, output lifecycle and visualizer launch/ready/close/exit.
- Changes to the cached active setup, routing and effect states, and one-second
  workload samples. These are UI observations with snapshot age, not an exact
  capture of DSP state at an earlier timing maximum. Fast actions can occur
  between setup snapshots; action records help locate them.
- Embedded TrackSister transport is reported separately from the legacy tracker.
- Once-per-second host process CPU, total system CPU, resident memory and available
  system memory where supported. Process CPU uses 100% per core; system CPU uses
  100% across the machine. `-1` CPU or zero memory means unavailable. These figures
  cannot identify a particular other application or diagnose GPU/driver latency.
- Backend-reported counters where available, manual click markers, Health resets,
  and optional profile summaries only when PROFILE is explicitly enabled.

The format is newline-delimited JSON (schema 1), with session-relative monotonic
`t_ms`. Audio and UI producers have separate queues: sort by timestamp when
building a timeline. Action requests can fail or be cancelled; they are not proof
that a change was applied. Subsequent setup snapshots describe observed state.
Audio records, backend signals and manual click reports remain distinct evidence;
callback gaps are not labelled as confirmed audible dropouts. The log contains
no recorded audio or sample contents and is not automatically uploaded.

## Bounds and failure handling

The output callback performs one fixed packet enqueue to an 8,192-entry SPSC
queue. It does not allocate, format text, write files, wait for the writer or
acquire a logging mutex. Control events use a separate bounded queue with a
single nonblocking attempt. Slow disk, queue overflow and export copying can
lose diagnostic records; cumulative loss counts are explicit. Missing queue
records are not converted into false callback-gap measurements.

A low-priority background thread writes and flushes the log, reads system counters
and copies exports. Each live file is capped at 64 MiB plus a small terminal
limit record. At the cap, recording stops with a visible FULL status; the existing
session remains exportable. The writer keeps the eight newest inactive auto logs
and their matching saved copies at startup, skipping processes still running.
Thus a typical installation retains eight previous sessions plus the current one,
with up to two 64 MiB files per retained session. Copy files elsewhere before they
age out. Cleanup failure may leave additional files; it never deletes unrelated
files or stalls audio. This retention policy applies only to the recovery folder,
not project `logs` folders. Open/write failures are shown as WRITE ERROR.

Exports create their directory and copy on the writer thread, then replace the
previous snapshot only after a successful copy. A failed export preserves an older
snapshot and reports failure in Health and the main status line; it does not undo
a successful project save. Up to eight exports can queue, including Save As while
an earlier copy is running. Save-and-Exit drains accepted exports before shutdown.
The export/status mutex never participates in the audio callback or event enqueue.
Project saves carry the existing `logs` entry into the replacement bundle, so
earlier sessions survive repeated saves. A separate filesystem guard coordinates
project replacement with exports; the recorder continues draining events while
export copying waits for the project save to finish. Audio never takes this guard.

This is diagnostic instrumentation, not a guarantee that every audible artifact
has a timing fault or that logging has literally zero cost. Its overhead is
measured separately and it can be disabled for comparisons.
