# Sun Loops Studio

*[Italiano](README.it.md)*

An open source, loop-based music production program for Linux, written in C++20 with JUCE 8. You build
beats and melodies in a **Channel Rack**, write notes in a **Piano Roll**, arrange patterns on a
**Playlist**, balance them in a **Mixer** and export the result to WAV. Every tool lives in a floating
panel that you can move, resize, overlap, open and close.

![Sun Loops Studio: channel rack, playlist and piano roll](docs/images/overview.png)

Sun Loops Studio is inspired by the workflow of loop-based DAWs such as FL Studio. It is an independent project
that uses no code, names, graphics or assets from other products, and it is not affiliated with or endorsed by
their makers.

**Status: alpha (0.4.0).** It builds, the automated tests pass and the interface has been exercised in a virtual
display. Playback was confirmed on a real Nobara machine for version 0.3; the panels, patterns and playlist added
since were tested headless and in a virtual display, not yet with real audio. See
[docs/roadmap.md](docs/roadmap.md) for exactly what is and is not verified and what is missing (saving projects and
hosting VST plugins are the big ones).

## Features

- **Floating panels** - Channel Rack, Playlist, Piano Roll and Mixer. Drag by the title bar, resize from the
  edges, close with the x, double-click the title to maximise. Panels overlap; clicking one raises it.
  `F5` / `F6` / `F7` / `F9` open, raise or close the Playlist / Rack / Piano Roll / Mixer. The layout is remembered.
- **Channel Rack** - up to 8 channels, each a synth or a sampler. 16th-note step sequencer on sampler channels,
  note preview on synth channels. Right-click a channel for *Piano roll*, *Instrument*, *Load sample...*, *Rename...*.
- **Piano Roll** - for the selected channel: click to add a note, drag to move, drag the right edge to resize,
  right-click to delete, scroll and zoom. Opens from a channel (menu or double-click), as in FL Studio.
- **Patterns and Playlist** - several patterns (new or cloned), arranged as clips on 8 tracks. A *Pattern / Song*
  switch chooses between looping the selected pattern and playing the whole playlist.
- **Mixer** - one vertical strip per channel: fader with dB read-out, pan, mute, solo, peak meter.
- **Sampler** - loads WAV, FLAC, OGG and AIFF files (up to 30 s), plays them one-shot, repitched by note.
  Kick, snare and hat are synthesised, so you need no sample files to start.
- **Export** - the pattern or the song to a 24-bit / 48 kHz WAV file, rendered by the same engine that plays it.
- **Audio** - device selection and a read-out of sample rate, buffer size and reported latency.

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
`cmake --install build --prefix ~/.local`. More detail, other distributions and troubleshooting:
[docs/building.md](docs/building.md).

Press **Space** to play, **F5 / F6 / F7 / F9** to show panels. A guided tour is in
[docs/user-guide.md](docs/user-guide.md).

## Documentation

| Document | Contents |
| --- | --- |
| [docs/user-guide.md](docs/user-guide.md) | How to use every panel, shortcuts, limits ([Italiano](docs/user-guide.it.md)) |
| [docs/building.md](docs/building.md) | Dependencies, build, install, tests, troubleshooting |
| [docs/architecture.md](docs/architecture.md) | Threads, real-time rules, data flow, patterns and playlist, panels, tests |
| [docs/roadmap.md](docs/roadmap.md) | What is verified, what is not, what comes next |
| [CHANGELOG.md](CHANGELOG.md) | Changes per version |

## Tests

```bash
./build/SunLoopsStudioTests_artefacts/Release/SunLoopsStudioTests
```

Headless (no audio device or display needed). They check, among other things, synth pitch and sample-exact timing,
looping, polyphony, mixer, sampler, that `Player::render` never allocates, a concurrent stress test of the
UI-to-audio hand-over, WAV round trip, the plugin scanner, panel layout rules and the playlist.

## License

This repository carries an MIT [LICENSE](LICENSE). Note that JUCE is available only under the GPLv3/AGPLv3 or a
paid commercial license, and a program linking JUCE under its free license must itself be distributed under the GPL.
Before distributing binaries, either relicense this project under GPLv3 or obtain a JUCE commercial license.
