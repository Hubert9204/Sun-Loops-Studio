# Changelog

All notable changes to Sun Loops Studio. Newest first.

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
