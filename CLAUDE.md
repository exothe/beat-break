# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

BeatBreak — a JUCE 8 audio plugin (VST3 / AU / Standalone) recreating FL Studio's
Gross Beat: two tempo-synced curves over a one-bar grid, a **time** curve that
maps playback position and a **volume** curve that gates gain, applied to a
rolling buffer of incoming audio. Not a git repo. macOS is the developed-on
platform; nothing is macOS-only except the `auval` step.

## Commands

`cmake` is not on PATH here — it lives at `/opt/homebrew/bin/cmake`.

```sh
/opt/homebrew/bin/cmake -B build -DCMAKE_BUILD_TYPE=Release      # first configure fetches JUCE 8.0.15 (~1 min)
/opt/homebrew/bin/cmake --build build -j8                        # all three plugin formats
/opt/homebrew/bin/cmake --build build -j8 --target BeatBreakTests
./build/BeatBreakTests_artefacts/Release/BeatBreakTests          # exit 0 = all checks passed
/opt/homebrew/bin/cmake --build build -j8 --target BeatBreakStateTests
./build/BeatBreakStateTests_artefacts/Release/BeatBreakStateTests
auval -v aufx Bbt1 Bbrk                                          # validate the AU (macOS)
```

`-DJUCE_PATH=/path/to/JUCE` uses a local JUCE checkout instead of fetching one.
`COPY_PLUGIN_AFTER_BUILD` installs into `~/Library/Audio/Plug-Ins/{VST3,Components}`
on every build, so a stale DAW scan is usually a stale build, not a stale copy.

The standalone build is the fastest way to smoke-test the GUI:
`./build/BeatBreak_artefacts/Release/Standalone/BeatBreak.app/Contents/MacOS/BeatBreak`

### Tests

Two console apps, not a test framework: `main()` runs numbered blocks in
sequence and each calls `check(condition, description)`. There is **no filter
flag** — to run one check, comment out the other blocks.

`Tests/EngineTests.cpp` (DSP) builds from a few sources and is fast.
`Tests/StateTests.cpp` covers preset files, host state and slot names, so it
constructs a real `BeatBreakProcessor` and links the whole plugin's shared-code
target — slower to build, and the one that catches a broken state format.

The technique is worth preserving: the input signal is a ramp encoding absolute
sample position (`positionSignal`), so an output sample value decoded by
`sourceOf()` says exactly *which input sample the read pointer fetched*. That
turns "does the stutter work" into an exact numeric assertion. Tolerances near
0.25 samples exist because the ramp is float32 seconds, not because the DSP is
imprecise.

Two traps when adding checks: probe frames must be inside the rendered length
(a 12 s render at 120 BPM is 6 one-bar loops), and probes must avoid the
clamp region described below.

## Architecture

Signal flow per sample, all in `GrossEngine::process`:

1. `x` = position inside the pattern loop, `0…1`, derived from host ppq.
2. `y` = time curve at `x`, morphed toward the unity line by **Time Amount**.
3. Mapped read position in beats: `loopStart + loopBeats − (1 − y)·spanBeats`.
4. `delay = now − mapped`, read from the ring buffer with a Catmull-Rom kernel.
5. Volume curve at `x` → gain through the attack/release envelope, then
   **Mix** against dry.

### Invariants that are easy to break

- **Slope is playback rate.** Unity playback is the line
  `y = 1 − loopBeats·(1 − x)/spanBeats` — the corner-to-corner diagonal only
  when Time Span is 1 loop. `BeatBreakProcessor::getUnityY` is the single
  source of truth for it; the editor draws it and the engine morphs toward it.
- **Above the unity line is the future.** The engine clamps `delay` to ≥ 0, so a
  curve point higher than unity reads "now" instead. Factory patterns are
  written to stay at or below it — a new pattern that violates this will sound
  like a pass-through in part of the loop. This is also why the freeze test
  probes the second half of the loop.
- **`appendPlay` takes a rate, not a length.** Each slice window stops `edge`
  (0.0006 of a loop) short of the next boundary so the jump reads as a
  discontinuity. The source length is derived from that *shortened* window, so
  rate 1.0 is exactly unity. Deriving it from the full window makes every slice
  play ~0.5 % sharp (~8 cents) — this was a real bug caught by the tests.
- **`CurveSnapshot::maxPoints` is 128.** Pattern generators must stay under it:
  a 32-slice stutter is already 64 points. `copyFrom` silently truncates.
- **No allocation, locking or blocking in the audio path.** `GrossEngine` and
  `CurveSnapshot::getValue` are the hot path; the ring buffer is sized once in
  `prepare` (24 s) and never resized.

### Curve editing rules

`EnvelopeCurve::minSpacing` is the one-point-per-x rule: `addPoint` returns the
existing index instead of stacking a second point on a taken x, and `movePoint`
clamps between the neighbours rather than reordering, so a drag index stays
valid for the whole gesture. Factory patterns bypass this through `setPoints` -
slices sit `edge` (0.0006) apart, which is tighter than `minSpacing`, and
`movePoint` falls back to the midpoint when neighbours are that close.

In the editor, right-click adds or grabs the point owning that x column,
double-click on a point removes it, and right-click *on* a point opens the
shape menu - which must be targeted with `withTargetScreenArea`, since
`withTargetComponent` puts it at the middle left of the grid. Smooth points get
a diamond tension handle halfway along their segment (drag = tension,
right-click = reset); it shares `tensionSegment` with the segment-body drag.

