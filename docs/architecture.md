# Architecture

Sun Loops Studio is a JUCE 8 application in C++20. This document explains how it is put together and why, so that
you can change it without breaking the real-time audio path. For using the program see
[user-guide.md](user-guide.md); for building it see [building.md](building.md).

## Source map

```
src/main.cpp                    Application and main window (minimum size 1080x600)

src/audio/                      No UI dependencies except JUCE audio types
  NoteSequence.h                Note, 480 ticks per beat, note validation and sorting
  Project.h                     Channel, MixerTrack, Console (aux buses, VCA groups), SampleData, Project: the immutable snapshot the audio thread reads
  Automation.h                  Automation targets, ranges, clips, points, evaluation and editing helpers (plain C++)
  TimeStretch.*                 StretchSettings and stretchSample(): offline time-stretch / pitch-shift of a SampleData with Signalsmith Stretch
  SampleEdit.*                  Destructive edits of a SampleData (ranges, cut / crop / fades / gain / normalise ...), noise reduction (spectral gating), onset detection, grid slices, 32-bit float WAV writing (plain C++ over SampleData)
  Auditioner.h                  The editor's preview voice: lock-free hand-over to the audio thread, mixed after the master chain
  HostedPlugin.h                What the engine knows of a third-party plugin (0.17.0): process(), isInstrument(), isLoaded(); no JUCE
  SynthPatch.h                  The synth channel's sound (0.18.0): engine (Classic / Subtractive), oscillators, noise, filter, envelopes, LFO; factory patches (plain C++)
  SampleData.h                  SampleData (decoded audio) and finiteOr(), split from Project.h so that DrumKit.h and Project.h can include each other's needs
  SamplerProgram.h              The advanced sampler: SamplerSettings (envelope, loop, filter), SampleZone / SamplerProgram (multi-sample, key and velocity ranges), makeSliceProgram() (plain C++)
  DrumKit.h                     The drum machine model: 16 pads, 4 velocity layers, choke groups, swing; layerFor(), padForPitch(), sanitised() (plain C++)
  Loudness.h                    LoudnessMeter: BS.1770-4 K-weighting, gating, LUFS, loudness range, true peak (plain C++)
  Spectrum.h                    StereoRing (lock-free audio -> GUI hand-over), SpectrumAnalyser (own FFT), stereo correlation
  Effects.*                     EffectSlot (type, bypass, parameters) and EffectUnit: Equalizer, Reverb, Delay, Compressor
  Arrangement.h                 Patterns, clips, and compilation of pattern / song into a plain Project
  SnapshotExchange.h            Lock-free hand-over of snapshots from the UI thread to the audio thread
  Player.*                      Transport, synth and sampler voices, channels, mixer tracks and effects (real-time safe)
  AudioEngine.*                 Audio device, atomics (BPM, gain, play state, peaks, RMS, load), audio callback
  MixerRouting.h                Which mixer tracks have channels on them / are free (plain C++)
  BufferTuning.h                Buffer size logic: clean-trial rule and the BufferTuner search (plain C++)
  OfflineRenderer.*             Renders a Project to a buffer or WAV file with the same Player
  AudioExport.*                 Export to WAV, AIFF, FLAC, Ogg (.ogg/.oga) natively; MP3 and AAC through ffmpeg
  AudioImport.*                 Decoding audio files and extracting the sound of videos (ffmpeg) into SampleData
  InputRecorder.*               Audio input to a WAV file: wait-free hand-over to a writer thread
  MidiNotes.h                   MidiNoteMapper: MIDI messages -> live note events (plain C++ over juce::MidiMessage)
  TapTempo.h                    Tap tempo (plain C++)
  BuiltinSamples.*              Synthesised kick, snare and hat

src/library/                    No JUCE GUI; JUCE core only (files, JSON)
  BrowserModel.h                Browser tree nodes, which are draggable, search filter (plain C++)
  ChannelPreset.*               Channel presets as JSON (.slspreset) and the factory presets
  DrumKitFile.*                 Drum kits: KitSpec (what a kit refers to), JSON, kit folders (kit.json + samples/), kits from a folder of sounds, the built-in kits, loadKit()
  SoundFont.*                   SoundFont 2 presets to SamplerPrograms (parsed with third_party/tinysoundfont), ProgramSpec (what the project file stores), sampler settings as JSON
  JsonRead.h                    readNumber / readInt / readBool / readName / number: the range-clamping JSON helpers shared by the project, preset and kit files
  Library.*                     ~/Music/Sun Loops Studio folders, directory scan, building the browser tree
  ProjectFile.*                 The .slp project file (older .slsproj still read): ProjectDocument <-> JSON, safe read and write

src/plugins/PluginScanner.*     A plain VST3 file finder (older; kept for its tests). The browser uses PluginHost's scan since 0.17.0
src/plugins/PluginHost.*        Plugin hosting (0.17.0): JucePlugin (a VST3 / LV2 / LADSPA instance behind HostedPlugin: block processing, MIDI, state,
                                placeholders for missing plugins, a "changed" flag) and PluginHost (formats, scan with the dead man's pedal, known list,
                                creating instances, preparing them all again for a new sample rate)

src/ui/
  MainComponent.*               Toolbar, workspace, panel wiring, channels, layout persistence, the demo project, timer
  MainComponent{Patterns,Import,Recording,Notes,Browser,Project,Automation,Drums,SampleEditor,Channels,History,Safety,Sampler}.cpp
                                The rest of MainComponent's member functions, one file per area: patterns, pattern length and playlist; sample /
                                audio / MIDI import and audio clips; audio recording; MIDI-keyboard note recording; browser, presets, export;
                                project files, recent projects, the File menu; automation clips; the drum machine window; the sample editor;
                                deleting / moving / cloning channels; the project-wide undo history; autosave, crash recovery, backup and
                                export with samples
  AutomatableSlider.h           A slider whose right-click menu offers "Create automation clip"
  AutomationEditor.*            The point editor window for one automation clip
  MetersView.*                  The Meters window: loudness read-outs, true peak, spectrum, goniometer, correlation
  SamplerPanel.*                The sampler settings window (MainComponentSampler.cpp opens and wires it, loads SoundFonts, makes slice channels)
  SynthPanel.*                  The synth window (0.18.0; MainComponentSynth.cpp opens and wires it)
  PluginWindow.h                A plugin's window: its own editor or JUCE's generic one (0.17.0; MainComponentPlugins.cpp creates, wires and closes them)
  DrumMachine.*                 The drum machine window: pad grid, pad inspector, step grid per pad, kit menu (MainComponentDrums.cpp opens and wires it)
  PanelWindow.*                 PanelWindow (floating panel) and Workspace (the area they float in)
  PanelLayout.h                 Panel position/size/visibility: parse, serialise, clamp (no JUCE)
  RackView.h  ChannelStrip.*    Channel Rack panel body and its rows (step grid, knobs, context menu, drop target)
  Knob.*  TrackSelector.*       Rotary knob with caption; the "Track N" display
  FileBrowser.*  DragItems.h    The browser sidebar; the text format of dragged items
  PianoRoll.*                   Note editor
  PlaylistView.*                Playlist panel body: pattern box with New / Rename / Delete, tool buttons, grid, zoom, undo, readouts
  PlaylistTimeline.*            The timeline: ruler, clips with waveforms, the five tools, undo snapshots, file and browser drops, track arming
  MetronomePanel.*  RightClickButton.h   The metronome pop-up (tempo, volume, tap); a button with a right-click action
  AudioSettings.*               The Audio... dialog: device selector, buffer size menu, Auto, load read-out
  MixerPanel.*                  Mixer panel body: track strips (with sends and VCA switches), aux and VCA strips, master strip, effects panel
  AssignTrackDialog.*           The "Assign free mixer track" window
  VuMeter.*                     Peak/RMS meter (mixer)
  AudioMeter.*                  Simple level meter (rack rows, toolbar)

tests/test_core.cpp             Headless tests (no audio device, no display)
```

