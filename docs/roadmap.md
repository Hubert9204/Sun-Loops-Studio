# Status and roadmap

Version 0.5.0 (alpha). This page separates what has been **verified** from what has only been **written**, and lists
what is missing. If something here disagrees with the program, the program is right: please fix the page.

## What has been verified, and how

| Area | How it was checked | Result |
| --- | --- | --- |
| Audio core (project model, synth, sampler, mixer, looping, stop, polyphony, no allocation in the audio callback, UI-to-audio hand-over, WAV export, plugin scanner) | Automated headless tests, plus deliberate-breakage ("mutation") checks, plus a thread-sanitizer / address-sanitizer run of the exchange test | All pass |
| Panel layout rules, pattern and playlist logic (placement rules, compilation of pattern and song, audible result through the offline renderer) | Automated headless tests and mutation checks | All pass |
| Build and start on the maintainer's machine (Nobara 44, GCC 16, PipeWire), real sound output, playhead movement | Run by the maintainer on version 0.3 | Worked |
| Panels (drag, resize with minimum size, maximise, close, overlap, bring to front, F-keys, Reset, saved layout across restarts, re-clamping when the window shrinks) | Manual, in a virtual display (`Xvfb`) with simulated mouse and keyboard and screenshots | Worked |
| Channel menu, double-click to open the piano roll, add channel, new/cloned pattern, pattern isolation (clearing a channel in one pattern leaves the other intact), playlist place / move / resize / delete / refused overlap, Pattern/Song button | Same virtual display | Worked |
| `cmake --install` into a staging directory | `DESTDIR` install | Installs only the command, `.desktop` file and icons |
| Pitch, swing, mixer tracks (routing, fader, balance, mute/solo, meters), the four built-in effects (response of each, bypass, state reset, silence in = silence out, no allocation in `Player::render` with effects running), browser tree, search filter, directory scan, channel presets | Automated headless tests (194 checks) and mutation checks | All pass |
| Browser (tree, search, fold with F8), drag from the browser onto a channel and onto the empty rack area, double-click to load, Pitch/Swing knobs, Track display, expanded mixer with live Peak/RMS, effect chain editing, bypass | Manual, in a virtual display with simulated mouse and a silent (ALSA `null`) audio device; a compressor at -60 dB / 20:1 lowered the track meter and bypass restored it | Worked |
| A use-after-free when clicking an item that rebuilds the browser tree (crash on *Scan for VST3 plugins...*) | Found in that manual session | Fixed (item actions are deferred); the same click was repeated afterwards |

## What has not been verified

- **Sound of the new features on real hardware.** Panels, patterns, the playlist, Song mode, pitch, swing, track
  routing and the effects were not heard through an audio device (the test environment had none); they are covered
  by offline-renderer tests and by meters in a virtual display. How the effects *sound* (EQ curve taste, reverb
  quality, compressor behaviour) has not been judged by ears. The playhead moving across the playlist during
  playback has not been seen.
- **Dragging files from the desktop file manager** onto the program: not implemented (drags from the in-app browser
  were verified only with simulated mouse events).
- **Typing in the "Save as preset" name box** was not tested (the virtual display had no window manager, so dialogs
  do not receive keyboard focus); saving and loading presets is covered by the tests.
- **Memory of the effects**: about 16 MB at 48 kHz (8 tracks x 4 slots), more at higher sample rates; not measured
  on a real device.
- **Latency.** The program shows what the device reports; it has never been measured.
- **The installed menu entry on KDE** (the file is installed and well-formed, but it was not seen in a real menu).
- **Compiler warnings on GCC 16** in `tests/test_core.cpp` (silenced with pragmas written against a message from
  GCC 16, tested here only with GCC 13).
- **Other distributions** than Nobara/Fedora.
- **Wayland-specific behaviour** (JUCE runs through XWayland).

## Not implemented

| Feature | Notes |
| --- | --- |
| Save and open projects | There is no project file. Closing the program loses the song. Only the panel layout is saved. |
| VST3 plugins | Plugins can be *scanned* (library code, no UI). Playing them as instruments or effects is not implemented: it needs plugin processing inside the audio thread, state saving, parameter handling and plugin editor windows. |
| Undo / redo | Not implemented. |
| VST3 effects in the mixer | The four slots per track hold built-in effects only. No third-party plugin can be loaded. |
| Sends, groups, master effects, more than 8 mixer tracks | A channel goes to one of 8 tracks; the master strip has a fader and a meter only. No sends or sidechain, no effect on the master, no per-slot wet/dry beyond what each effect offers, no effect-chain presets. |
| Automation, MIDI input and recording | Not implemented. |
| Playlist seeking | Clicking the ruler does not move the playhead. |
| Pattern deletion and renaming | Patterns are created and cloned only; clips cannot be re-pointed to another pattern. |
| Channel deletion and reordering | Channels can be added (up to 8) but not removed or moved. |
| Pattern length, time signature, tempo changes | A pattern is always two bars of 4/4 with a 16th grid; one BPM for the whole song. Swing exists per channel (off-beat 16ths only). |
| Sample editing | Samples play once from the start: no loop points, envelope, trim or time-stretch. |
| Export formats | WAV (16/24 bit in the library, 24 bit in the UI). No MP3/FLAC export, no stems. |
| Packaging | No Flatpak or RPM; install is `cmake --install`. |

## Proposed order of work

1. **Save / open projects** (so work survives closing the program): a versioned file holding channels, patterns and
   clips, with samples referenced by path (or embedded), and a test that a round trip gives an identical project.
2. **Undo / redo** over the edits the arrangement and channels already funnel through one place.
3. **Playlist and pattern polish**: seeking from the ruler, pattern rename and delete, pattern length.
4. **Continuous integration**: build and run the tests on every push (GitHub Actions on a Fedora container).
5. **Mixer**: sends, master effects, effect-chain presets, a spectrum display.
6. **VST3 hosting**, instruments first, then effects in the existing mixer slots. This is the largest item and
   deserves its own design note.
7. **Sampler improvements** (loop, envelope, trim), then automation and MIDI input.
8. **Packaging** and a decision on the license (see below).

## License

The repository carries an MIT `LICENSE`, which is a deliberate choice of the maintainer. JUCE, however, is available
only under the GPLv3/AGPLv3 or a paid commercial license, and a program linking JUCE under its free license must be
distributed under the GPL. This must be settled (relicense under GPLv3, or buy a JUCE commercial license) before
binaries are distributed.
