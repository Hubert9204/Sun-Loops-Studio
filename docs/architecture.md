# Architecture

Sun Loops Studio is a JUCE 8 application in C++20. This document explains how it is put together and why, so that
you can change it without breaking the real-time audio path. For using the program see
[user-guide.md](user-guide.md); for building it see [building.md](building.md).

## Source map

```
src/main.cpp                    Application and main window (minimum size 1080x600)

src/audio/                      No UI dependencies except JUCE audio types
  NoteSequence.h                Note, 480 ticks per beat, note validation and sorting
  Project.h                     Channel, SampleData, Project: the immutable snapshot the audio thread reads
  Arrangement.h                 Patterns, clips, and compilation of pattern / song into a plain Project
  SnapshotExchange.h            Lock-free hand-over of snapshots from the UI thread to the audio thread
  Player.*                      Transport, synth and sampler voices, per-channel mixer (real-time safe)
  AudioEngine.*                 Audio device, atomics (BPM, gain, play state, peaks), audio callback
  OfflineRenderer.*             Renders a Project to a buffer or WAV file with the same Player
  BuiltinSamples.*              Synthesised kick, snare and hat

src/plugins/PluginScanner.*     VST3 discovery (library only; not connected to the UI or audio path)

src/ui/
  MainComponent.*               Toolbar, workspace, panel wiring, patterns, layout persistence, export
  PanelWindow.*                 PanelWindow (floating panel) and Workspace (the area they float in)
  PanelLayout.h                 Panel position/size/visibility: parse, serialise, clamp (no JUCE)
  RackView.h  ChannelStrip.*    Channel Rack panel body and its rows (step grid, context menu)
  PianoRoll.*                   Note editor
  PlaylistView.*                Playlist panel body
  MixerPanel.*                  Mixer panel body (vertical strips)
  AudioMeter.*                  Level meter

tests/test_core.cpp             Headless tests (no audio device, no display)
```

`CMakeLists.txt` builds two targets from the same core sources (`SLS_CORE_SOURCES`: Player, BuiltinSamples,
OfflineRenderer, PluginScanner): the application `SunLoopsStudio` and the console test program
`SunLoopsStudioTests`.

## Threads and real-time rules

| Thread | What it does | Rules |
| --- | --- | --- |
| Message (UI) thread | Windows, editing, file dialogs, decoding samples, building a new `Project` | May allocate and block briefly |
| Audio thread | `AudioEngine::audioDeviceIOCallbackWithContext` -> `Player::render` | No allocation, no locks, no I/O, no logging |

The audio thread shares state with the rest of the program in two ways only: lock-free atomics for scalar controls
(play state, BPM, master gain; peaks flow the other way) and `SnapshotExchange` for the project itself.

## Moving the project to the audio thread

The UI owns a `std::vector<Channel>` and an `Arrangement`. After every edit it compiles a `Project` (see below) with
`Project::make`, which validates and sorts the notes and clamps volume, pan and root pitch, and gives it to
`SnapshotExchange<Project>::publish`.

The audio thread calls `acquire()` once per block. It announces which snapshot it is reading (a hazard pointer) and
`publish()` never frees that one; everything else is freed on the UI thread, so the audio thread never frees memory.

A snapshot owns its sample data through `shared_ptr`. The audio thread may keep a raw pointer into a snapshot across
blocks only if the next snapshot still owns the same object, so `Player` drops every sampler voice whose sample is no
longer its channel's current sample when a new snapshot arrives (covered by a test). Because compiling a pattern or a
song copies the channels, a sample stays the same object across edits and its voices keep ringing.

## Player

`Player::render` does everything for one block: advances the transport, starts notes sample-accurately, runs up to
32 voices (synth or sampler), mixes them per channel with smoothed gain and equal-power pan, tracks per-channel peaks
and soft-limits the master with `tanh`.

- Resolution is 480 ticks per beat; the editors snap to 16th notes (120 ticks).
- A synth voice is a sine with two harmonics and a short attack/release. It ends at the note length and at the loop
  boundary. A sampler voice plays its sample once, repitched by note relative to the channel's root pitch (linear
  interpolation, sample-rate compensated) and keeps ringing across the loop boundary.
