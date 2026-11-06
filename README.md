# Sun Loops Studio

*[Italiano](README.it.md)*

An open source, loop-based music production program for Linux, written in C++20 with JUCE 8. You build
beats and melodies in a **Channel Rack**, write notes in a **Piano Roll**, arrange patterns on a
**Playlist** (together with audio files and recordings), balance them in a **Mixer** with effects and export the result
to WAV, MP3, Ogg, AAC, FLAC or AIFF. A **Browser** on the left
holds your sounds; every other tool lives in a floating panel that you can move, resize, overlap, open and close.

![Sun Loops Studio: browser, channel rack and playlist](docs/images/overview.png)

![The expanded mixer with an effects chain](docs/images/mixer.png)

Sun Loops Studio is inspired by the workflow of loop-based DAWs such as FL Studio. It is an independent project
that uses no code, names, graphics or assets from other products, and it is not affiliated with or endorsed by
their makers.

**Status: alpha (0.17.0).** It builds, the automated tests pass and the interface has been exercised in a virtual
display. Playback was confirmed on a real Nobara machine for version 0.3; everything added since (panels, patterns,
playlist, browser, pitch/swing, the expanded mixer and its effects, saving projects, the AppImage, audio clips, drag and
drop, recording, MIDI, the metronome and the export formats) was tested headless and in a virtual display with a silent
audio device, not yet with real audio, microphones or MIDI hardware. **Plugins (VST3, LV2, LADSPA) can be used since 0.17.0** as
instruments and as mixer effects; they were tried with small test plugins and the LADSPA examples, not yet with commercial plugins.
See [docs/roadmap.md](docs/roadmap.md) for exactly what is and is not verified and what is missing.

## Features

- **Floating panels** - Channel Rack, Playlist, Piano Roll and Mixer. Drag by the title bar, resize from the
  edges, close with the x, double-click the title to maximise. Panels overlap; clicking one raises it.
  `F5` / `F6` / `F7` / `F9` open, raise or close the Playlist / Rack / Piano Roll / Mixer. The layout is remembered.
- **Browser** (left sidebar, `F8` folds it away) - a tree with *Samples*, *Drum Kits*, *Instruments*, *Presets* and
  *Plugin*, a quick-search box, and drag-and-drop (or double-click) of sounds onto a channel of the Channel Rack.
  Your own files live in `~/Music/Sun Loops Studio/`.
- **Channel Rack** - up to 32 channels, each a synth, a sampler or a drum machine. 16th-note step sequencer on sampler channels,
  note preview on synth channels. Per channel: volume, pan, a **Pitch** knob (semitones) and a **Swing** knob, and a
  **Track N** display that sends the channel to a mixer track. Right-click a channel for *Piano roll*, *Instrument*,
  *Load sample...*, *Rename...*, *Clone*, *Move up / down*, *Delete*, *Assign free mixer track*, *Save as preset...*.
- **Piano Roll** - for the selected channel: click to add a note, drag to move, drag the right edge to resize,
  right-click to delete, scroll and zoom. Opens from a channel (menu or double-click), as in FL Studio.
- **Patterns and Playlist** - several patterns (new, cloned, renamed or deleted from buttons above the playlist), arranged
  as clips on 8 tracks; patterns are 1, 2, 4 or 8 bars long. A *Pattern / Song* switch chooses between looping the selected pattern and playing the whole
  playlist. **Click the ruler to move the playhead, even while playing; right-drag on it to draw a loop region** (right-click it to move, resize or delete it).
- **Audio in the Playlist** - drag audio files (`.wav .aiff .flac .ogg .oga .mp3 .aac`) or video files (their sound
  track) from the file manager onto the Playlist: they become clips with a waveform. **Select, Split, Cut, Erase and Zoom
  tools**, trimming from both edges, a grid down to one tick, undo/redo and a ruler in minutes:seconds.milliseconds
  with a readout of the time under the mouse let you work on a take precisely. AAC and video sound need `ffmpeg` installed.
- **Recording** - the **Record** button records a microphone or an instrument into the playlist track you choose, in
  time with the song. **MIDI keyboards** play the selected channel, and **Rec notes** writes what they play into the piano roll
  (quantized) so you can make patterns; a take can also become a new sampler channel. Set up in **Audio...**.
- **Cyber-Hub sounds** - 30 system sounds by Hubert are built in (Browser > Samples > Cyber-Hub, and a Cyber-Hub Kit); the demo
  project is made of them and a start-up sound plays when the program opens (switch it off in the File menu). See
  [docs/cyber-hub.md](docs/cyber-hub.md).
- **MIDI import, legato and slide, note undo** - a Standard MIDI File (File menu, or dropped on the Playlist) becomes new
  channels and patterns; notes can be joined with **legato** or a pitch **slide** (Shift+click in the Piano Roll); **Ctrl+Z / Ctrl+Y**
  undo and redo any edit (notes, channels, kits, mixer, patterns; the Playlist also has its own clip undo).
- **Safety** - autosave every minute, recovery after a crash, a `.bak` of the previous version, and *Export project with samples* to move a
  project to another computer.
- **Metronome** - a click with adjustable tempo (and tap tempo) and volume, on its own button.
- **Mixer** (`F9`) - 8 vertical tracks, each with a fader and dB read-out, stereo balance, mute, solo, a Peak/RMS
  meter and 4 insert-effect slots (Equalizer, Reverb, Delay, Compressor, each with bypass), two **sends** (pre or post fader)
  and two **VCA** switches. Two **aux buses** with their own effects and two **VCA groups** sit beside the tracks. The master strip on the
  right has the same 4 effect slots, applied to the whole mix. Several channels can share a track.
