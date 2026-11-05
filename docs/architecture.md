# Architecture

Sun Loops Studio is a JUCE 8 application in C++20. This document explains how it is put together and why, so that
you can change it without breaking the real-time audio path. For using the program see
[user-guide.md](user-guide.md); for building it see [building.md](building.md).

## Source map

```
src/main.cpp                    Application and main window (minimum size 1080x600)

src/audio/                      No UI dependencies except JUCE audio types
  NoteSequence.h                Note, 480 ticks per beat, note validation and sorting
  Project.h                     Channel, MixerTrack, SampleData, Project: the immutable snapshot the audio thread reads
  Effects.*                     EffectSlot (type, bypass, parameters) and EffectUnit: Equalizer, Reverb, Delay, Compressor
  Arrangement.h                 Patterns, clips, and compilation of pattern / song into a plain Project
  SnapshotExchange.h            Lock-free hand-over of snapshots from the UI thread to the audio thread
  Player.*                      Transport, synth and sampler voices, channels, mixer tracks and effects (real-time safe)
  AudioEngine.*                 Audio device, atomics (BPM, gain, play state, peaks, RMS), audio callback
  OfflineRenderer.*             Renders a Project to a buffer or WAV file with the same Player
  BuiltinSamples.*              Synthesised kick, snare and hat

src/library/                    No JUCE GUI; JUCE core only (files, JSON)
  BrowserModel.h                Browser tree nodes, which are draggable, search filter (plain C++)
  ChannelPreset.*               Channel presets as JSON (.slspreset) and the factory presets
  Library.*                     ~/Music/Sun Loops Studio folders, directory scan, building the browser tree

src/plugins/PluginScanner.*     VST3 discovery. Only feeds the browser's Plugin folder; no hosting

src/ui/
  MainComponent.*               Toolbar, workspace, panel wiring, patterns, layout persistence, export
  PanelWindow.*                 PanelWindow (floating panel) and Workspace (the area they float in)
  PanelLayout.h                 Panel position/size/visibility: parse, serialise, clamp (no JUCE)
  RackView.h  ChannelStrip.*    Channel Rack panel body and its rows (step grid, knobs, context menu, drop target)
  Knob.*  TrackSelector.*       Rotary knob with caption; the "Track N" display
  FileBrowser.*  DragItems.h    The browser sidebar; the text format of dragged items
  PianoRoll.*                   Note editor
  PlaylistView.*                Playlist panel body
  MixerPanel.*                  Mixer panel body: track strips, master strip, effects panel
  VuMeter.*                     Peak/RMS meter (mixer)
  AudioMeter.*                  Simple level meter (rack rows, toolbar)

tests/test_core.cpp             Headless tests (no audio device, no display)
```

`CMakeLists.txt` builds two targets from the same core sources (`SLS_CORE_SOURCES`: Player, Effects, BuiltinSamples,
OfflineRenderer, ChannelPreset, Library, PluginScanner): the application `SunLoopsStudio` and the console test program
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
32 voices (synth or sampler), mixes them per channel with smoothed gain and equal-power pan, sends each channel to its
mixer track, runs the track effects and fader, tracks per-track peaks and RMS and soft-limits the master with `tanh`.
It works in chunks of 64 samples so that note starts stay sample-accurate while the per-track buffers stay small.

- Resolution is 480 ticks per beat; the editors snap to 16th notes (120 ticks).
- **Pitch** (`Channel::pitch`, semitones, float) is applied when a note starts: the synth's frequency or the
  sampler's playback step. Notes already sounding are not retuned.
- **Swing** (`Channel::swing`, 0..1) is applied when the `Project` is made (`Project::applySwing`): a note on an odd
  16th is delayed by `round(swing * 60)` ticks, unless that would push it past the loop end, in which case it stays.
  The stored notes are unchanged, so turning swing down gives the original timing back.
- A synth voice is a sine with two harmonics and a short attack/release. It ends at the note length and at the loop
  boundary. A sampler voice plays its sample once, repitched by note relative to the channel's root pitch (linear
  interpolation, sample-rate compensated) and keeps ringing across the loop boundary.
- On stop the voices fade out over 80 ms and the transport rewinds to the start.
- When a new snapshot arrives the player recomputes where each channel's next note is from the current position, so
  edits during playback take effect immediately and a project of a different length (switching between pattern and
  song mode) is handled by wrapping the position.

`OfflineRenderer` runs the same `Player` over one pass of the project plus a tail (default 1 s), with sample tails
allowed to ring out, and writes 16- or 24-bit WAV. An exported file therefore sounds like the live engine.

