# Video through the mandala

Open **F11 → EDIT VISUALIZER → VIDEO → ADD VIDEO FILE**. The existing file
browser opens in TapeSister's main window. Choose a clip, then **OPEN VISUALIZER**.
The footage becomes the source of a reflected radial kaleidoscope, underneath
the mandala's geometry and inside its trails, haze and echo treatment. The clip's
movement, seeded crop drift and musical activity evolve independently, so a loop
need not repeat the same visual composition.

| Control | Behavior |
| --- | --- |
| Add / Replace Video File | Reference a local clip; no upload or copy |
| Video On / Off | Enable the source or return to procedural geometry |
| Remove Video | Clear the project reference; never delete the file |
| Pause / Play Video | Hold/release the source frame while music and geometry continue |
| Restart | Return to the beginning and resume video |
| Loop | Restart at EOF, on by default; off holds the last frame |
| Video Blend | Mix the footage into the geometric mandala |
| Video Zoom | Change how much of each source frame enters the lens |
| Rotation | 50 is still; either side changes direction and speed |
| Audio Response | Amount of level, stereo and effect-driven deformation |
| Symmetry | Number of reflected sectors, including continuous odd symmetry |

Video sound is always ignored. Existing final-output audio features drive the
lens; this does not create another sound source, mixer route or audio device.
**Freeze Visual / Space** freezes both footage and geometry. Minimizing the
visual window pauses video decoding/presentation until it returns.

The project recalls the file reference, enable/loop switches and look controls.
Playback position and temporary pause are not saved. Reopening visuals starts
at the beginning. Put clips beside the project (or inside its sidecar folder)
to save relative references and move them together. Other files retain absolute
references. Media is not embedded in `.tsr` or automatically copied into a bundle.
Missing or unsupported media shows a player message while procedural visuals
and musical playback remain available. Use Replace Video File to relink it;
Restart retries a failed source after the file becomes available again.

Mandala settings use version 3. Versions 1 and 2 load with video off and retain
their original look. Older TapeSister builds reject version-3 visual settings;
keep a copy before overwriting a project you need to open with an older build.

## Media and performance

The picker lists MP4/M4V/MOV, MKV/WebM, AVI, MPEG/TS and GIF. Container extensions
do not guarantee a supported codec. The bundled decoder supports H.264, HEVC,
VP8/VP9, MPEG-1/2/4, MJPEG, raw video, FFV1, ProRes and GIF. AV1 is not included.
For older performance machines, start with a 640×360 or 960×540 H.264 MP4 at
24–30 fps. Larger inputs still incur their full decode cost before scaling.
Frames above 4096×2160 are rejected. There is no automatic proxy conversion in
this version. Clips with unusual rotation metadata are displayed in encoded
orientation; prepare a correctly oriented copy if necessary.

Only the visualizer child opens files and decodes video, on one low-priority
worker thread. The audio callback and its packet size are unchanged. Three
preallocated BGRA frame slots (about 3 MB) transfer the newest frame without
blocking the renderer; source decode/reference buffers are additional. One SDL
texture is at most 640×400. The textured lens has at most 1,728 vertices. Uploads
are capped at 15 fps in Economy, 30 otherwise, and 8 while shedding load.
Source timestamps pace playback; late converted frames are skipped. Looping
never resets the visual seed or the mandala's clock. A stalled visual process
can be closed/killed by the host without waiting in the audio callback.

The release builds bundle replaceable LGPL FFmpeg shared libraries and exact
source/license material. Keep `video/lib` with the Linux executable, or all
FFmpeg DLLs beside the Windows executable. A development build can use installed
FFmpeg development packages with `-DTAPESISTER_BUNDLE_VIDEO=OFF`.

## Validation

`test_mandala_video.c` exercises a synthetic H.264 clip with B-frames and an AAC
sound track: decode/scale, pacing, pause/resume, EOF hold, loop, restart, bounded
backpressure, missing files and repeated teardown. The controller test uses the
actual file picker, editor and child process while musical transport runs. It
checks video-only pause against full visual freeze and error fallback. Existing
queue, visualizer lifecycle, project-state and audio regression tests remain in
place. Dummy/software SDL checks do not establish latency or frame rates on a
physical X220, HP A8, projector or Windows audio interface.
