# Sun Loops Studio

An open source music production program for Linux, written in C++20 with JUCE 8. It is inspired by
the way loop-based DAWs work (a channel rack with step sequencer, a piano roll, a mixer) but it is
its own project and uses no names, graphics or assets from other products.

## What works today

- **Floating panels** (Channel Rack, Playlist, Piano Roll, Mixer): drag a panel by its title bar, resize it from any
  edge, close it with the x, double-click the title to maximise/restore. Panels can overlap; clicking one
  brings it to the front. Open/close from the toolbar or with **F6** (rack), **F5** (playlist), **F7** (piano roll), **F9** (mixer):
  the key opens a closed panel, raises an open one that is behind, and closes it when it is already on top.
  **Reset** restores the default layout. Positions, sizes and open/closed state are remembered in
  `~/.config/SunLoopsStudio/SunLoopsStudio.settings`.
- **Channel rack**: five demo channels (two synths, three sample players) and **+ Add channel** (up to 8).
  Right-click a channel for **Piano roll**, **Instrument** (Synth / Sampler), **Load sample...** and **Rename...**;
  double-click a synth channel's note preview or a channel's name to open its piano roll. The piano roll is
  closed at start, as in FL Studio, and opens from the channel you want to write notes for.
- **Patterns and Playlist**: a pattern is one set of notes for all channels (the pattern box in the toolbar,
  **+** for a new or cloned pattern). The Playlist panel arranges patterns as clips on 8 tracks: click an empty
  cell to place the selected pattern, drag a clip to move it, drag its right edge to repeat it, right-click to
  delete, double-click to select its pattern. The **Pattern / Song** button chooses between looping the
  selected pattern and playing the playlist; Export WAV renders whichever is selected.
- **Step sequencer** on sampler channels: 16th-note step buttons, 2-bar loop.
- **Piano roll** for the selected channel: click to add a note (drag to set its length), drag to move,
  drag the right edge to resize, right-click to delete. Wheel scrolls pitch, Shift+wheel scrolls time,
  Ctrl+wheel zooms time.
- **Mixer** panel with one vertical strip per channel (fader with dB read-out, pan, mute, solo, peak meter),
  kept in sync with the rack rows. Master volume and meter are in the toolbar.
- **Sampler**: loads WAV / FLAC / OGG / AIFF files (up to 30 s), plays them one-shot, repitched by note.
  Drum sounds are synthesised, so no sample files are needed to get started.
- **Transport**: Play/Stop (Space), BPM 40-240.
- **Export** of the pattern to WAV (24 bit, 48 kHz), rendered by the same engine used for playback.
- **VST3 scanning** (library only, not yet connected to the UI or the audio path).
- Audio device selection and a readout of sample rate, buffer and reported output latency.

## Install (optional)

```bash
cmake --install build --prefix ~/.local
```

This installs the `sun-loops-studio` command, an application-menu entry (`.desktop` file) and the icon under
`~/.local`; log out and in again, or run `kbuildsycoca6`, if KDE does not show it right away. It needs CMake 3.28
or newer so that JUCE's own install rules are left out.

## Build on Nobara / Fedora

```bash
./scripts/setup_nobara.sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/SunLoopsStudio_artefacts/Release/"Sun Loops Studio"
```

CMake downloads JUCE 8.0.4 the first time it is configured, so it needs network access once.

## Tests

```bash
./build/SunLoopsStudioTests_artefacts/Release/SunLoopsStudioTests
```

The tests are headless (no audio device, no display). They cover the project model, synth pitch and
timing, looping and stop, polyphony and voice stealing, volume / pan / mute / solo, the sampler
(pitch, sample-rate conversion, one-shot behaviour), safe replacement of a sample while it plays,
that `Player::render` never allocates, a concurrent stress test of the snapshot exchange, a WAV
round trip and the VST3 scanner on empty and invalid directories.

## Architecture

```
src/audio/Project.h            Channel, SampleData and the immutable Project snapshot
src/audio/NoteSequence.h       Note, tick resolution (480 ticks per beat), note validation
src/audio/SnapshotExchange.h   Hands snapshots from the UI thread to the audio thread, lock-free
src/audio/Player.*             Transport, synth and sampler voices, per-channel mixer
src/audio/OfflineRenderer.*    Renders a Project to a buffer or a WAV file with the same Player
src/audio/BuiltinSamples.*     Synthesised kick, snare and hat
src/audio/AudioEngine.*        Audio device, atomics for BPM / volume / play state, audio callback
src/plugins/PluginScanner.*    VST3 discovery
src/ui/MainComponent.*         Window: toolbar, panel workspace, layout persistence, export, sample loading
src/ui/PanelWindow.*           Floating panel (drag, resize, close, maximise) and the Workspace holding them
src/ui/PanelLayout.h           Panel position/size/visibility: parse, serialise, clamp (unit-tested)
src/ui/RackView.h              Scrollable list of channel strips (Channel Rack panel body)
src/ui/PlaylistView.*          Playlist panel body: clips on tracks
src/audio/Arrangement.h        Patterns, clips, and compilation of pattern / song into a plain Project
src/ui/MixerPanel.*            Vertical mixer strips (Mixer panel body)
src/ui/ChannelStrip.*          One rack row: name, mute/solo, volume, pan, meter, steps
src/ui/PianoRoll.*             Note editor
tests/test_core.cpp            Headless tests
```

The audio callback does no allocation, no locking and no I/O. The UI builds a new `Project` on every
edit and publishes it; the audio thread only reads it. See `docs/architecture.md` for details.

## Audio backends

On Linux JUCE uses ALSA and JACK. PipeWire is reached through its compatibility layers
(`pipewire-alsa`, `pipewire-jack`), which Nobara installs by default. JUCE has no native PipeWire
backend. Latency is whatever the device reports; measure it instead of assuming it.

## Status and roadmap

Verified in a headless environment: clean build, all tests, the application starting under a virtual
display and mouse editing. **Not verified:** actual sound output, the playhead moving during playback
and real latency, because that environment has no audio device. Please try those on a real machine.

Not done yet: adding and removing channels, more than one pattern, a playlist / arranger, effects,
hosting VST3 plugins inside the audio path, automation, saving and loading projects, MP3 export.

## License

The repository carries an MIT `LICENSE`. JUCE, however, is available only under GPLv3/AGPLv3 or a
paid commercial license. A program that links JUCE under its free license must itself be distributed
under the GPL. Before distributing binaries, either switch this project to GPLv3 or obtain a JUCE
commercial license.
