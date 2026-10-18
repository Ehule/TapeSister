# Offline rendering

Open **F11 → Render Offline…** from any workspace. Select **Mosaic**, **TrackSister**,
or both, enter a duration and effects tail, choose 24-bit PCM or 32-bit float WAV,
then use **Choose File and Render…**. The normal file browser selects the destination
and confirms replacement of an existing file. The sample rate follows the current
session (48 kHz when no rate is available).

Duration accepts seconds, `minutes:seconds`, or `hours:minutes:seconds`. Both selected
players start at zero. Their existing loops, tracker tempo changes/jumps, Mosaic tape
speed, mutes/solos, channel levels and routes still apply. Duration is the hard end of
source playback: a looping score cannot make an infinite export. The effects tail is
additional time with the players stopped and their processing still running. Use a
finite tail for long feedback patches. WAVs larger than the RIFF 4 GB limit are rejected
before rendering; no truncated header is written.

The renderer is a separate instance of the same executable, using the native audio
callback without opening an audio or MIDI device. It runs as fast as processing permits,
with progress and cancellation. It does not use a replacement mixer or synth. Windows
runs the worker below normal priority. Rendering still consumes CPU and memory; it is
best done outside a live performance.

The exported signal is the **stereo FILE OUT mix**, including the selected sources'
internal routing, shared effects, independent Matrix effects and sidechains, Pedalboard,
Sister processing, EQ, limiter and OUT gain. This is not a downmix of a multichannel
speaker array: routes dedicated to hardware outputs or Ambisonics speaker feeds remain
outside the stereo FILE OUT tap, just as in real-time FILE OUT recording.

A temporary native project snapshot includes unsaved project edits. Export does not mark
the working project saved or move its playheads. Finish pending Mosaic card edits and
captures first. ARP, manual MIDI/QWERTY playing, live input, Live Link, and previously
recorded audio still circulating only in the live Sister buffer are not captured. An
active legacy external Insert must be bypassed first; real hardware requires real-time
recording. Matrix hardware send/return paths cannot be reproduced offline.

Each export starts with fresh DSP histories. Active EQ A/B, Prism Matrix, Router state
sequence/timed bypass and spatial motion triggers restart in the child; stopped timers stay stopped.
Current Pedalboard engage/topology ramps continue from their captured point. Persisted
LFOs and all render-side transitions advance from audio frames, independently of how
quickly the render finishes. This is a render of the current setup from the beginning,
not a recording of future live edits or an exact continuation of a live buffer.

**Hide** closes only the progress window. F11's **Render Progress…** button reopens it.
Closing TapeSister cancels its worker. Failed or cancelled renders remove their temporary
WAV and project snapshot; an existing destination is replaced only after successful
completion. Only one render runs at a time.

## Implementation and regression coverage

`main_sdl_offline_engine.inc` drives the same `audio_callback` used for playback, tapping
the existing FILE OUT point before hardware speaker mapping. The worker is dispatched
before normal application startup. `main_sdl_offline_job.inc` manages private snapshots,
process lifetime, progress, cancellation and final output publication. No helper binary
or external encoder needs packaging.

`tapesister_offline_render_tests` exercises the real worker with an intentionally invalid
audio driver, Mosaic/TrackSister/both, exact frame counts, both WAV encodings, channel gains,
live-state preservation, automation capture, cancellation, destination collisions and
write failures. It also renders the real F11 and export windows for visual inspection.
