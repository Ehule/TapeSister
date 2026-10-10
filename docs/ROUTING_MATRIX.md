# F9 routing matrix

F9, tile right-click and TrackSister track-header right-click open the same
modeless window. Audio and keyboard playing continue while it is open.

**Read across a row:** this sound goes to each checked destination column.
Multiple sources feeding a processor are summed into one shared instance.
Prism, Pedalboard and Fallout rows contain only the processed contribution.
Sister's row contains its tape/head mix. Dry audio needs its own connection;
checking a processor does not secretly add another dry copy.

## Normal stereo

New configurations start with tile/track sound and FM/Live Link feeding
**Main Mix → Master EQ → Limiter → OUT 1/2**. External inputs start disconnected.
Track routes take precedence over tile routes. **Use Tile** makes the entire
track inherit the route of each played tile; **Use Track** gives it its own route.
An inherited row says TILE rather than pretending that every note has one route.

Older configurations and project files retain the old engine and saved sound.
F9 identifies this as a legacy project. **Use Matrix** opts into the new graph:
the legacy serial order, Insert assignment and Sister source switches stop
controlling the signal path. Existing source clean/send levels are retained;
legacy direct speakers retain their channel mapping. Shared processor outputs
start at Main, followed by EQ/limiter. This is an intentional conversion, not a
claim that an arbitrary old serial chain sounds identical through the new graph.
Save an experimental version under another project name if you want both.

## Connections and positioning

- Left-click a cell to connect/disconnect. A new connection starts at 0 dB.
- Right-click a cell to inspect it without toggling it.
- Drag **Level**, or Ctrl+wheel over a cell, to change that connection's level.
- **Row Pan** positions that source on all its outgoing connections. Track pan
  also updates the yellow scope indicator, including while stopped or muted.
- **Stereo / Mono** preserves stereo or folds it to mono before pan. Mono plus
  full-left/right pan addresses a single speaker in an output pair.
- Wheel over the grid to scroll sources. The filters show tracks, tiles on all
  pages, external/live inputs, or processor returns. OUT arrows reveal more ports.
- Middle-click an input row or output column to rename it, or select it and use
  **Name I/O**. Enter saves; Escape cancels. IN/OUT numbers remain visible.

Physical destinations are pairs on the active output device, up to OUT 15/16;
input rows expose up to four pairs on the active input device. **N/A** means a
port is not open on the current device. It stays saved but produces silence;
it is never silently redirected into the main speakers. These physical matrix
outputs bypass the old spatial-array/Insert port reservation system.

The processors' own power/effect controls still apply. OFF labels distinguish
a connected processor from an enabled one. Pedalboard's row is its POST rack;
PRE/head placements remain local to Sister. Sister POWER, ROLL, HOLD and MONITOR
remain operational. Its old source trims are replaced by matrix connection
levels. Local head feedback remains inside Sister.

## Terra to three destinations

Name IN 1/2 “Terra”. On that row check Sister, Prism and Main (or a physical
output pair for a direct path). Set each connection level independently.
Sister and Prism each process the sum of all sources sent to them once.
Route their output rows to Main or to different hardware pairs as desired.

## Vulture across the whole mix

1. Name OUT 3/4 “Vulture Send” and IN 5/6 “Vulture Return”.
2. On Main Mix, disconnect EQ and connect OUT 3/4.
3. Connect IN 5/6 to Master EQ.
4. Keep Master EQ → Limiter → OUT 1/2 connected.

Connect the physical OUT 3/4 cables to Vulture and its outputs to IN 5/6.
No legacy Insert assignment is needed. Keeping Main → EQ connected as well
would intentionally mix dry with the hardware return; leave it disconnected
for a full-series hardware path.

Matrix checkbox, gain, pan and naming changes are live. Adding an input route
requests the active capture device automatically. Changing the audio backend,
ASIO driver or driver buffer still uses the existing Audio setup lifecycle;
those settings can require Save Config + Restart. The matrix does not introduce
backend hot switching or aggregate multiple independent output devices.

## Feedback

Ordinary connections must form an acyclic graph. Enable **Feedback** to make a
connection that closes a loop. That connection is amber and marked F. It reads
a bounded output delayed by 64 samples and is limited to −6 dB maximum gain.
**Cut Feedback** removes all explicitly delayed connections and clears their
stored samples. No extra processor instances are created. Physical cable loops
are outside the graph and cannot be inferred from port aliases.

## Saving, recording and compatibility

TSR37 tile files, SISTRK v7 tracker scores and Sister state v30 preserve the
new source routes, graph, connection levels, row pan/mono and hardware aliases.
Old formats load; older application builds cannot read the new tile/score saves.
Opening a project replaces its matrix and aliases rather than retaining the
previous project's graph. The graph and aliases are also in saved configuration.

FILE OUT records the main stereo program (OUT 1/2), before the final linked
hardware guard. It does not sum every separate speaker/send output. Sister's
H1/H2/H3 capture taps remain available. Matrix-routed input to Sister reaches
its tape directly; a second TRACK/EXT source switch is not required.

Internal route edits are compiled off the audio thread. The callback uses fixed
storage and one shared DSP history per processor. ASIO inputs use the same
borrowed duplex block as playback; asynchronous input devices use bounded,
resampling input bridges. Physical M6/Vulture listening remains a hardware check.

A legacy direct stereo route using nonadjacent/reversed speaker-array channels
is marked OLD, rather than showing an incorrect matrix checkbox. It keeps
playing its saved map. The row's **Use Matrix** button explicitly replaces that
map with Main Mix so it can be repatched using the physical pair columns.
