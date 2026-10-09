# Tile and track output routing

Right-click an occupied canvas tile to open its routing window. Middle-click
renames it. A Rename Tile button is also available in the routing window for
mice without a middle button. Shift-right-click retains the existing clear action.

In SisterTracker, right-click the **track number** above its pattern column.
The length and FastTracks controls retain their existing gestures. This window
is modeless: playback continues while you change a route, and Escape closes it.

| Setting | Result |
| --- | --- |
| Main Route | Existing shared Router, effects, Master EQ, limiter, OUT and Ambisonics/stereo output path |
| Clean Stereo Pair | The source's left and right channels go directly to two chosen logical speakers, with pan/balance and width |
| Clean Single Speaker | `(L + R) / 2` goes directly to one chosen logical speaker |
| Use Tile (tracks only) | Each note follows its tile's route; this is every track's default |

An explicit track route replaces the whole tile route, including its pan and
width. Choosing **Main Route** on a track overrides even a clean-routed tile.
The tile itself stays unchanged, so the same tile can have different destinations
on different tracks. Track routes apply throughout the song, including channels
9–32 and imported module instruments with multiple tile bindings.

## Controls and output mapping

Click destination buttons to step forward, right-click to step backward, or use
the wheel. Drag or wheel Pan/Balance and Width; Shift-wheel gives one-unit steps;
right-click restores center or 100% width. The default pair preserves stereo
exactly. Pan attenuates the opposite side, with unity at center. Width ranges from
mono (0%) through original stereo (100%) to wider stereo (200%). These adjustments
follow any tracker note/envelope panning. A single-speaker route is a mono fold;
opposite-polarity material can cancel, as it would in an ordinary mono mix.

The speaker diagram uses the array configured under **Ctrl+F9 → Array**. Speaker
numbers are logical positions; the labels show their hardware output channels.
Clean output uses that mapping but bypasses Ambisonics motion/decoding and its
per-speaker calibration trim/delay. It also bypasses the creative effects and
Master EQ. The global OUT fader still controls it, and a final linked peak guard
protects the combined hardware output. There are no extra effect instances.

A missing channel, duplicate assignment, or channel reserved by the external
Insert makes that route unavailable. The **whole affected route** folds to
hardware outputs 1/2 and the window displays a warning. Other valid source routes
keep their destinations. No audio device needs to be reopened to edit a route.
Route edits interpolate over 5 ms; note/voice transitions retain short fades.

## Saving and recording

Routes belong to stable tile identities and tracker lanes. Copying a tile copies
its default route; moving it between slots/pages keeps the route. Replacement
and destructive edits retain the destination. Project dirty detection includes
routing. New projects save tile data as **TSR34** and tracker data as **SISTRK v4**.
Older projects open with Main on tiles and Use Tile on tracks. Older TapeSister
builds cannot open the new formats; save a separate project copy when comparing
this build with an older build.

FILE OUT, Mosaic OUTPUT recording and output meters include a **stereo reference
mix** of the clean routes plus Main, after OUT gain. They do not capture discrete
multichannel files or reproduce the array's physical placement/calibration.
Dry keyboard recording includes clean-routed notes. Existing internal H1/H2/H3
stem taps keep their meanings. Mosaic cards, FM, external inputs and browser
previews retain their existing Main routing; this release adds tile and embedded
tracker routes, not independent routes for those sources.

Shared effect sends and a freely connected routing matrix remain a later step.
This version offers one Main or clean destination per source.

## Native UI captures

These images were captured from the SDL framebuffer using the controller test,
with a four-output fixture and a valid four-speaker mapping.

![Tile route with stereo pair, pan and width](screenshots/source-routing/tile-routing.png)

![Track using each tile's route](screenshots/source-routing/track-routing-inherit.png)

![Track overriding its tiles with a single speaker](screenshots/source-routing/track-routing-override.png)

## Validation

The source-route tests exercise matrix coefficients, inheritance, transitions,
output ownership and fallback, note/performance/ARP playback, tile cloning and
persistence. The native controller test drives the actual routing window and
main audio callback, including embedded tracker playback, live overrides,
embedded score save/reopen, dry recording's signal tap, and stereo fallback.
Existing core, tracker, keyboard, stereo voice and spatial regressions run with
them. The vendored application patch is checked against all 271 pinned source
hashes; the upstream TapeHead repository is unchanged.

For a listening check: play a sustained tile, switch Main → Clean Pair, sweep
pan/width, then sequence it on two tracks with different overrides. Repeat on a
stereo device and confirm the fallback warning. Physical interface behavior and
CPU headroom on the X220 still need that hardware check.