`CMakeLists.txt` builds two targets from the same core sources (`SLS_CORE_SOURCES`: Player, Effects, BuiltinSamples,
OfflineRenderer, AudioExport, AudioImport, InputRecorder, ChannelPreset, Library, ProjectFile, PluginScanner): the application `SunLoopsStudio` and the console test program
`SunLoopsStudioTests`.

## Threads and real-time rules

| Thread | What it does | Rules |
| --- | --- | --- |
| Message (UI) thread | Windows, editing, file dialogs, decoding samples, building a new `Project` | May allocate and block briefly |
| Audio thread | `AudioEngine::audioDeviceIOCallbackWithContext` -> `Player::render` | No allocation, no locks, no I/O, no logging |
| Import thread | A one-thread pool in `MainComponent`: decodes dropped audio, runs ffmpeg for AAC and video sound | Hands each result to the message thread with `callAsync` (guarded by a `SafePointer`) |
| Recorder thread | A `TimeSliceThread` in `InputRecorder`: writes the recorded blocks to the WAV file | Fed by a lock-free FIFO (`ThreadedWriter`) |
| MIDI threads | One per enabled MIDI input; call `AudioEngine::handleMidi` | Take a tiny spin lock shared with the message thread's live notes, then push to the wait-free queue; the audio thread never takes that lock |

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
- A synth voice of the **Classic** engine is a sine with two harmonics and a short attack/release. A **Subtractive** voice (0.18.0,
  `Player::renderSubtractive`) runs two oscillators at fixed ratios of the note's rate (`increment`, so legato and slide work unchanged):
  sine, triangle, and saw / square with PolyBLEP; white noise from a per-voice xorshift; the amplitude envelope uses the voice's existing
  attack / decay / sustain / release fields and the filter envelope its own; every 16 samples the LFO, the pitch and pulse-width modulation and
  the cutoff (base x 2^(envelope + key tracking + LFO) octaves) are computed and the state-variable filter's coefficients updated (one or two
  filters in a row). The patch is read from the voice's channel in the snapshot (plain values, nothing to free). Either kind ends at the note length and at the loop
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

## Recording notes, Cyber-Hub and the start-up sound

- **Note recording.** The audio thread stamps every keyboard note (`LiveNoteEvent::fromMidi`) with the song position minus the
  output latency and half a block (`makeTake`) and pushes a `MidiTake` into a `SpscQueue`; a message-thread timer drains it into
  `NoteRecorder` (quantize, wrap across the loop end, closing held notes on stop), whose notes `MainComponent` writes into the
  selected channel's current pattern. Nothing on the audio thread allocates or locks.
- **Cyber-Hub.** `resources/sounds/` is turned into C++ data by `juce_add_binary_data` (target `SunLoopsSounds`, namespace
  `SunLoopsSoundData`) and decoded by `CyberHub.cpp` with JUCE's Ogg Vorbis / WAV readers from memory, once, then cached. Ids are
  `cyber:<name>`; `BuiltinSamples` forwards them, so projects store `builtin:cyber:<name>`.
- **Start-up sound.** `OneShot` (header only) plays a `SampleData` once with linear interpolation; `AudioEngine` owns one, started by
  an atomic flag (`playStartupSound`) and mixed after the master volume.

## Audio clips

