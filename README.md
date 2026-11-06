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

**Status: alpha (0.9.0).** It builds, the automated tests pass and the interface has been exercised in a virtual
display. Playback was confirmed on a real Nobara machine for version 0.3; everything added since (panels, patterns,
playlist, browser, pitch/swing, the expanded mixer and its effects, saving projects, the AppImage, audio clips, drag and
drop, recording, MIDI, the metronome and the export formats) was tested headless and in a virtual display with a silent
audio device, not yet with real audio, microphones or MIDI hardware. **Hosting VST3 plugins is not implemented**: the mixer's effect
slots hold Sun Loops Studio's own Equalizer, Reverb, Delay and Compressor. See [docs/roadmap.md](docs/roadmap.md) for
exactly what is and is not verified and what is missing (VST3 hosting and undo are the big ones).

## Features

- **Floating panels** - Channel Rack, Playlist, Piano Roll and Mixer. Drag by the title bar, resize from the
  edges, close with the x, double-click the title to maximise. Panels overlap; clicking one raises it.
  `F5` / `F6` / `F7` / `F9` open, raise or close the Playlist / Rack / Piano Roll / Mixer. The layout is remembered.
- **Browser** (left sidebar, `F8` folds it away) - a tree with *Samples*, *Drum Kits*, *Instruments*, *Presets* and
  *Plugin*, a quick-search box, and drag-and-drop (or double-click) of sounds onto a channel of the Channel Rack.
  Your own files live in `~/Music/Sun Loops Studio/`.
- **Channel Rack** - up to 8 channels, each a synth or a sampler. 16th-note step sequencer on sampler channels,
  note preview on synth channels. Per channel: volume, pan, a **Pitch** knob (semitones) and a **Swing** knob, and a
  **Track N** display that sends the channel to a mixer track. Right-click a channel for *Piano roll*, *Instrument*,
  *Load sample...*, *Rename...*, *Assign free mixer track*, *Save as preset...*.
- **Piano Roll** - for the selected channel: click to add a note, drag to move, drag the right edge to resize,
  right-click to delete, scroll and zoom. Opens from a channel (menu or double-click), as in FL Studio.
- **Patterns and Playlist** - several patterns (new, cloned, renamed or deleted from buttons above the playlist), arranged
  as clips on 8 tracks. A *Pattern / Song* switch chooses between looping the selected pattern and playing the whole
  playlist.
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
- **Metronome** - a click with adjustable tempo (and tap tempo) and volume, on its own button.
- **Mixer** (`F9`) - 8 vertical tracks, each with a fader and dB read-out, stereo balance, mute, solo, a Peak/RMS
  meter and 4 insert-effect slots (Equalizer, Reverb, Delay, Compressor, each with bypass). The master strip on the
  right has the same 4 effect slots, applied to the whole mix. Several channels can share a track.
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

This repository carries an MIT [LICENSE](LICENSE). Note that JUCE is available only under the GPLv3/AGPLv3 or a
paid commercial license, and a program linking JUCE under its free license must itself be distributed under the GPL.
Before distributing binaries, either relicense this project under GPLv3 or obtain a JUCE commercial license.
