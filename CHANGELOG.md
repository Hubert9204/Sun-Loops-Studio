# Changelog

All notable changes to Sun Loops Studio. Newest first.

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