An `AudioClip` (in `Arrangement`) holds the decoded `SampleData` (whose `source` is the file path), a name, a playlist
track, a start in ticks (any tick; the grid is a UI matter), a length in ticks, a gain and an `offsetSeconds` (where in the sound the clip starts: non-zero after a trim or split from the left). `ClipEdit.h` holds the pure functions behind Split and Cut. `canPlaceAudio` enforces the timeline limits and
no overlap with other audio clips *or* pattern clips on the same track (and `canPlace` for pattern clips now checks the
audio clips). `compileSong` turns each clip into an `AudioEvent` (start, end, mixer track = playlist track, gain, shared
pointer to the audio) in `Project::audioEvents`, sorted by start; `compilePattern` has none, so audio only plays in Song
mode and in exports of the song.

`Player` keeps 16 clip voices, separate from the synth/sampler voices. Like notes, events start when `tickPos` reaches
`startTick` (`nextClip` is the cursor, reset on loop wrap, stop and `rewind()`); a voice reads the audio with linear
interpolation at `sampleRate / deviceRate`, is faded in over 2 ms, fades out over 80 ms when its end tick, a loop wrap or
a stop is reached, and adds to the mixer track's bus before the track's effects and fader. When a new snapshot arrives a
playing voice survives only if the new snapshot still holds the same audio (the data belongs to the snapshot).
`Player::rewind()` (audio thread) restarts the song, used by the engine when a recording begins.

The clip's *length* is stored (it was the audio's length at the tempo in force when it was added), but the sound always
plays at its real speed, so a tempo change can leave the clip shorter or longer than the sound. In the project file
(`"audioClips"`) only the path of each file is written; on opening, `ProjectDocument::fromJson` gives each clip a silent
placeholder (`makeMissingSample`) and `MainComponent::loadAudioClipFiles` decodes the files, keeping the placeholder (and
the path) for a missing file.

### Importing

`AudioImport` decodes with JUCE's readers (WAV, AIFF, FLAC, Ogg; `.oga` through `OggVorbisAudioFormat` directly; MP3 with
`JUCE_USE_MP3AUDIOFORMAT=1`), up to `kMaxImportSeconds` (300 s; longer files are cut with a warning). AAC and anything the
built-in readers refuse go through `ffmpeg -i in -vn -map 0:a:0 -t 301 -ac 2 -c:a pcm_s16le out.wav` and are then read the
same way. A video's sound is written the same way into `Samples/From videos/` under a unique name (through a `.part` file,
which is removed on failure) and the clip refers to that WAV. `PlaylistView` is a `FileDragAndDropTarget`: it highlights
the drop position and reports `(files, track, tick)`; `MainComponent::importMediaFiles` decodes on the import thread and
`finishImport` places the clips on the message thread (first free beat from the drop position, shortened to the free
space if needed).

## Recording and MIDI

`InputRecorder::start` opens a 24-bit WAV through a `ThreadedWriter` with a 3-second FIFO. `process()` (audio thread)
measures the peak, drops the first `skipFrames` (the round-trip latency the device reports, so that the take lines up with
the song that was playing) and writes the block; a start/stop handshake (`active` and `inProcess`, both sequentially
consistent) lets `stop()` wait until the audio thread has left `process()` before the writer is destroyed, so the audio
thread takes no lock. `AudioEngine::startRecording` sets the transport playing *before* arming the recorder; the callback
checks the recorder first and the transport second, and calls `Player::rewind()` in the first block that sees the recorder
armed, so the take and the song start in the same block. `AudioEngine::enableDefaultInput` switches on the device's first
input channels (the engine opens the device with inputs available but none enabled, so no microphone is open until the
user chooses one or records). Input monitoring adds the input to the output before the master volume is applied.

`MainComponent` finishes a take (also when the transport is stopped or a limit is reached), reads the WAV back, and places
it at tick 0 of the armed track, cut to the free space.

MIDI: the engine registers one `MidiInputCallback` for all enabled inputs. `MidiNoteMapper` turns note on/off (velocity 0
included), all-notes-off and all-sound-off into `LiveNoteEvent`s for the selected channel and remembers which channel
got each note on, so the note off cannot be misrouted when the selection changes. The producers (message thread and MIDI
threads) share a spin lock in front of the single-producer `LiveNoteQueue`; the audio thread only pops. A
`MidiDeviceListConnection` switches on keyboards plugged in while the program runs.

## Mixer tracks and effects

Signal path, per block:

```
voices -> channel (smoothed gain, equal-power pan, channel mute/solo)
       -> mixer track bus (Channel::mixerTrack, 0..7)
       -> insert chain: slot 1 -> 2 -> 3 -> 4   (EffectUnit::process, pre-fader)
       -> track fader (0..1.5, smoothed) x balance (centre = unity), track mute/solo
       -> peak + RMS measurement -> master sum
       -> master insert chain: slot 1 -> 2 -> 3 -> 4   (Project::masterSlots, on the sum, before the master fader)
       -> tanh -> master gain (AudioEngine)
```

- `MixerTrack { name, volume, pan, mute, solo, slots[4] }`; `Project::make` always yields exactly 8 tracks and clamps
  every field. A `Project` that arrives with fewer tracks (older code, tests) is padded with defaults.
- The master bus has its own chain, `Project::masterSlots` (4 `EffectSlot`s, sanitised in `Project::make` and passed through
  `compilePattern` / `compileSong`), run by `Player::masterEffects` (4 more `EffectUnit`s, allocated in `prepare`). It
  sits before the soft limiter and before the master volume, so the offline renderer and the export include it. The UI
  keeps it in a `MixerTrack masterTrack` (only `slots` used) so the same `FxPanel` edits it; `kMasterIndex` (= `kMaxTracks`)
  stands for the master wherever a track index is passed (`MixerPanel::setSelectedTrack`, `refreshTrack`, `onTrackChanged`).
  The project file stores it as `"master": {"effects": [...]}`; files without it open with an empty chain.