- **Plugins** - VST3, LV2 and LADSPA instruments and effects: scan them from the browser, double-click or drag an instrument for a channel,
  pick an effect in a mixer slot, open the plugin's own window; their settings are saved in the project, and a missing plugin keeps them.
- **Automation** - right-click a volume, pan, fader or effect parameter > *Create automation clip*; clips live on four lanes under the
  playlist tracks and have a point editor (linear / hold / smooth lines). Song mode only.
- **Stretch and pitch** - right-click an audio clip: make it longer or shorter without changing its pitch, shift its pitch without changing its length,
  or match a loop to the project tempo (Signalsmith Stretch).
- **Sampler and SoundFonts** - envelope (ADSR), loop, low / high / band-pass filter and root key per sampler channel; **SoundFont 2 (.sf2)** presets
  played as multi-sample zones; the sample editor's slices become a sampler channel with one note per slice.
- **Drum machine** - a channel with 16 pads (notes 36 to 51): up to four sound layers per pad chosen by velocity, level, pan, tuning, swing and
  choke groups per pad, a step sequencer with a row per pad, drag-and-drop from the browser, and kits saved as folders (`kit.json` + sounds).
- **Sample editor** - right-click an audio clip or a sampler channel > *Edit sample...*: waveform with zoom and selection, preview with a live spectrum,
  cut / crop / silence / reverse / invert / gain / normalise / fades / DC removal with undo, noise reduction (spectral gating), and slicing a clip at detected hits
  or equal parts. The result is saved as a new WAV file; the original is never touched.
- **Meters** - loudness in LUFS (momentary, short-term, integrated, range; EBU R128), true peak, spectrum analyser, goniometer and correlation.
- **Sampler** - loads WAV, FLAC, OGG and AIFF files (up to 30 s), plays them one-shot, repitched by note.
  Kick, snare and hat are synthesised, so you need no sample files to start.
- **Projects** - `File` menu and `Ctrl+N` / `Ctrl+O` / `Ctrl+S` / `Ctrl+Shift+S`: *Save* writes a named project;
  a project that has no name yet is saved at once under an automatic name (`Untitled <date time>.slp`) in
  `~/Music/Sun Loops Studio/Projects`, so nothing is lost; *Save as...* asks for a name. Open recent, a title bar
  that shows the name and `*` for unsaved changes, and a prompt before closing with unsaved work. Samples are
  referenced by path, not copied into the file.
- **Export** - *File > Export audio*: the pattern or the song as WAV, AIFF, FLAC (lossless) or MP3, Ogg Vorbis
  (`.ogg` / `.oga`), AAC (lossy), rendered by the same engine that plays it. MP3 and AAC need `ffmpeg`.
- **Audio** - device selection, an adjustable **buffer size** (default 1024 samples) and an **Auto** button that tests
  your computer and picks a size that does not crackle, plus a read-out of load and dropped blocks. The choice is
  remembered.

## Quick start (Nobara / Fedora)

```bash
git clone https://github.com/Hubert9204/Sun-Loops-Studio.git
cd Sun-Loops-Studio
./scripts/setup_nobara.sh                      # installs build dependencies (asks for sudo)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/SunLoopsStudio_artefacts/Release/"Sun Loops Studio"
```

The first configure downloads JUCE 8.0.4 (needs network once). To install into your menu:
`cmake --install build --prefix ~/.local`. To make a single-file **AppImage** (`dist/Sun-Loops-Studio-<version>-x86_64.AppImage`):
`./scripts/build_appimage.sh`. More detail, other distributions and troubleshooting:
[docs/building.md](docs/building.md).

Press **Space** to play, **F5 / F6 / F7 / F9** to show panels and **F8** for the browser. A guided tour is in
[docs/user-guide.md](docs/user-guide.md).

## Documentation

| Document | Contents |
| --- | --- |
| [docs/user-guide.md](docs/user-guide.md) | How to use every panel, shortcuts, limits ([Italiano](docs/user-guide.it.md)) |
| [docs/building.md](docs/building.md) | Dependencies, build, install, tests, troubleshooting |
| [docs/cyber-hub.md](docs/cyber-hub.md) | The Cyber-Hub sounds, the listening analysis, the demo, the start-up sound and a licence note |
| [docs/architecture.md](docs/architecture.md) | Threads, real-time rules, data flow, patterns and playlist, panels, tests |
| [docs/roadmap.md](docs/roadmap.md) | What is verified, what is not, what comes next |
| [CHANGELOG.md](CHANGELOG.md) | Changes per version |

## Tests

```bash
./build/SunLoopsStudioTests_artefacts/Release/SunLoopsStudioTests
```

Headless (no audio device or display needed). They check, among other things, synth pitch and sample-exact timing,
looping, polyphony, mixer tracks, pitch and swing, the four effects, sampler, that `Player::render` never allocates
(effects included), a concurrent stress test of the UI-to-audio hand-over, WAV round trip, the plugin scanner, the
browser tree and file scan, channel presets, panel layout rules, the playlist, live notes and the project file
(save/load round trip, damaged and hostile files, missing samples, atomic writing), export to every format,
importing audio and video, audio clips, the recorder, MIDI note handling and the metronome.

## License

Sun Loops Studio is free software under the **GNU General Public License, version 3** ([LICENSE](LICENSE)); versions up to
0.16.0 were published under MIT. GPLv3 is the license under which JUCE may be used for free, so the program and its AppImage can
be shared as long as the source comes with them.

`third_party/signalsmith` holds Signalsmith Stretch and Signalsmith Linear, both MIT-licensed (their licences and the exact revisions are in that folder).
`third_party/tinysoundfont` holds TinySoundFont's `tsf.h` (MIT), used only to read SoundFont files.