## Live notes (playing by hand)

The piano roll can sound a note while a mouse button is held. This path is separate from the pattern, so it works with
the transport stopped.

```
PianoRoll (message thread)  --onNoteStarted/Stopped-->  MainComponent  --AudioEngine::liveNoteOn/Off-->
LiveNoteQueue (wait-free SPSC, 64 events)  -->  audio callback  -->  Player::liveNoteOn/Off  -->  voices
```

- `LiveNoteQueue` (`audio/LiveNotes.h`, plain C++) has one producer (the message thread) and one consumer (the audio
  thread). `push` returns false when full instead of overwriting; it never allocates or blocks.
- The audio callback drains the queue before `render`, using the snapshot it just acquired, so the voice sees the same
  channel, sample and mixer routing as the block that follows.
- A live synth voice has `live = true`: no end tick, and `releaseVoices` (loop boundary, transport stop) skips it. Only
  `liveNoteOff(channel, pitch)` or the safety limit (`Player::kMaxLiveSeconds`, 20 s of audio) releases it, so a lost
  "off" cannot leave a note sounding for ever. A sampler voice is an ordinary one-shot.
- It goes through the channel (pitch, volume, pan, mute) and the track (effects, fader, mute), like any other note.
- `PianoRoll::sound(pitch)` keeps at most one note sounding and pairs every start with one stop (also when the panel
  is hidden or its notes are replaced). `MainComponent` remembers which channel got the "on" so the "off" goes to the
  same one even if the selection changes in between.

## Mixer tracks and effects

Signal path, per block:

```
voices -> channel (smoothed gain, equal-power pan, channel mute/solo)
       -> mixer track bus (Channel::mixerTrack, 0..7)
       -> insert chain: slot 1 -> 2 -> 3 -> 4   (EffectUnit::process, pre-fader)
       -> track fader (0..1.5, smoothed) x balance (centre = unity), track mute/solo
       -> peak + RMS measurement -> master sum -> tanh -> master gain (AudioEngine)
```

- `MixerTrack { name, volume, pan, mute, solo, slots[4] }`; `Project::make` always yields exactly 8 tracks and clamps
  every field. A `Project` that arrives with fewer tracks (older code, tests) is padded with defaults.
- Track mute/solo are separate from channel mute/solo. If any track is soloed, non-soloed tracks are silent.
- `EffectSlot { type, bypass, params[5] }` is plain data inside the snapshot. `sanitised()` maps unknown types to
  empty, NaN/inf parameters to their defaults and clamps the rest, so a corrupt slot cannot reach the DSP.
- `EffectUnit` is the DSP behind one slot. **All its memory (reverb combs and all-passes, a 1 s delay line) is
  allocated in `Player::prepare`** for 8 x 4 = 32 units; about 0.5 MB each at 48 kHz, so ~16 MB, and proportionally
  more at higher sample rates. `process` never allocates. If a slot's type changes the old state is dropped; a
  bypassed slot clears its state when it is switched on again, so no stale tail comes back.
- Equalizer: low shelf 120 Hz, peaking mid (Q 0.8, frequency parameter), high shelf 6 kHz, as biquads; a band at 0 dB is
  skipped. Reverb: Freeverb layout (8 combs, 4 all-passes per side) scaled to the sample rate. Delay: up to 1 s with
  a glide when the time changes, a low-pass in the feedback path and a dry/wet mix. Compressor: peak detector
  (stereo-linked), feed-forward, threshold / ratio / attack / release / makeup.
- Meters: the audio thread keeps a peak per track (`consumeTrackPeak` reads and resets it on the UI timer) and a
  sliding mean square over ~300 ms (`getTrackRms`). `AudioEngine` does the same for the master. `ScopedNoDenormals`
  is active in the audio callback and the feedback loops flush tiny values.
- There is no VST3 hosting. An `EffectSlot` has a built-in `EffectType`; hosting would add a slot kind that points at
  a plugin instance owned outside the snapshot, which needs its own design (state, editors, threading).

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

Channel settings (instrument, sample, volume, pan, mute, solo, pitch, swing, mixer track) belong to the channel and
are shared by all patterns. The mixer tracks belong to the project as a whole (`MainComponent::tracks`); the
arrangement passes them through to `compilePattern` / `compileSong`, which hand them to `Project::make`.

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
(`panel2.rack`, `panel2.piano`, `panel2.mixer`, `panel2.playlist`) and `browser.open`. The keys were renamed from
`panel.*` in 0.5 because the default layout changed; old values are simply not read. Only layout is stored; there is
no project file yet.