- Track mute/solo are separate from channel mute/solo. If any track is soloed, non-soloed tracks are silent.
- `EffectSlot { type, bypass, params[5] }` is plain data inside the snapshot. `sanitised()` maps unknown types to
  empty, NaN/inf parameters to their defaults and clamps the rest, so a corrupt slot cannot reach the DSP.
- `EffectUnit` is the DSP behind one slot. **All its memory (reverb combs and all-passes, a 1 s delay line) is
  allocated in `Player::prepare`** for 11 x 4 = 44 units (8 tracks, the master and the two aux buses); about 0.5 MB each at 48 kHz, so ~22 MB, and proportionally
  more at higher sample rates. `process` never allocates. If a slot's type changes the old state is dropped; a
  bypassed slot clears its state when it is switched on again, so no stale tail comes back.
- Equalizer: low shelf 120 Hz, peaking mid (Q 0.8, frequency parameter), high shelf 6 kHz, as biquads; a band at 0 dB is
  skipped. Reverb: Freeverb layout (8 combs, 4 all-passes per side) scaled to the sample rate. Delay: up to 1 s with
  a glide when the time changes, a low-pass in the feedback path and a dry/wet mix. Compressor: peak detector
  (stereo-linked), feed-forward, threshold / ratio / attack / release / makeup.
- Meters: the audio thread keeps a peak per track (`consumeTrackPeak` reads and resets it on the UI timer) and a
  sliding mean square over ~300 ms (`getTrackRms`). `AudioEngine` does the same for the master. `ScopedNoDenormals`
  is active in the audio callback and the feedback loops flush tiny values.
- Plugin effects (0.17.0): `EffectType::Plugin` with `EffectSlot::plugin` (a `shared_ptr<HostedPlugin>`). `EffectUnit::process` hands the
  chunk to the plugin; bypass leaves the signal alone. See *Plugins* below.

### Plugins (0.17.0)

The engine sees a plugin only through `HostedPlugin` (`audio/HostedPlugin.h`): `process (left, right, n, events, numEvents)`. The JUCE side,
`JucePlugin` in `plugins/PluginHost.cpp`, owns the `AudioPluginInstance`, a preallocated buffer (at least two channels, `Player::kChunk`
samples) and a preallocated `MidiBuffer`; `process` copies the chunk in, calls `processBlock` under a *try*-lock of the plugin's callback lock
(it never waits: a plugin being reconfigured is skipped for that chunk) and copies stereo back. A placeholder (the plugin is not installed)
has no instance: effects pass the sound through, instruments are silent, and the saved state is kept for the next save.

- **Instruments**: `Channel::plugin` on an `Instrument::Plugin` channel. In `Player`, note starts and ends (pattern, song, live notes, MIDI
  keyboards) become 3-byte MIDI events at their sample inside the current 64-sample chunk (`pluginNoteOn`, at most 256 events per channel
  and chunk, 256 sounding notes tracked so their note-offs are sent at the note end, at a loop or jump and at Stop; the notes of a
  plugin that leaves the project are forgotten).
  After the voices, each plugin channel renders its chunk into the channel's mix. No allocation (tested over 2000 blocks).
- **Ownership and threads**: plugins are `shared_ptr`s in the channels and slots, so the snapshots, the undo history and the open document
  share them; the last reference is dropped on the message thread (snapshots are freed there). `PluginHost` is declared before the engine in
  `MainComponent`, so every instance goes before the formats (modules) that made it. Creating, preparing, opening editors and reading
  state happen on the message thread; `prepare` is called when the device starts (`AudioEngine::onDeviceStarting`, audio stopped) and around
  an export, while `AudioEngine::setSuspended (true)` keeps the callback out of the `Player` (the export renders the same instances at 48 kHz).
