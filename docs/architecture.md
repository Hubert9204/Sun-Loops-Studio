# Architecture

## Threads

| Thread | What it does | Rules |
| --- | --- | --- |
| Message (UI) thread | Windows, editing, file dialogs, decoding samples, building a new `Project` | Free to allocate |
| Audio thread | `AudioEngine::audioDeviceIOCallbackWithContext` -> `Player::render` | No allocation, no locks, no I/O, no logging |

## Moving data from the UI to the audio thread

The UI owns a `std::vector<Channel>`. On every edit it calls `Project::make`, which validates and sorts
the notes and clamps volume and pan, then hands the result to `SnapshotExchange<Project>::publish`.

The audio thread calls `acquire()` once per block. It announces which snapshot it is reading (a hazard
pointer), and `publish()` never frees that one. Everything else is freed on the UI thread, so the audio
thread never frees memory.

Because a snapshot owns its sample data through `shared_ptr`, the audio thread may only keep a pointer
into a snapshot across blocks if the next snapshot still owns the same object. `Player` enforces this:
when a new snapshot arrives, any sampler voice whose sample is no longer the channel's current sample is
dropped before it can read freed memory (covered by a test).

## Player

`Player::render` does everything for one block: it advances the transport, starts notes sample-accurately,
runs up to 32 voices (synth or sampler), mixes them per channel with smoothed gain and equal-power pan,
tracks per-channel peaks and soft-limits the master with `tanh`.

- Resolution is 480 ticks per beat; the grid of the editors is a 16th note (120 ticks).
- Synth notes end at their length and at the loop boundary. Sampler voices are one-shot and keep ringing
  across the loop boundary.
- When playback stops, voices fade out over 80 ms and the transport rewinds.

The offline renderer uses the same `Player`, so an exported file sounds like what you hear. It renders
one pass of the loop plus a tail so releases and drum tails are not cut.

## Panels

The main window is a toolbar plus a `Workspace`; the Channel Rack, Piano Roll and Mixer are `PanelWindow`
children of it, so they float inside the program, can overlap and are z-ordered by clicking. A panel does
not own its content (`RackView`, `PianoRoll`, `MixerPanel` are members of `MainComponent`, declared before
the panels so they outlive them).

`PanelLayout` (x, y, width, height, visible) has no JUCE dependency: it serialises to `"x y w h v"` for the
settings file, rejects malformed text so a corrupted file falls back to the defaults, and `clampedTo()` keeps
a panel inside the workspace and above its minimum size. The workspace re-clamps every panel when the window
is resized. The rack rows and the mixer strips edit the same `Channel` objects and refresh each other.

## Testing

`tests/test_core.cpp` drives `Player` and the renderer without an audio device and checks the result with
single-bin DFTs (pitch), sample-exact timing, peak levels, silence and a replaced `operator new` that counts
allocations inside `Player::render`.
