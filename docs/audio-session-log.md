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
2. In Health, choose **SAVE SESSION**. The writer creates a snapshot in the
   background and copies its file path to the clipboard when ready.
3. **LOG FOLDER** opens that folder. Attach the `.saved.jsonl` file for analysis.
   Playback and the original live log continue after saving. Saving again updates
   that session's saved copy. Copy it elsewhere to retain multiple snapshots.

Health shows RECORDING, OFF, FULL, or WRITE ERROR and flags lost records. COPY
REPORT also includes recorder state, the live file path, and lost-record counts.
Reset only resets Health's displayed measurements; a reset marker remains in the
session. Project loads, New, and device changes do not erase previous history.

Files use SDL's writable per-user `TapeSister/Health` application-data directory
(typically `%APPDATA%` on Windows and `$XDG_DATA_HOME` or `~/.local/share` on Linux).
Each starts with `health-session-`, UTC date/time, process ID and a unique counter.
The log is flushed about once a second; after a crash, the last flush normally
remains available. An interrupted final JSON line can be ignored.

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
files or stalls audio. Open/write failures are shown as WRITE ERROR.

This is diagnostic instrumentation, not a guarantee that every audible artifact
has a timing fault or that logging has literally zero cost. Its overhead is
measured separately and it can be disabled for comparisons.