- **Project file**: a `PluginSpec` per plugin (place: channel / track / master / aux, index, slot; the `PluginDescription` as XML text; the
  state from `getStateInformation` in JUCE's base64). `MainComponent::makeDocument (true)` asks every plugin for its state; the undo history
  uses `makeDocument (false)`, which names a plugin by its instance (cheap, and enough to see that a slot changed). Edits made in a plugin's
  window raise its "changed" flag (`AudioProcessorListener`), which the UI timer turns into an unsaved-changes mark.
- **Scan**: `PluginHost::scan` runs JUCE's `PluginDirectoryScanner` per format on a background thread with a dead man's pedal file in the
  settings folder; `distributionPaths` adds the `lib64` folders JUCE's defaults miss on Fedora. The known list is `plugins.xml` next to the
  settings and is read at start-up.
- **Tests**: `tests/plugins/TestPlugins.cpp` builds two JUCE plugins (a sine synth and a gain with a saved parameter) as VST3 and LV2;
  the test program scans them, plays and measures them through the `Player`, saves and restores state, and checks placeholders.

### Time-stretch and pitch-shift (0.12.0)

`stretchSample (input, StretchSettings)` renders a whole `SampleData` offline with Signalsmith Stretch (`third_party/signalsmith`, header-only,
compiled as a `SYSTEM` include so its warnings are not ours): `seek` with the first `inputLatency` samples, `process` the rest plus `inputLatency`
samples of silence into `round(frames x ratio)` output frames, `flush` the last `outputLatency`, and drop the first `outputLatency` samples, so the result
is exactly `ratio` times as long and aligned with the input. Pitch is `setTransposeSemitones` (with a tonality limit of 8 kHz in *keep timbre* mode); the
length ratio is the ratio of the buffer sizes. The result keeps `name` and `source`.
An `AudioClip` has `data` (what plays), `original` (the file's audio, only while stretched) and `StretchSettings stretch`; `restretchClip` always renders from
`original` and scales `lengthTicks` and `offsetSeconds` by the change of the ratio. **The audio thread knows nothing about it**: `compileSong` still hands
`clip.data` to the player. The project file keeps `"stretch": {lengthRatio, semitones, keepTimbre}` per audio clip; `loadAudioForClips` (AudioImport.cpp, called
by `MainComponent::loadAudioClipFiles`) reads the files and renders each distinct (file, settings) pair once.

### Sample editor (0.13.0)

`SampleEdit.*` has only pure functions on `shared_ptr<const SampleData>`: each returns a **new** sample (or `nullptr` when there is nothing to do, or the
same pointer when nothing changed) and never touches its input, so the editor's undo stack is just a vector of pointers (trimmed by a memory budget and
40 steps). `reduceNoise` is an STFT (2048 frames, hop 512, Hann analysis and synthesis, normalised by the overlap sum): the threshold per bin is
mean + sensitivity x standard deviation of the magnitude over the noise selection, a bin is "open" when it is above its threshold, the 0/1 mask is
averaged over neighbouring bins, and the gain moves to `floor + (1 - floor) x mask` at once upwards but falls only by the release factor per frame; one profile per channel. `detectOnsets` is spectral flux of `log1p(50 x magnitude)`, a local median threshold, peak
picking within +-3 frames, an energy-growth gate (to reject the truncated tail of a hit) and a refinement to where the waveform really starts to rise.
`writeSampleWav` writes 32-bit float and refuses to replace a file; `uniqueEditedFile` picks `<name>-edit.wav`, `-edit-2.wav` ... in `Samples/Edited`.

`Auditioner` is a single voice that plays a `shared_ptr<const SampleData>` range, optionally looping. The UI thread writes the request and publishes it with an
atomic flag; `AudioEngine::audioDeviceIOCallback` calls `audition.mixInto` after the startup sound and the master gain. It takes no lock and allocates nothing;
`stop()` waits (briefly) until the audio thread has left `mixInto`, so the sample the UI is about to release cannot be in use. The editor window
(`SampleEditor`) feeds the spectrum from the playing position with the same `SpectrumAnalyser` as the meters.

Applying: `MainComponent::openSampleEditorForClip` opens the editor on exactly the part of the audio the clip plays (`cropToRange`); on Apply it writes the WAV,
reads nothing back (the sample in memory is used, `source` set to the new path) and calls `PlaylistTimeline::useEditedAudio`, which runs `sls::useEditedAudio`
(ClipEdit.h, pure, tested) on a copy of the arrangement: the clip gets the new audio from its start, the longest length that fits (binary search on
`canPlaceAudio`), and `splitAudioClip` at each slice point; only a successful result is committed, as one undo step. `openSampleEditorForChannel` replaces
the channel's sample instead. The project file is unchanged: clips and channels still store only the path.

### The advanced sampler (0.16.0)

A sampler channel has `SamplerSettings` (always) and may have a `SamplerProgram` (`shared_ptr<const ...>`, immutable like a kit): a list of zones, each with
its own sample, key and velocity range, root key, tuning, key tracking, gain, pan and loop. `Player::noteOn` starts one voice per answering zone (at most
four; a stereo SoundFont sample is two zones), or one voice for the channel's own sample, through `samplerVoiceOn`. A sampler voice carries its loop points
(frames), the envelope steps (`attackStep`, `decayStep` towards `sustainLevel`, `relStep`), the zone's gain and constant-power pan (the same fields the drum pads
use) and, when the filter is on, the coefficients and state of a topology-preserving state-variable filter (Simper), computed once per note. A voice is `held`
when the envelope is on or it loops: it then ends at the note's end like a synth voice (and `releaseVoices` always releases it); otherwise it is the old
one-shot. In the render loop the loop wraps the position, interpolation reads the loop start after the loop end, the filter runs per sample and the decay
happens inside `State::Sustain` (no new voice state, so every existing "is sounding" test still holds). After a new snapshot a voice survives only if the
channel's sample or program still owns its sample.

SoundFonts are read by TinySoundFont's parser only (`tsf_load_filename`; its synthesizer is not used): each region of the preset becomes a zone, its slice of
`fontSamples` is copied once into a `SampleData` and shared by the zones that use it. The project file stores `"sampler"` settings and a `"program"` spec
(`soundfont`: file and preset; `slices`: file, slice starts and first key); `applyDocument` loads the program again, or keeps a zone-less placeholder that
remembers the spec when the file is missing.

### Seeking, the loop region and the undo history (0.15.0)

The UI asks for a new song position with `AudioEngine::requestSeek (tick)` (an atomic); the audio callback hands it to `Player::seek` before `render`. A seek
releases the sounding voices (as Stop does), moves `tickPos`, re-locates the next note of every channel and the next audio clip (`locate`, a binary search per
channel, the same code `onProjectChanged` uses) and, while playing, starts the audio clips that cover the new position from the matching frame
(`startClipsInside`). It also becomes `startPosition`, where Stop puts the transport (before 0.15.0 Stop always went to 0). The loop region travels in the
snapshot (`Project::loopStartTick / loopEndTick`, set by `MainComponent::publishProject` in Song mode only, so exports never see it): in the per-sample transport
step, crossing `loopEndTick` from before it calls `jumpTo (loopStart + overshoot)`, and the end of the song jumps to the region instead of wrapping to 0. All of it is
allocation-free and covered by a test that seeks and loops for 2000 blocks while counting allocations.

