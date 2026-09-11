# BeatBreak

A JUCE audio plugin that recreates the idea behind FL Studio's **Gross Beat**: two
tempo-synced mapping curves — one for *time*, one for *volume* — drawn over a
one-bar grid, applied to a rolling buffer of the incoming audio.

The time curve is a position map, not a delay: its slope *is* the playback rate.
Parallel to the unity line means normal speed, flat means frozen, falling means
backwards, shallower means pitched down, steeper means pitched up. Repeated
sawtooth shapes give stutters and rolls; a decelerating curve gives a tape stop.

Formats: **VST3**, **AU**, **Standalone** (macOS/Windows/Linux via JUCE).

## Build

Requires CMake 3.22+ and a C++17 compiler. JUCE 8.0.15 is fetched automatically.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

Artefacts land in `build/BeatBreak_artefacts/Release/` and are copied into the
system plugin folders (`COPY_PLUGIN_AFTER_BUILD`). To use an existing JUCE
checkout instead of fetching one:

```sh
cmake -B build -DJUCE_PATH=/path/to/JUCE -DCMAKE_BUILD_TYPE=Release
```

### Offline checks

```sh
cmake --build build --target BeatBreakTests
./build/BeatBreakTests_artefacts/Release/BeatBreakTests

cmake --build build --target BeatBreakStateTests
./build/BeatBreakStateTests_artefacts/Release/BeatBreakStateTests
```

The tests render a position-encoding ramp through the engine and verify where
the read pointer actually landed: unity mapping is a sample-accurate
pass-through, a flat curve holds one position, a half-speed curve advances at
0.5×, `Stutter 1/8` re-reads the first eighth exactly, `Reverse 1/8` runs at
−1.0×, gates open and mute, `amount = 0` is transparent, all 36 factory slots
stay finite and level-sane, jumps are crossfaded rather than spliced, and the
volume envelope's attack, release and tension behave (a 50 ms release takes
50 ms whatever the tension, which only bends the shape of the move).

`BeatBreakStateTests` covers the other half: a preset written and read back
keeps its parameters, curve edits and renamed slots, and host state carries the
same things.

## How the mapping works

The engine keeps 24 seconds of input in a ring buffer. For every output sample:

1. `x` = position inside the pattern loop, `0 … 1`, from the host playhead.
2. `y` = the time curve at `x` (morphed toward the unity line by **Time Amount**).
3. The curve's vertical axis covers `spanBeats` of buffer, with the top of the
   grid at the end of the current loop, so the mapped position in beats is
   `loopStart + loopBeats − (1 − y) · spanBeats`.
4. The difference between that and *now* is the read delay. Fractional
   positions are read with a Catmull-Rom kernel, so slides are pitched cleanly.
5. The volume curve at `x` gives the gain, then **Mix** blends against dry.

Unity playback is therefore the line `y = 1 − loopBeats·(1 − x) / spanBeats` —
the corner-to-corner diagonal when **Time Span** is `1 Loop`. The editor draws
it in orange as a reference.

Reading *above* that line would mean reading audio that has not arrived yet, so
the engine clamps to "now". This is a property of the effect, not a bug: with
**Time Span** = `1 Loop`, a flat curve at `y = 0.25` only freezes once the
playhead passes `x = 0.25`. Set **Time Span** to `2` or `4 Loops` to reach into
previous bars, at the cost of vertical resolution.

Discontinuities (every stutter jump) trigger a 3 ms crossfade: the old read
position keeps playing at unity speed while the new one fades in, which is why
hard splices do not click.

## Controls

| Control | What it does |
| --- | --- |
| **Loop Length** | Pattern length: 1/4, 1/2, 1, 2 or 4 bars (follows host time signature) |
| **Time Span** | How much buffer the time curve's y axis covers: 1, 2 or 4 loops |
| **Grid** | Snap and grid resolution: 1/4 … 1/32, including triplets |
| **Snap** | Toggle grid snapping (also bypassed while holding Shift) |
| **Host Sync** | Follow host tempo and playhead; off uses the free tempo slider |
| **Time Amount** | Morph the time curve toward unity playback |
| **Smoothing** | Glide on the read position — turns jumps into pitch slides |
| **Volume Amount** | Morph the volume curve toward unity gain |
| **ATT / REL** | Time a full-scale rise / fall of the volume envelope takes, 0–500 ms |
| **TENSION** | Bends that move: +1 jumps away and lands slowly, -1 creeps out and snaps home, 0 linear |
| **Mix** | Dry/wet |
| **Save / Load** | Write or read a `.bbpreset` file — parameters, all 72 curves and slot names. Default folder is `~/Documents/BeatBreak Presets` |
| **TIME / VOLUME** | Enable each curve independently |
| **Slots 1–36** | Per-curve pattern slots, automatable so a host can switch patterns |
| **Reset / Reverse / Factory** | Clear the slot, mirror it in time, or restore the factory pattern |

Each slot button shows its name, cut off with an ellipsis when it does not fit;
the tooltip has the slot number and the full name. Right-click a slot to rename
it, or to put its factory name back. Names are saved with the session and with
presets.

All knobs, both slot selectors and both enables are host-automatable
parameters. Curves themselves are saved with the session (all 72 slots).

## Editing curves

- **right-click** the grid: add a point there and drag it straight away. Each x
  column holds one point only, so right-clicking a column that already has one
  moves that point instead of stacking a second one on it
- **double-click** a point: delete it
- **drag** a point: move it (snapped; hold **Shift** for fine). Points stop
  short of their neighbours rather than crossing them
- **drag a segment's body** vertically, or **mouse wheel** over it: bend it
- a **Smooth** point puts a diamond handle halfway along its segment: drag it to
  set the tension, right-click it to reset the tension to zero
- **right-click** a point: segment shape — Linear/Curve, Step (hold), Smooth
  (S-curve) — or delete the point; press **D** while that menu is open to
  delete the point straight away

The white playhead line and dot show the live loop position and what the curve
is currently doing.

## Factory slots

36 time patterns (stutters 1/4 … 1/32, rolls, half/quarter speed, reverse
slices, tape stop/start, scratches, triplets, glitch tables, rewind, echoes)
and 36 volume patterns (gates 1/4 … 1/32 incl. triplets, offbeat and trance
gates, sidechain ducks, tremolos, fades, mutes, swells, reverse gates). Slot 1
of each is neutral: unity playback and unity gain.

## Layout

```
Source/
  EnvelopeCurve.h     point model, tension/step/smooth shaping, serialisation
  CurveSnapshot.h     lock-free, fixed-size copy for the audio thread
  GrossEngine.{h,cpp} ring buffer, position mapping, interpolation, declicking
  FactoryPatterns.*   the 72 factory patterns
  PluginProcessor.*   parameters, state, playhead handling, snapshot publishing
  CurveEditor.*       the editable mapping grid
  PluginEditor.*      window layout, slot grids, knobs
Tests/EngineTests.cpp offline DSP checks
```

Curve edits happen on the message thread under a spin lock; the audio thread
only *try*-locks it to take a snapshot, so it never blocks — if the editor holds
the lock during a block, that block simply uses the previous snapshot.

## Known limits

- The buffer is 24 s, so at very slow tempi a 4-bar loop with a 4-loop span will
  clamp instead of reaching all the way back.
- Slope changes pitch, as in Gross Beat — there is no time-stretching, by design.
- No MIDI-triggered slot switching yet; automate the slot parameter instead.