- On stop the voices fade out over 80 ms and the transport rewinds to the start.
- When a new snapshot arrives the player recomputes where each channel's next note is from the current position, so
  edits during playback take effect immediately and a project of a different length (switching between pattern and
  song mode) is handled by wrapping the position.

`OfflineRenderer` runs the same `Player` over one pass of the project plus a tail (default 1 s), with sample tails
allowed to ring out, and writes 16- or 24-bit WAV. An exported file therefore sounds like the live engine.

## Patterns and the playlist

`Arrangement` is the song as the user edits it: **patterns** (notes per channel, ticks relative to the pattern start)
and **clips** (pattern, track, start unit, length in units). One *unit* is one pattern length (two bars in the demo
project). Limits: 8 tracks, a 32-unit timeline, 64 patterns.

It is never played directly. It is *compiled* into an ordinary `Project`:

- `compilePattern(channels, arrangement, index)` - the loop of one pattern.
- `compileSong(channels, arrangement)` - every clip's notes laid on one timeline, repeated once per unit of the clip;
  the loop length is the end of the last clip (an empty playlist is one silent unit). Notes starting outside their
  pattern are skipped and notes are cut at the end of their pattern so they cannot leak into the next unit.

The audio engine, the `Player` and the renderer know nothing about patterns, so the verified real-time core is
unchanged by the arrangement feature.

`Arrangement::canPlace` enforces the clip rules (known pattern, inside the timeline, positive length, no overlap on
the same track); the playlist view only applies a move or resize that passes it. `MainComponent` keeps the *current
pattern's* notes in the channels (so the rack, the piano roll and the mixer edit `Channel` objects directly) and
copies them back into the arrangement (`commitCurrentPattern`) before switching pattern or compiling.

Channel settings (instrument, sample, volume, pan, mute, solo) belong to the channel and are shared by all patterns.

## User interface

The main window is a toolbar plus a `Workspace`; the Channel Rack, Playlist, Piano Roll and Mixer are `PanelWindow`
children of it. A `PanelWindow` has a title bar (drag, double-click to maximise), a close button and a
`ResizableBorderComponent`; a custom bounds constrainer keeps it inside the workspace. A click anywhere inside a panel
raises it. Panels do not own their content: `RackView`, `PianoRoll`, `MixerPanel` and `PlaylistView` are members of
`MainComponent`, declared before the panels so they outlive them.

`PanelLayout` (x, y, width, height, visible) has no JUCE dependency. It serialises to `"x y w h v"` for the settings
file, rejects malformed text (so a corrupt file falls back to the defaults) and `clampedTo()` keeps a panel inside
the workspace and above its minimum size. The workspace re-clamps every panel when the window is resized.

Settings live in `~/.config/SunLoopsStudio/SunLoopsStudio.settings` (a JUCE `PropertiesFile`), one key per panel
(`panel.rack`, `panel.piano`, `panel.mixer`, `panel.playlist`). Only the panel layout is stored; there is no project
file yet.

Keyboard focus: Space and F5/F6/F7/F9 are handled by `MainComponent`. Toolbar buttons and the pattern box do not take
keyboard focus, and the component reclaims it when the window is active and focus is elsewhere.

The rack rows and the mixer strips edit the same `Channel` objects and refresh each other through `MainComponent`.
Peaks are read from the engine once per timer tick (`consumeChannelPeak` resets the value) and handed to both.

## Testing

`tests/test_core.cpp` has no framework: each `test...()` function runs checks and `main` returns non-zero if any
failed. It covers:

- the project model (validation, sorting, clamping);
- synth pitch (single-bin DFT), sample-exact note timing, looping and stop, polyphony and voice stealing;
- mixer: volume, pan, mute, solo;
- sampler: pitch, sample-rate conversion, one-shot behaviour, safe replacement of a sample while it plays;
- that `Player::render` never allocates (the global `operator new` is replaced by a counting version);
- a concurrent stress test of `SnapshotExchange` (a reader on its own thread against a publishing writer);
- WAV round trip and the VST3 scanner on empty and invalid directories;
- `PanelLayout` parsing and clamping;
- the arrangement: placement rules, compilation of pattern and song, and audible results through the offline
  renderer (the first and second clip sound, the gap between them is silent).

Mutation checks were used while writing tests: deliberately breaking a rule in the code and confirming a test fails.
The user interface is checked by hand (see [building.md](building.md#tests)).