The project-wide undo (`MainComponentHistory.cpp`) records, at every `publishProject`, a copy of the editable state (channels, arrangement, tracks, master chain,
console, current pattern, tempo, master volume) together with its project-file text; the text decides whether anything changed and the state before an edit is
pushed unless the edit continues a mouse gesture (button still down). Restoring rebuilds the strips (they hold references into `channels`) and refreshes the mixer;
the Playlist's and the old note histories are cleared because their steps no longer match. Deleting, moving and cloning channels go through
`remapArrangementChannels` (Arrangement.h, plain C++, tested): one list "new channel n comes from old channel k" moves the notes of every pattern and the channel
automation clips.

### Drum machine (0.14.0)

A drum channel is `Instrument::Drums` with `Channel::kit`, a `shared_ptr<const DrumKit>` (immutable: an edit copies the kit, changes the copy and replaces the pointer, like a sampler channel's
sample). `Project::make` gives every drum channel a sanitised kit (and no other channel one) and applies each pad's own swing (`applyPadSwing`) instead of the channel swing. Pad *n* answers to
note 36 + *n*, so a drum channel's notes are ordinary notes: the sequencer, the piano roll, live notes and MIDI input need nothing new.

`Player::noteOn` hands a drum note to `drumNoteOn`: pad lookup, mute / solo, `DrumPad::layerFor (velocity)`, the choke group (every sounding voice of the same channel and group goes to `Release`), then a one-shot
sampler voice from the shared voice pool (`takeVoice`, the old allocation code) that also carries `pad`, `choke`, `padGain` and the pad's pan factors (constant power, 1.0 at the centre, so a centred pad is as loud as a
sampler voice). The render loop multiplies them in; nothing else changed there. After a new snapshot, a drum voice survives only if the new kit still owns its sample (`DrumKit::owns`). Legato / slide never apply to
drums. The tests count allocations while a busy pattern plays and swap the kit under sounding voices.

`library/DrumKitFile` describes a kit as a `KitSpec` (names, settings, a source string and a minimum velocity per layer) and reads / writes it as JSON for both the project file (`"kit"` inside a `"drums"` channel,
`ProjectDocument::kitSpecs`) and the kit file. `loadKit` reads every distinct source once; unreadable sources become `makeMissingSample` placeholders that keep the path. `saveKitFolder` writes `kit.json` (temporary
file, then rename) and copies non-built-in sounds into `samples/` with relative paths; `readKitFolder` also accepts a plain folder of audio files. The JSON readers (`readNumber` ...) moved to `library/JsonRead.h`.

The window (`ui/DrumMachine.*`) never holds a copy of the channel: `getChannel` returns the live channel, kit edits go through `editKit` (copy, change, replace, `onKitChanged`), and steps are edited in
`Channel::notes` directly with one `onNotesChanged` at the end of a mouse gesture (undo through the existing note history). `MainComponent::publishProject` refreshes the window, so changes from the piano roll, pattern
switches or undo show up. The window is closed when a project is opened (it points at a channel of the old project).

### Aux buses, sends and VCA groups (0.11.0)

`Project::console` holds `aux[2]` (each a `MixerTrack`: name, fader, balance, mute, four effect slots) and `vcaLevel[2]` /
`vcaMute[2]`. Each `MixerTrack` has `sends[2]`, `sendsPreFader` and `vcaMask` (bit n: member of VCA n); `Project::make` clamps all of them.
In `Player`, per 64-sample chunk, after a track's effects: the VCA factor is the product of the `vcaLevel`s of its groups (0 if one is
muted); the fader gain is multiplied by it; a **post-fader** send takes the signal after fader, balance and VCA, a **pre-fader** send takes the
signal after the effects only; a track silenced by mute or by another track's solo sends nothing. Send levels are smoothed (`sendGain`)
like the fader. The sends are summed into `auxL/auxR`, run through the bus's `EffectUnit`s (allocated in `prepare`: 2 x 4 more units, ~4 MB),
the bus fader and balance, metered (`getAuxPeak/Rms`) and added to the master sum **before** `masterEffects`. A bus cannot send to a bus.
Index space used by the UI: tracks 0..7, `kMasterIndex` = 8, `kAuxIndexBase` = 9 (aux A = 9, aux B = 10). The project file keeps
`"console"` (aux buses, VCA levels and mutes) and per track `sends`, `sendsPreFader` and `vca` (the bit mask); older files open with defaults.

### Automation (0.11.0)

`Automation.h` is plain C++. An `AutomationTarget` names a parameter (`ChannelVolume`, `ChannelPan`, `TrackVolume`, `TrackPan`, `TrackEffect`,
`MasterEffect` plus index, slot and parameter); `automationRange` gives its min / max / default. An `AutomationClip` has a lane (0..3), a start
tick, a length and points (tick as a double, value, shape Linear / Hold / Smooth). `Project::make` turns the clips into a sorted vector of
`AutomationEvent`s (`makeAutomationEvents`): clips with unknown targets, a missing effect or a different effect type are dropped, values and
ticks are sanitised, lengths and counts are limited. Evaluation is stateless: for a parameter, the latest-started clip that has begun decides;
the curve is read at `min(tick, end)`, so after the end the last value is held. The `Player` applies it once per 64-sample chunk, in Song mode only,
to the channel gains, the track faders / balances and **copies** of the effect slots (so the snapshot stays immutable; the effect parameters are
smoothed by the effect itself). The UI side: `AutomatableSlider` raises `onAutomate`, `MainComponent::createAutomationFor` makes a flat clip
and `Playlist` places it on a lane; `PlaylistTimeline` draws and edits the lanes with the same undo snapshots as other clips; `AutomationEditor`
edits the points through `insertAutomationPoint`, `moveAutomationPoint` and `removeAutomationPoint`. The project file keeps `"automation"`.