Keyboard focus: Space and F5/F6/F7/F8/F9 are handled by `MainComponent`. Toolbar buttons and the pattern box do not take
keyboard focus, and the component reclaims it when the window is active and focus is elsewhere.

The rack rows edit `Channel` objects and the mixer strips edit `MixerTrack` objects; `MainComponent` publishes a
new `Project` after either changes. Changing a channel's track updates the "routed channels" text on the strips
(`updateTrackRouting`). Peaks and RMS are read from the engine once per timer tick and handed to the rack rows, the
mixer strips and the master strip.

### Browser and drag and drop

`MainComponent` is a `juce::DragAndDropContainer`. `FileBrowser` shows a `juce::TreeView` built from a `BrowserNode`
tree (`library/BrowserModel.h`, plain C++ so it can be tested). `buildBrowserTree` produces Samples (built-in sounds
plus a scan of the user's folder), Drum Kits, Instruments, Presets (factory plus the user's `.slspreset` files) and
Plugin (the result of the on-demand scan). A directory scan skips hidden entries, leaves out folders with nothing
usable, goes at most 6 levels deep and creates at most 5000 nodes; a note appears in the tree if the budget runs out.
`filterTree` implements the search: a case-insensitive substring match on names; a folder whose own name matches
keeps its whole subtree, otherwise it stays only if something below it matches (notes and actions are hidden).

A dragged item is described by a string `sls:<kind>:<payload>` (kinds: `file`, `builtin`, `instrument`, `preset`;
`drag::Item` parses and builds it). `ChannelStrip` and `RackView` are `DragAndDropTarget`s; both end in
`MainComponent::applyBrowserItem(channelIndex, item)`, which is also what double-clicking an item calls. Dropping on
the empty rack area passes index -1, which first creates a channel. Plugin items are not draggable.

The tree items call back into `MainComponent`, and some callbacks rebuild the tree. Doing that inside the item's own
event handler destroyed the item while it was running (a use-after-free that crashed the program when *Scan for
VST3 plugins...* was clicked). The item callbacks are therefore deferred with `MessageManager::callAsync` and a
`SafePointer`. The plugin scan itself runs on a background `juce::Thread` and reports back with `callAsync`.

Channel presets (`ChannelPreset`) are JSON: name, instrument, sample reference (`builtin:<id>` or an absolute path),
volume, pan, pitch, swing. `fromJson` rejects malformed text and clamps values; a missing sample file is reported in
the status line instead of failing.

## Testing

`tests/test_core.cpp` has no framework: each `test...()` function runs checks and `main` returns non-zero if any
failed. It covers:

- the project model (validation, sorting, clamping);
- synth pitch (single-bin DFT), sample-exact note timing, looping and stop, polyphony and voice stealing;
- channel mixing: volume, pan, mute, solo;
- live notes: the queue (order, overflow, two threads), held notes with the transport stopped, across the loop wrap
  and stop/start, two keys at once, pitch / mute / track mute, sampler, invalid channels, the 20 s limit, and no
  allocation;
- pitch (synth frequency by DFT, sampler step) and swing (which notes move, by how much, the loop-edge rule);
- mixer tracks: routing, fader, balance, track mute/solo, peak and RMS values, padding of short track lists;
- the four effects (equalizer gain at known frequencies, reverb tail and mix, delay time and feedback, compressor
  gain reduction and makeup), bypass, state reset on type change, silence in gives silence out, hostile parameters
  (NaN, out of range, unknown type), and the same chain running inside the player;
- sampler: pitch, sample-rate conversion, one-shot behaviour, safe replacement of a sample while it plays;
- that `Player::render` never allocates, including with all effects running (the global `operator new` is replaced
  by a counting version);
- the browser model (tree building, leaf kinds, which items are draggable, search filtering), channel presets (JSON
  round trip, rejection of bad input, factory presets) and the library scan (a temporary directory tree, hidden
  files, depth and node limits);
- a concurrent stress test of `SnapshotExchange` (a reader on its own thread against a publishing writer);
- WAV round trip and the VST3 scanner on empty and invalid directories;
- `PanelLayout` parsing and clamping;
- the arrangement: placement rules, compilation of pattern and song, and audible results through the offline
  renderer (the first and second clip sound, the gap between them is silent).

Mutation checks were used while writing tests: deliberately breaking a rule in the code and confirming a test fails.
The user interface is checked by hand (see [building.md](building.md#tests)).