The point menu's **D** shortcut is a `KeyListener` attached to the modal menu
window, grabbed from `ModalComponentManager` right after `showMenuAsync` -
JUCE's menus only handle the arrows, return and escape themselves, and there is
no API for item shortcuts. `stopListeningToMenu()` must run on every exit path
(menu callback, destructor) or the listener outlives the window.

Menu windows are created with `windowIgnoresKeyPresses` and never take focus:
keys reach them through the peer of whatever *is* focused, which
`ComponentPeer::getTargetForKeyPress` then redirects to the modal component.
With nothing in the editor holding focus that peer never sees a key at all - in
FL the keystroke just goes to FL. So `CurveEditor` borrows focus for the life of
the menu (`takeKeyboardFocusForMenu`) and hands it back afterwards, including
`SetFocus` to the host's root window on Windows; JUCE's own
`giveAwayKeyboardFocus` does not return the OS focus.

The curve is stroked **per segment**, evaluating `EnvelopeCurve::shape` at
`u = 0 … 1`, not per screen column. Sampling by column makes the polyline cut
the corner at any point whose x falls between two columns, which is obvious as
soon as the tension gets steep.

### Threading model

Curves are edited on the message thread and read on the audio thread:

- `EnvelopeCurve` (std::vector, message-thread owned) → `CurveSnapshot`
  (fixed POD) → `PublishedCurve` (double buffer + atomic index).
- Mutations take `BeatBreakProcessor::getCurveLock()` (a `SpinLock`); the audio
  thread only *try*-locks it in `processBlock` to publish, so it never blocks —
  a failed try just reuses last block's snapshot.
- Publishing is triggered by `publishActiveCurves()` (sets `curvesDirty`) or by
  the slot parameter changing; the audio thread is the only publisher, which is
  what keeps `PublishedCurve` single-writer.

Any new curve mutation path must take the lock and then mark dirty, or edits
will not reach the audio thread.

### Files

| File | Role |
| --- | --- |
| `Source/EnvelopeCurve.h` | Point model (x, y, tension, shape ∈ curve/step/smooth), editing ops, `toString`/`fromString` |
| `Source/CurveSnapshot.h` | Audio-thread copy + `PublishedCurve` handoff |
| `Source/GrossEngine.*` | Ring buffer, position mapping, interpolation, declick crossfade |
| `Source/FactoryPatterns.*` | 36 time + 36 volume patterns, built from `appendPlay`/`slicedTime`/`gate`/`duck`/… helpers; slot 0 of each is neutral |
| `Source/PluginProcessor.*` | APVTS parameters, state, playhead/tempo, snapshot publishing |
| `Source/CurveEditor.*` | The editable grid (also renders the unity line and playhead) |
| `Source/PluginEditor.*` | Window layout, 36-slot grids, knobs |

Parameter IDs (`ParamID` in `PluginProcessor.cpp`): `mix`, `timeAmount`,
`volAmount`, `volAttack`, `volRelease`, `volTension`, `smoothing`,
`loopLength`, `span`, `timeSlot`, `volSlot`,
`timeEnable`, `volEnable`, `sync`, `freeTempo`. Slots are `AudioParameterChoice`
so hosts can automate pattern switching, which is how Gross Beat is played.

State is the APVTS tree plus a `curves` child holding all 72 curves as text
attributes `t0…t35` / `v0…v35`, plus `tn*` / `vn*` for renamed slots (absent
attribute = factory name). Changing `EnvelopeCurve::toString`'s format breaks
saved sessions.

`captureState()` / `applyState()` build and consume that tree; host state
(`get`/`setStateInformation`) and preset files (`savePreset` / `loadPreset`,
`.bbpreset` XML under `~/Documents/BeatBreak Presets`) are both thin wrappers
around them, so anything added to the tree is saved in both places at once.

### Volume envelope

**Attack / Release** are the time a *full-scale* (0 → 1) move takes, so the
envelope is a slew limiter: a curve segment that ramps slower than the knob
passes through untouched, only steps get shaped. Both floor at 0.2 ms, which is
what keeps a hard gate from clicking with the knobs at zero.

**Tension** bends the shape without changing the length: the per-sample rate is
`k · travelled^(1 − 1/k)` with `k = 2^(−tension·2)`, which integrates to exactly
the knob's time for any `k`. Positive tension (`k < 1`) races away from the
start and lands slowly, negative creeps out and snaps home. The rate is clamped
to `[0.002, 64]` — without that the stiff ends of extreme tension either stall
or overshoot the intended length by more than a few percent, which is exactly
what `EngineTests` check 10 measures.

### Keyboard focus

Nothing in the editor takes keyboard focus: the constructor walks the whole
child tree calling `setWantsKeyboardFocus (false)` and
`setMouseClickGrabsKeyboardFocus (false)`, and `EDITOR_WANTS_KEYBOARD_FOCUS` is
`FALSE`. A focused JUCE `Button` treats space as a click, which steals FL
Studio's transport key. `BeatBreakEditor::keyPressed` is a Windows-only backstop
that posts the spacebar on to the host's root window. Any new child component
needs the same treatment - the walk only runs once, in the constructor.

### Declicking

Jumps are unavoidable (they are the effect). When the delay moves more than 2 ms
in one sample, the engine crossfades 3 ms from the *old* read position held at
constant delay — a constant delay means normal-speed playback, so the outgoing
half keeps sounding musical rather than freezing. **Smoothing** instead glides
the delay, which is why it produces pitch slides rather than clean cuts.

See `README.md` for the user-facing control reference and editing gestures.