### Metering (0.11.0)

The audio callback feeds the final stereo output to `LoudnessMeter` (BS.1770-4: stage-1 shelf and RLB high-pass K-weighting coefficients
computed for the actual sample rate, 400 ms blocks hopping 100 ms for momentary and 3 s for short-term, histograms of 0.1 LU for the absolute
(-70 LUFS) and relative (-10 LU, and -20 LU for the range) gates, 10th to 95th percentile range, 4x polyphase-sinc true peak) and to a `StereoRing`
(lock-free, single writer, single reader). The Meters window, on the message thread, reads the loudness values, copies the last 4096 samples from the
ring, applies a Hann window and the library-free radix-2 FFT of `SpectrumAnalyser`, and draws the spectrum, goniometer (last 1024 samples) and the correlation.
`Reset` sets a flag (`requestReset`) that the audio thread acts on. The meter's buffers are sized in the constructor; there is no allocation test for `AudioEngine`'s callback itself (the tests call the meter directly).

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

## Audio buffer and Auto

`AudioEngine::initialise()` opens the device from the state saved in the settings file (`audio.device`, JUCE's
`createStateXml()`, rewritten by `MainComponent` whenever the device manager changes), or, the first time, the default
device with a 1024-sample buffer (`kDefaultBufferSize`). The audio callback times itself with the high-resolution
clock (`clock_gettime`, no locks or allocation) and raises an atomic `peakLoad` to the longest block time divided by
the block's duration; the message thread reads and resets it with `consumePeakLoad()`. `getXRunCount()` forwards the
audio system's own count of dropped blocks (ALSA and JACK report it; -1 means unknown).

`setLoadTest(f)` makes each block spin until it has taken `f` of its duration, measured *after* `peakLoad` is
recorded, so the reported load is still the song's own cost. Only the Auto button turns it on (35 %).

`BufferTuner` (`BufferTuning.h`, no JUCE) decides: it is given the candidate sizes, asks for a trial of the smallest,
is told `(xruns, peakLoad)` for it, and asks for the next. A trial is *clean* when no block was dropped (unknown counts
as none) and `peakLoad <= 0.5`. It finishes at the first clean trial whose predecessor was clean too and returns that
trial's size (one step of margin that was itself measured); if only the last candidate is clean it returns that; if
none is, it returns the largest and `struggling()` is true. `AudioSettingsComponent` drives it from a 100 ms timer:
set the size, wait 0.5 s for the restarted device, measure for 2 s, record, repeat. Cancelling or destroying the
dialog mid-test restores the previous size and play state.

## Project files

`ProjectDocument` (`src/library/ProjectFile.h`) is everything that makes up a song: the channels without their notes,
the sample source of each channel, the 8 mixer tracks with their effect chains, the `Arrangement` (patterns and
playlist clips; the notes live in the patterns), the current pattern, Song mode, BPM and master volume. It has no
audio data and no JUCE GUI types, so it is tested headless.

`toJson()` writes `{"format":"sun-loops-studio-project","version":1, ...}`; effect types are written as strings, notes
as `[pitch, start, length, velocity]`. `fromJson()` is strict about structure (format name, a version this program
understands, expected types, 1-8 channels, 1-64 patterns, at most 20000 notes per channel and pattern) and lenient
about values: numbers are clamped into their valid range, NaN becomes the default, notes that do not fit the
pattern and clips that cannot be placed are dropped and reported in `warnings`. A file over 32 MB is refused without
being read. A version newer than 1 is refused ("written by a newer version"). The file is treated as untrusted input: every
value goes through the limits the engine expects before it becomes a `Project`.

Samples are not embedded. A channel stores `builtin:<id>`, or the absolute path of its file. `MainComponent` loads each
one when the project is applied; a source that cannot be loaded becomes `makeMissingSample()`: a `SampleData` with no
frames (silent) that still carries the `source` string, so saving again does not lose the reference.

`writeProjectFile()` writes to a `juce::TemporaryFile` next to the target and then replaces the target, so a failed
write leaves the old file intact; it returns an error string. `withProjectExtension()` adds `.slp` to a name that
lacks it (names ending in `.slp` or the older `.slsproj` are left alone; `projectFileForSaveAs()` turns an old `.slsproj` into
`.slp`), and `automaticProjectFile()` makes `Untitled <date> <time>.slp` in `Library`'s `projects` folder
(`~/Music/Sun Loops Studio/Projects`), never an existing name.

In the interface, `MainComponent` turns its state into a document (`makeDocument`) and back (`applyDocument`, which
stops playback, rebuilds channels, tracks and views and publishes one new `Project`). A project with no file is
saved by `saveProject()` straight to an automatic name; `saveProjectAs()` asks. `projectDirty` is set by `markDirty()`,
which `publishProject()`, the sliders and the playlist call; it is not set while a project is being loaded
(`ignoreDirty`) nor for view-only changes (selecting a pattern, Pattern/Song). `confirmDiscardChanges()` is the single
gate used by New, Open and closing the window (`main.cpp` asks it from `systemRequestedQuit`). The window title is
`<name>[ *] - Sun Loops Studio`. The recent list is kept in the settings file.

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
`panel.*` in 0.5 because the default layout changed; old values are simply not read. The settings file holds only layout and the
recent-projects list; songs are saved as project files (see above).

Keyboard focus: Space and F5/F6/F7/F8/F9 are handled by `MainComponent`. Toolbar buttons and the pattern box do not take
keyboard focus, and the component reclaims it when the window is active and focus is elsewhere.

