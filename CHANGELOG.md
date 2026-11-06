# Changelog

All notable changes to Sun Loops Studio. Newest first.

## 0.12.0 - 2026-11-06

### Added
- **Time-stretch and pitch-shift of audio clips.** Right-click an audio clip > *Stretch and pitch...*: make it longer or shorter (25 % to
  400 %) without changing its pitch, change its pitch (-24 to +24 semitones, decimals allowed) without changing its length, or both. Type
  the **tempo the audio was recorded at** and the length is set so that it plays in time at the project tempo. A *Keep the timbre* mode limits how
  far high frequencies are moved when pitch-shifting. *Back to the original* undoes it for good (the file is never touched); the playlist's Undo works too.
  The clip changes length by the same factor, its name shows the change (`[x1.50, +3 st]`), a split keeps it on both halves, and the project
  file stores only the settings (the audio is processed again when the project is opened; clips with the same file and settings share one rendering).
  The processing is done by [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) (MIT) and its FFT library, copied
  unchanged into `third_party/signalsmith` with their licences. It runs offline, once, when you press *Apply*: about 40 times faster than the audio lasts
  on the test machine, so a 3-minute clip takes a few seconds; audio longer than about 15 minutes is refused.

## 0.11.0 - 2026-11-06

### Added
- **Automation.** Right-click a control and choose *Create automation clip*: the channel volume and pan (Channel Rack), a track's fader
  and balance (Mixer) and every parameter of an effect on a track or on the master. The clip (one pattern long, a flat line at the
  control's current value) lands on one of **four automation lanes under the playlist tracks** and its editor opens. In the lanes
  a clip is moved, lengthened or shortened by its right edge, duplicated (Ctrl+D), deleted or erased like any other clip; double-click
  (or right click > *Edit the points...*) opens the **point editor**: click to add a point, drag to move it (time snaps to a 16th note
  unless Alt is held), right click a point for the shape of the line that leaves it (linear, hold, smooth) or to delete it. Points
  sit at any position, not on a grid (a fraction of a tick is allowed). Clips are saved in the project; older files open as before.
  Automation plays in **Song mode only**; where clips of one parameter follow each other the later one takes over at its start, and after
  the last clip its final value is held. The value is applied once per 64 samples (about 1.3 ms), not per sample.
- **Mixer: aux buses, sends and VCA groups.** Every track has two send levels (A, B), a **Pre** switch (sends take the sound before the fader,
  balance and VCA; a muted track still sends nothing) and two VCA switches (V1, V2). Two **aux buses** (Aux A, Aux B) sit right of the tracks: four effect
  slots, balance, mute, fader and meter each; their output joins the tracks before the master chain. Two **VCA groups** have a fader and a
  mute that scale / silence every member track (a post-fader send follows the VCA, a pre-fader send does not; two VCAs multiply). All of it is saved in the
  project; older files open with no sends and two empty buses.
- **Master meters** (new *Meters* button): momentary, short-term and integrated **loudness in LUFS** and the **loudness range** following ITU-R BS.1770-4 and
  EBU R128 (K-weighting at any sample rate, absolute and relative gates, 0.1 LU histogram), **true peak** (4x oversampled, dBTP) and sample peak, a
  **spectrum analyser** (4096-point FFT, Hann window, logarithmic axis, peak hold) and a **goniometer** with the stereo correlation meter.
  The loudness code is checked against the EBU reference (a -23 dBFS stereo sine reads -23.0 LUFS at 44.1, 48 and 96 kHz), the gating, the
  K-weighting curve, a 10 LU range test and an inter-sample-peak signal.

### Fixed
- A metronome test read past the end of its buffer (it looked at beat 5 of a render that ended before it), so it failed on some runs. Only the test was wrong.

### Changed
- The channel rack's default height is smaller (the playlist panel needs room for the automation lanes, its minimum height is now 340 px); the mixer
  strips are 72 px wide (they were 80) and the mixer scrolls sideways when the window is too narrow for 8 tracks, 2 buses, 2 VCAs and the master.
- `MainComponent.cpp` is split into seven files by area (no change in behaviour).
- Second code audit (clang-tidy bugprone / performance / clang-analyzer checks, a hunt for repeated blocks and for functions nobody calls):
  the three "ask for a line of text" windows (rename channel, rename pattern, save preset) share one helper (`ui/TextPrompt.h`);
  seven more unused getters were removed; a repeated `switch` branch, a needless copy of the note list and an `int` multiplication that
  was widened too late were fixed. No change in behaviour. What the audit found and left alone is listed in `docs/audit.md`.

## 0.10.0 - 2026-11-06

### Added
- **MIDI file import.** *File > Import a MIDI file into patterns...*, or drop a `.mid` / `.midi` file on the Playlist. Every track (and every channel of a track) with notes becomes a new **synth channel** named after
  the track; the notes are spread over new patterns of two bars (a note that crosses the end of a pattern is cut there and continues, as a
  new note, at the start of the next), placed on playlist track 1 after whatever the playlist already holds. The file's first tempo
  becomes the BPM. Format 0 and 1 files with beat timing (any resolution) are read; SMPTE-timed files, files without notes and files over
  8 MB or 200 000 notes are refused with a message. **Not imported:** General MIDI drum tracks (channel 10: the pitch picks a drum, which
  needs a kit mapping), tempo changes after the first one, controllers, pitch bend and program changes. Tracks that do not fit in the free
  channels (8 in all) are counted and skipped.
- **Legato and slide between notes of one channel.** A note can be *Legato* (the sound that is playing goes on, its pitch jumps to the new
  note: no new attack) or *Slide* (the same, with the pitch gliding to the new note in at most an eighth note). It works on synth and
  sampler channels, needs the note before to still be sounding when the linked note starts (otherwise it plays normally) and is saved in
  the project (a fifth value of the note; older programs ignore it). In the Piano Roll **Shift+click** a note to cycle normal, legato
  (blue "L"), slide (amber "S"); the join is drawn as a line. Turning legato on lengthens a note that stops up to a 16th early.
- **Undo / redo of note edits** (Piano Roll, step sequencer, Clear notes, a take of recorded notes as one step): **Ctrl+Z / Ctrl+Y** (or
  Ctrl+Shift+Z) anywhere except over the Playlist, where the keys still undo the playlist; also in the File menu. The last 200 edits are
  kept. Not covered: channel settings, the mixer, adding or deleting patterns and channels (opening a project or deleting a pattern
  forgets the history).

### Changed
- Code clean-up from an audit: the duplicated fader layout of the mixer strips is one function, the 16th-note constant lives in one
  place, the strip hand-over to the rack is one function, ten unused getters and a stale comment were removed.
- `Note` has a `link` field (`NoteLink`); project files store it only when it is set.

## 0.9.0 - 2026-11-06

### Added
- **Recording MIDI notes into the piano roll.** A **Rec notes** button (right-click: quantize to 1/4, 1/8, 1/16 - the
  default - 1/32 or off). With it on, **Play** and play a MIDI keyboard: the notes go to the selected channel, in the
  current pattern (Pattern or Song mode), at the song position you heard them (the output latency is taken off), with
  their velocity, and can be edited in the piano roll straight away. A note held past the end of the pattern is split
  across the loop; notes still held when you stop are closed. Recording only runs while the song plays.
- **Record into an instrument.** *Record > The recording becomes > A new sampler channel*: the audio take of a
  microphone or guitar becomes the sample of a **new sampler channel** (play it from a MIDI keyboard or draw notes in
  the piano roll to make patterns of it) instead of a playlist clip. If all 8 channels are in use the take goes to the
  playlist and the status line says so. Pitch detection (guitar or voice turned into notes) is **not** implemented.
- **Pattern buttons in the Playlist:** **New**, **Rename** and **Delete** next to the pattern box (the pattern list still
  grows upwards automatically: Pattern 3, 4...). Delete removes the pattern's notes and every clip that plays it; the
  last pattern cannot be deleted. Names are trimmed and at most 40 characters.
- **Audio editing tools in the Playlist:** **Select, Split, Cut, Erase, Zoom** (keys 1-5), a **Grid** box (bar down to a
  single tick; Alt ignores it), **Undo / Redo** (Ctrl+Z / Ctrl+Y), copy / cut / paste / duplicate (Ctrl+C / X / V / D),
  trimming an audio clip from either edge (the sound keeps its place), a right-click menu (*Split here, Duplicate, Copy, Delete*, and
  *Position, length and gain...* for audio) with a **properties** dialog for exact values. Split and Cut also work on pattern clips (on whole
  patterns). Audio clips can now start on any tick, not only on a beat.
- **Precise metrics:** a ruler with minutes:seconds.milliseconds and bar numbers that adapt to the zoom, a readout of the
  time and bar:beat:tick under the mouse and of the selected clip (start, length, end, file length and sample rate, offset into
  the file in seconds and samples, gain), zoom from the
  toolbar, Ctrl+wheel or a dragged range, **Fit**, and a horizontal scroll bar.
- **Cyber-Hub sounds** built into the program (30 system sounds by Hubert, unchanged) in **Browser > Samples >
  Cyber-Hub** and a **Cyber-Hub Kit** under Drum Kits; see [docs/cyber-hub.md](docs/cyber-hub.md) for the listening
  analysis and the licence note.
- **New demo project** made of Cyber-Hub sounds (112 BPM, Cyber-Hub drums, Desktop login / logout as audio clips).
- **Start-up sound:** `intro.wav` plays once when the program opens; switch off in *File > Play the start-up sound*.

### Changed
- Audio clips are no longer forced onto the beat grid and are saved with an `offsetSeconds` (older files open as before).
- The Playlist is now `PlaylistView` (toolbar) around `PlaylistTimeline`; the minimum Playlist size and window width grew.
- A clip made by Split or Cut keeps the audio's position in **seconds** computed at the tempo of the edit: changing the BPM
  afterwards moves the cut points relative to the beat (the sound itself plays at its original speed, as before).

### Not done
- Turning the pitch of a guitar or voice into MIDI notes; real audio and MIDI hardware were not available to test.

## 0.8.0 - 2026-11-06

### Added
- **Audio clips in the Playlist.** Recordings and audio files sit on the playlist tracks next to the pattern clips:
  drawn with their waveform, moved on the beat grid (also to another track), shortened or lengthened from the right
  edge, deleted with a right click, never overlapping another clip. They play in Song mode through the mixer track
  with the same number as their playlist track (so mute, solo, the fader and the effects of that track apply) and are
  part of exports. Saved in the project (`"audioClips"`: the path of each file, its position, length and gain; older
  files open without them).
- **Drag and drop into the Playlist** from the file manager: `.wav`, `.aiff`, `.flac`, `.ogg`, `.oga`, `.mp3`, `.aac`
  (and `.m4a`) and the **sound track of video files** (`.mp4`, `.mkv`, `.mov`, `.avi`, `.webm` and more). Several
  files at once go on consecutive tracks. The files are decoded on a background thread. WAV, AIFF, FLAC, Ogg Vorbis and
  MP3 are read by the program; **AAC and the sound of videos need `ffmpeg`** (`sudo dnf install ffmpeg`), which extracts
  a WAV into `~/Music/Sun Loops Studio/Samples/From videos/`. A file longer than 5 minutes is cut there, with a message.
  *File > Import audio or video into the playlist...* does the same through a file chooser.
- **Recording.** A **Record** button (with an options arrow) records the audio input - microphone, guitar into an audio
  interface - into the playlist track marked with the red dot (click a track's name to choose it). The song plays from
  the start while you record; **Stop** (or the Record button again) ends the take, which appears as a clip at the start
  of the track. The take is a 24-bit WAV in `~/Music/Sun Loops Studio/Samples/Recordings/`, delayed-corrected by the
  device's reported round-trip latency. Options: mono (first input) or stereo (inputs 1+2), and *hear the input while
  recording*. The input is only opened when you press Record (or choose one in **Audio...**). A take stops by itself at
  the end of the playlist.
- **MIDI keyboards.** The **Audio...** dialog lists the MIDI inputs and the input channels; every enabled MIDI keyboard
  plays the selected channel (notes, velocity; note off always reaches the channel that got the note on). Keyboards are
  switched on the first time and when plugged in while the program runs; the choice is remembered. The toolbar's status
  line names the active MIDI inputs. MIDI is for playing live: it does not record notes into the piano roll yet.
- **Input level** bar in the **Audio...** dialog, to check that a microphone or guitar arrives.
- **Metronome** button in the toolbar: click for on/off; right-click for tempo (the BPM of the song), volume and
  **Tap tempo**. A short click on every beat with a higher, louder one on the first beat of each bar; it follows the
  song while it plays and runs by itself at the song's BPM while stopped. It is never part of an export. The setting is
  remembered.
- **Export to more formats** from *File > Export audio*: WAV, MP3, Ogg Vorbis (`.ogg`), Ogg (`.oga`), AAC (`.aac`),
  FLAC and AIFF. WAV, AIFF, FLAC and Ogg are written by the program; MP3 and AAC need `ffmpeg` (the menu says so when it
  is missing). Files are written under a temporary name and moved into place, so a failed export never damages an
  existing file.
- Engine: `Project::audioEvents` and `AudioClip` in the arrangement; `Player` clip voices and `rewind()`;
  `InputRecorder` (wait-free hand-over to a writer thread), `MidiNoteMapper`, `AudioImport`, `AudioExport`, `TapTempo`.
- Tests: 134 new checks (414 in total).

### Changed
- **Projects are now saved as `.slp`** (`.slsproj` before). Old `.slsproj` files still open (and *Save as* offers the
  new ending); the file-type association covers both.
- **Export and save are in the File menu** (the toolbar's Export WAV button is gone).
- The audio device is opened with two input channels *available* (none switched on), so inputs can be chosen.

### Not verified
- Real microphones, audio interfaces and MIDI keyboards (the test machine has none), the real timing of a take
  (the virtual audio device runs faster than real time) and the alignment of a take with its backing track.

## 0.7.0 - 2026-11-05

### Added
- **Effects on the master.** The master strip now has four effect slots like a track (Equalizer, Reverb, Delay,
  Compressor, each with bypass and its parameters in the effects panel, titled *Effects - Master*). The chain processes
  the sum of all tracks, before the master fader and the soft limiter, so it also shapes the exported WAV. It is saved
  in the project (`"master"` in the `.slsproj`; older files open with an empty chain).
- **Assign free mixer track** in a channel's right-click menu. It opens a window listing the 8 mixer tracks (free ones
  marked, the others with the instruments on them, the current one marked) with the first free track selected; **Assign**
  puts the instrument on the chosen track. The Track display, the mixer and the project's changed marker follow.
- Engine: `Project::masterSlots`, `Player::masterEffects`; `MixerRouting.h` (free-track helpers).
- Tests: 17 new checks (280 in total).

### Changed
- Effects memory grows to about 18 MB at 48 kHz (the master chain adds four units).

## 0.6.1 - 2026-11-05

### Added
- **Buffer size setting.** In **Audio...** a *Buffer size* menu (64 to 8192 samples, with the delay in ms) applies at
  once, and the choice is remembered between runs (the whole audio device state is now saved).
- **Auto: set for this computer.** Plays the current song at 256, 512, 1024, ... samples with extra load added,
  watching dropped blocks and how long the slowest block took, and picks the first size that is clean with one measured
  step of margin. Cancel / closing the window restores the previous size. If even the largest size drops blocks it
  says so.
- A live read-out in the dialog: computer load, busiest block (% of its time) and dropped blocks.
- Engine: per-callback load measurement (`consumePeakLoad`), dropout count (`getXRunCount`), `setLoadTest`.
- Tests: `BufferTuner` and the clean-trial rule (12 new checks, 263 in total).

### Changed
- **The default buffer is now 1024 samples** (it used to be the device's own default, 512 on the maintainer's
  machine, which crackled when the demo played).
- The audio dialog hides sample rate and buffer in the selector's "Show advanced settings..." section; the buffer
  controls are in the dialog itself.

### Notes
- Whether 1024 or Auto removes the crackling on the maintainer's real hardware has not been confirmed yet: it was
  only exercised with a silent audio device in a virtual display.

## 0.6.0 - 2026-11-05

### Added
- **Saving and opening projects.** A `File` button in the toolbar (and `Ctrl+N`, `Ctrl+O`, `Ctrl+S`,
  `Ctrl+Shift+S`) with *New*, *Open...*, *Open recent*, *Save* and *Save as...*. The project is one JSON `.slsproj`
  file with channels, patterns and notes, the playlist, the mixer and its effects, BPM, master volume and Song mode.
- **Saving without a name.** *Save* on a project that has no name writes it straight away as
  `Untitled <date> <time>.slsproj` in `~/Music/Sun Loops Studio/Projects` and keeps using that file; *Save as...*
  asks for a name.
- The window title shows the project name and `*` for unsaved changes; *New*, *Open...* and closing the window ask
  *Save / Don't save / Cancel* when there is unsaved work. A `.slsproj` given on the command line is opened.
- Reading is strict about structure and lenient about values (clamping, dropping notes and clips that cannot be
  placed, with a message); files from a newer version, damaged files and files over 32 MB are refused without
  touching the current project. Writing is atomic (temporary file, then replace).
- Samples are stored as references (`builtin:<id>` or a path). A sample that cannot be found opens as silent and
  keeps its reference, so saving again does not lose it.
- **AppImage.** `scripts/build_appimage.sh` builds `Sun-Loops-Studio-<version>-x86_64.AppImage` with pinned,
  checksum-verified linuxdeploy and appimagetool. The `.desktop` file now takes a file (`%f`) and `cmake --install`
  installs a MIME type for `.slsproj`.
- Tests: `ProjectFile` round trip, bad input, missing samples, atomic write, automatic file name (251 checks in total).

### Notes
- Samples are not copied into the project file: moving a project to another computer needs its samples as well.
- No autosave, crash recovery or undo yet.
- The AppImage was built and started on Ubuntu 24.04 in a virtual display; it has not yet been run on Nobara.

## 0.5.1 - 2026-11-05

### Added
- **Playable Piano Roll.** Press the left button on a key of the on-screen keyboard and that note sounds for as long as
  you hold the button; slide up or down to play the keys in turn. The note you are drawing, moving or resizing on the
  grid sounds too while the button is down. Notes go through the selected channel (its instrument or sample, volume,
  pan, Pitch, mixer track and effects) and work with the transport stopped or running. The key being played lights up.
- Engine: live notes (`LiveNotes.h`: a wait-free single-producer/single-consumer queue from the interface to the audio
  thread; `Player::liveNoteOn` / `liveNoteOff`). A held note ignores the loop boundary and the transport; as a safety
  net a held synth note ends by itself after 20 seconds.
- Tests: 22 new checks (queue order, overflow and a two-thread test; held notes with the transport stopped, across the
  loop wrap and stop/start; two keys; Pitch, mute and track mute; sampler; bad input; the 20 s limit; no allocation).

### Notes
- Sampler channels play their sample once per key press (release does not cut it short).
- The computer keyboard does not play notes yet.

## 0.5.0 - 2026-11-05

### Added
- **Browser** (left sidebar, `F8`, folds into a thin tab): a tree with *Samples*, *Drum Kits*, *Instruments*,
  *Presets* and *Plugin*, a quick-search box that filters every folder, *Folder* / *Refresh* buttons. Sounds are
  dragged onto a channel of the Channel Rack (or onto the empty area to create a channel), or double-clicked to
  load them into the selected channel. Built-in sounds (kick, snare, hat) and factory presets are always there;
  your own files are read from `~/Music/Sun Loops Studio/{Samples,Drum Kits,Presets}` (created at start).
  *Plugin* lists VST3 plugins found by an on-demand scan; they cannot be used yet (see below).
- **Channel presets**: *Save as preset...* in a channel's menu writes a `.slspreset` JSON file (instrument, sample
  reference, volume, pan, pitch, swing); presets are applied by drag or double-click. Six factory presets.
- **Channel Rack**: a **Pitch** knob (-12..+12 semitones, applied to every note of the channel), a **Swing** knob
  (delays the off-beat 16th notes by up to half a step) and a **Track N** display that routes the channel to a
  mixer track (click for a menu, drag or scroll to step).
- **Expanded Mixer** (`F9`): 8 tracks side by side, each with a vertical fader (dB read-out), stereo balance, mute,
  solo, a **Peak/RMS** meter with peak hold and clip lamp, and **4 insert-effect slots**; a master strip; a panel
  for the selected slot's parameters.
- **Built-in effects**: Equalizer (low shelf, mid peak with frequency, high shelf), Reverb, Delay and Compressor,
  each with a bypass switch. They run in the audio thread with all memory preallocated.
- Tests: pitch, swing, mixer tracks, the four effects (also through the player, with an allocation check), the
  browser tree, channel presets and library scan (194 checks in total).

### Changed
- The mixer is now **per track**, not per channel: channels choose a track, and several channels may share one.
  Channel volume/pan/mute/solo remain per channel; track fader, balance, mute and solo are separate.
- Panel layout keys were renamed to `panel2.*` because the default layout changed (rack across the top with the
  browser on the left, mixer opens large over everything). **Layouts saved by older versions are ignored once.**
- `Player::render` works in blocks of 64 samples; the audio callback disables denormals.

### Known limits
- **VST3 hosting is not implemented.** The effect slots hold the built-in effects; the slot menu shows a disabled
  "VST3 plugin..." entry.
- Dragging files from the desktop file manager onto the program is not implemented; drag from the in-app browser.
- Effects memory is about 16 MB with eight tracks at 48 kHz and grows with the sample rate.
- Verified headless and in a virtual display with a silent audio device, not with real audio hardware.

## 0.4.0 - 2026-11-05

### Added
- **Floating panels**: Channel Rack, Playlist, Piano Roll and Mixer are movable, resizable panels inside the main
  window. They can overlap, come to the front on click, be maximised with a double-click on the title and be closed.
  `F5` / `F6` / `F7` / `F9` and toolbar buttons open, raise or close them; **Reset** restores the default layout;
  positions, sizes and open/closed state are saved in `~/.config/SunLoopsStudio/SunLoopsStudio.settings`.
- **Mixer panel** with vertical strips (fader with dB read-out, pan, mute, solo, meter), synchronised with the rack.
- **Patterns and Playlist**: several patterns (new or cloned) and a playlist of clips on 8 tracks (place, move,
  repeat by resizing, delete, no overlaps on a track). **Pattern / Song** switch; Export WAV renders the selected mode.
  The model (`Arrangement.h`) compiles pattern or song into a plain `Project`, so the audio core is unchanged.
- **Channel Rack**: right-click menu (Piano roll, Instrument, Load sample, Rename), double-click on the note preview
  or the name opens the piano roll, **+ Add channel** (up to 8), scrollable rack.
- The **Piano Roll** starts closed, is titled with its channel and scrolls to the channel's notes; its zoom follows
  the panel width until the user zooms.
- `cmake --install`: the `sun-loops-studio` command, a `.desktop` entry and icons (JUCE's own install rules are
  excluded with CMake 3.28+).
- Tests for panel layout rules and for the arrangement (placement, compilation, audible result through the offline
  renderer).
- Documentation rewritten: README (English and Italian), user guide (English and Italian), building, architecture,
  roadmap, this changelog.

### Changed
- Toolbar on two rows; main window 1280x860 by default, minimum 1080x600.

### Removed
- A stray, unused `juce-src` submodule entry left over from the first scaffold.

## 0.3.0

- The first scaffold, written by another assistant and not working (invalid JUCE calls, a broken setup script, empty
  icons), was replaced by a verified, tested core and renamed to **Sun Loops Studio**.
- Real-time safe `Player` (synth and sampler voices, per-channel volume/pan/mute/solo, peaks), lock-free
  `SnapshotExchange` between the UI and audio threads, offline WAV export, built-in drum samples, sample loading,
  VST3 scanner library, audio device selection, step grid and piano roll editors.
- Headless test program (`SunLoopsStudioTests`), `scripts/setup_nobara.sh` for Fedora/Nobara, MIT `LICENSE` kept.

## 0.1 - 0.2

- Initial scaffold (superseded by 0.3.0).