The rack rows edit `Channel` objects and the mixer strips edit `MixerTrack` objects; `MainComponent` publishes a
new `Project` after either changes. Changing a channel's track updates the "routed channels" text on the strips
(`updateTrackRouting`). Peaks and RMS are read from the engine once per timer tick and handed to the rack rows, the
mixer strips and the master strip.

"Assign free mixer track" (channel right-click menu) opens `AssignTrackComponent` in a `DialogWindow`. It lists the 8
tracks with the instruments on each (`channelsPerTrack`), marks the free ones, preselects `firstFreeTrack`, and on
Assign calls `MainComponent::assignChannelToTrack`, which sets `Channel::mixerTrack`, refreshes the strip's Track display
and the routing text, selects the track in the mixer and publishes the project (so it also marks the project changed).

### Browser and drag and drop

`MainComponent` is a `juce::DragAndDropContainer`. `FileBrowser` shows a `juce::TreeView` built from a `BrowserNode`
tree (`library/BrowserModel.h`, plain C++ so it can be tested). `buildBrowserTree` produces Samples (built-in sounds
plus a scan of the user's folder), Drum Kits, Instruments, Presets (factory plus the user's `.slspreset` files) and
Plugin (the result of the on-demand scan). A directory scan skips hidden entries, leaves out folders with nothing
usable, goes at most 6 levels deep and creates at most 5000 nodes; a note appears in the tree if the budget runs out.
`filterTree` implements the search: a case-insensitive substring match on names; a folder whose own name matches
keeps its whole subtree, otherwise it stays only if something below it matches (notes and actions are hidden).

A dragged item is described by a string `sls:<kind>:<payload>` (kinds: `file`, `builtin`, `instrument`, `preset`, `plugin`;
`drag::Item` parses and builds it). `ChannelStrip` and `RackView` are `DragAndDropTarget`s; both end in
`MainComponent::applyBrowserItem(channelIndex, item)`, which is also what double-clicking an item calls. Dropping on
the empty rack area passes index -1, which first creates a channel. A plugin item's payload is `instrument|<id>` or `effect|<id>`
(JUCE's identifier string): an instrument becomes the channel's instrument (or a new channel), an effect goes into the channel's track.

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
- master effects (a delay tail, bypass, the compressor on the sum of two tracks, hostile parameters, no allocation with a
  full chain, a removed effect's tail stops, pattern and song compilation carry the chain) and the free-track helpers
  (`MixerRouting.h`: first free track, a channel's own track, shared tracks, all eight used, out-of-range numbers);
- the project file: a rich document survives write and read unchanged; the loaded project renders exactly
  the same audio as the original; sample sources (built-in, path, missing) and effect chains are kept; and every kind
  of bad input is rejected or repaired as described above (wrong format name or version, unknown instrument, NaN, out-of-range
  values, impossible notes and clips, no or too many channels, no patterns, empty, garbage and truncated files, a folder
  instead of a file), plus atomic writing (a failed write leaves the old file untouched, no temporary file is left
  behind) and the automatic file name. The 32 MB size limit has no test of its own;
- a concurrent stress test of `SnapshotExchange` (a reader on its own thread against a publishing writer);
- WAV round trip and the VST3 scanner on empty and invalid directories; plugin hosting with real VST3 / LV2 test plugins (0.17.0);
- export to every format (round trip with length and sound; ffmpeg formats decoded again with ffmpeg; no temporary
  files left; a missing or failing ffmpeg writes nothing and never damages an existing file);
- importing: every format, mono, the length limit, damaged/missing files, AAC without ffmpeg, the sound of a video (and a
  video without sound, a second import never replacing a file);
- audio clips: placement rules against audio and pattern clips, compilation, playing at the right time and speed, cut at
  the end tick, loop wrap, mixer-track mute and meter, snapshot changes while playing, no allocation, export, project
  file (round trip, bad clips dropped, older files);
- the recorder (stereo, mono, one input, skipped frames, no input, refusals, no allocation on the audio thread, stopping
  while a thread is busy calling `process`), MIDI note mapping (also through the queue into the Player), `rewind()`, the
  metronome (timing, accent, BPM, volume, never in an export) and tap tempo;
- `PanelLayout` parsing and clamping;
- automation: evaluation (shapes, held last value, overlapping clips, hostile numbers), editing helpers, the audible effect of every target through
  the offline renderer, Song mode only, the project file round trip and old files;
- the console: sends (pre / post fader, mute and solo), aux bus effects and fader, bus before the master chain, VCA scaling, two groups, VCA mute,
  hostile values and the project file;
- the drum machine: pad / note mapping, layer choice, choke, level / pan / tuning, mute / solo, per-pad swing, the kit model, kit files (JSON, folders, hostile files) and the project file;
- the sample editor: every edit operation (ranges, cut / crop, fades, normalise, DC), noise reduction against a tone in noise, onset detection and grid slices,
  the WAV writer, the preview voice (including stop while the audio thread runs) and `useEditedAudio`;
- time-stretch: length, pitch and timing of test tones at four sample rates, edge cases, the clip arithmetic, tempo matching and the project file;
- metering: K-weighting response, the -23 dBFS reference at three sample rates, gating, loudness range, true peak, FFT bin and level, correlation, the ring;
- the arrangement: placement rules, compilation of pattern and song, and audible results through the offline
  renderer (the first and second clip sound, the gap between them is silent).

Mutation checks were used while writing the earlier tests: deliberately breaking a rule in the code and confirming a test fails.
The user interface is checked by hand (see [building.md](building.md#tests)).
