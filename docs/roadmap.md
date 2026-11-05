# Status and roadmap

Version 0.8.0 (alpha). This page separates what has been **verified** from what has only been **written**, and lists
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
| Live notes in the Piano Roll: held notes with the transport stopped or running, across the loop wrap, pitch/mute/track mute, sampler, safety limit, queue order and overflow, no allocation | Automated headless tests (216 checks in total) and mutation checks (transport releasing live notes, no safety limit, "off" ignored: each made tests fail) | All pass |
| Piano Roll mouse: pressing a key lights it and the channel meter moves while the button is held and falls to zero on release; sliding changes the key; drawing a note sounds while dragging and the note stays after release; right-click on a key is silent | Manual, in a virtual display with simulated mouse (meter and screenshots; the note's sound itself was not heard) | Worked |
| Browser (tree, search, fold with F8), drag from the browser onto a channel and onto the empty rack area, double-click to load, Pitch/Swing knobs, Track display, expanded mixer with live Peak/RMS, effect chain editing, bypass | Manual, in a virtual display with simulated mouse and a silent (ALSA `null`) audio device; a compressor at -60 dB / 20:1 lowered the track meter and bypass restored it | Worked |
| Project file: write/read round trip of a rich project (all pattern notes, clips, tracks, effect chains, sample sources), the loaded project rendering the same audio, version/format/type checks, NaN and out-of-range values, impossible notes and clips, size limits, truncated and empty files, missing samples keeping their reference, atomic writing, automatic file names | Automated headless tests (251 checks in total); the 32 MB size limit has no test | All pass |
| Saving and opening in the interface: title `Untitled` / `Untitled *`, Ctrl+S on an unnamed project writing `Untitled <date> <time>.slsproj` and overwriting the same file on the next Ctrl+S, File menu, New / Open recent with Save / Don't save / Cancel, reopening restoring the saved state and not the unsaved edit, opening from the command line, a corrupt file and a file with a missing sample (messages shown, current project untouched / opened silent), Save as typing a name, the open chooser listing projects and double-click loading one, closing the window with Escape (stays), Return (saves and quits) and Don't save (quits without writing) | Manual, in a virtual display with a window manager (openbox) and simulated mouse and keyboard | Worked |
| AppImage: `scripts/build_appimage.sh` builds `dist/Sun-Loops-Studio-0.6.0-x86_64.AppImage` (5 MB); started with `--appimage-extract-and-run` in a virtual display, it opened a project given on the command line (title and panels correct) | Built on Ubuntu 24.04 (glibc 2.39), run in a virtual display with a silent audio device | Worked |
| Buffer size: the `BufferTuner` search and the clean-trial rule (all paths: fast and slow computers, flaky sizes, nothing clean, bad input) | Automated headless tests (263 checks in total) and mutation checks (ignoring dropped blocks, accepting a single clean trial: each made tests fail) | All pass |
| Buffer size in the interface: default 1024 on a fresh profile; the menu switching to 256 and back (device restarts, status line and delay label follow); the choice still there after a restart; Auto testing 256 then 512 with the song playing and extra load (the load read-out showed 35 % during the test and 0 % after), choosing 512 and listing what it tested; Cancel and closing the window mid-test restoring the old size and stopping the song again; closing the program with the dialog open | Manual, in a virtual display with a window manager and a silent (ALSA `null`) device | Worked |
| Master effects: a delay tail on the master, bypass, a compressor acting on the sum of two tracks, hostile parameters, no allocation with a full chain over 2000 blocks, removing an effect stops its tail, pattern/song compilation, project round trip and an old file without a master chain; the track-assignment helpers | Automated headless tests (280 checks in total) and a mutation check (removing the master processing made 5 checks fail) | All pass |
| Master effects and track assignment in the interface: the master strip's four slots line up with the tracks and open the effect menu; Compressor and Equalizer added to the master and shown as *Effects - Master* with their parameters; saved, and found again after reopening the file; the right-click menu entry; the window listing tracks (free ones in green, current marked), first free track preselected; choosing track 8 and Assign moved the instrument (Track display, mixer routing text, status line, track selected, project marked as changed) | Manual, in a virtual display with a window manager and a silent device | Worked |
| Metronome, tap tempo, export formats, audio import, audio clips, recorder, MIDI mapping, rewind | Automated headless tests (414 checks in total): click timing, accent, BPM spacing, volume, never in an export; every export format read back with the right length and sound (MP3/AAC through ffmpeg); each import format and the sound of a video; clip placement rules, playback timing, mixer routing, loop wrap, snapshot changes, no allocation; recorder (mono/stereo, latency skip, file consistency when stopped under load, no allocation); MIDI note/off/velocity/all-notes-off | All pass (the ffmpeg-dependent checks ran with ffmpeg installed) |
| File menu with the Export submenu (all seven formats listed, an FLAC export written and read back 24-bit stereo 48 kHz), Metronome button (toggle, right-click panel with tempo, volume and tap), `.slp` saving and reopening | Manual, in a virtual display with a window manager and a silent device | Worked |
| Dragging files from a file manager onto the Playlist: all seven audio formats and an MP4 dropped together landed on consecutive tracks with waveforms; the drop highlight; move, shorten, delete and re-place by mouse; saving a project with audio clips and reopening it (clips and waveforms back); the song with the clips exported to WAV and measured | Manual, in a virtual display; the drag was simulated with a small program that speaks the X11 drag-and-drop protocol (XDND) to the window, as a file manager does | Worked |
| Recording in the interface: the input is opened on first use, the take is written, placed as a clip at the start of the armed track, a second take on an occupied track is refused with a message, the Audio dialog shows input device, channels and the level bar | Manual, in a virtual display with the silent (ALSA `null`) device, which delivers silence faster than real time (so a take ended by itself at the end of the playlist) | Worked |
| A use-after-free when clicking an item that rebuilds the browser tree (crash on *Scan for VST3 plugins...*) | Found in that manual session | Fixed (item actions are deferred); the same click was repeated afterwards |

## What has not been verified

- **Sound of the new features on real hardware.** Panels, patterns, the playlist, Song mode, pitch, swing, track
  routing and the effects were not heard through an audio device (the test environment had none); they are covered
  by offline-renderer tests and by meters in a virtual display. How the effects *sound* (EQ curve taste, reverb
  quality, compressor behaviour) has not been judged by ears. The playhead moving across the playlist during
  playback has not been seen.
- **How live notes sound on real hardware** (attack click, latency between click and sound, behaviour of a note held
  across a device restart). The virtual display's silent ALSA device runs faster than real time, so the 20-second limit
  ended a held note after about a second there; with the limit raised for the check the meter stayed lit for as long as
  the button was held.
- **The AppImage on a real Nobara machine.** It was built and run only on the build host (Ubuntu 24.04) in a virtual
  display. Not checked: FUSE mounting (the build host has no FUSE, so only `--appimage-extract-and-run` was used),
  the real audio device, the desktop's own file-type association for `.slp` / `.slsproj`, and the menu entry (AppImages do not
  create one without a helper such as AppImageLauncher).
- **Saving and opening on real data.** Only projects made in the tests and in the virtual display were used. The
  file chooser is JUCE's own, not the KDE dialog. Large projects were not timed, and the Save / Save as flow was not
  tried with a read-only folder or a full disk (a failed write is covered by a test that points the target at an
  impossible place and checks that the old file is untouched).
- **That the new default buffer and Auto stop the crackling on real hardware.** The crackling was reported on a real
  machine at a 512-sample buffer; here only a silent device was available, which never drops blocks, so Auto's
  detection of real dropouts (the audio system's count, ALSA/JACK) and the effect of the extra load were not seen
  with a computer that actually struggles. If 1024 is not enough, try a bigger size and tell us what **Audio...** shows
  (load, busiest block, dropped blocks). Whether PipeWire's ALSA layer reports every dropout is also unchecked; Auto
  says so when the count is unavailable.
- **How the master effects sound.** Only measured (levels, tails, bypass) and seen, not listened to on real hardware. The master chain was not checked in an exported WAV by ear.
- **Real input hardware.** No microphone, audio interface or MIDI keyboard was available: the input level, a take
  from a real device, the monitoring, MIDI notes from a real keyboard (and plugging one in while the program runs) and
  the device lists in the Audio dialog for real hardware were not tried. The MIDI path was checked only through the unit
  tests of the note handling.
- **Take alignment.** A take is shifted by the round-trip latency the device reports. Whether that lines up with the
  backing track on real hardware (some drivers report latency wrongly) was not measured. The virtual audio device runs
  faster than real time, so the Stop button during a take was not exercised with a real clock either (the automatic stop
  at the end of the playlist uses the same code).
- **ffmpeg on the user's machine.** AAC export and import, MP3 export and the sound of videos use `ffmpeg`; here it
  was ffmpeg 7 on the build host. MP3 import and every other format are decoded by the program itself. Other video
  containers than MP4 were not tried, nor videos with several audio tracks (the first one is used).
- **Dropping from the real file managers** (Dolphin, Nautilus) under Wayland: only the X11 protocol was simulated.
- **How the metronome sounds** on real hardware (the click is a 45 ms sine burst; nobody has listened to it).
- **Typing in the "Save as preset" name box** was not tested (the virtual display had no window manager, so dialogs
  do not receive keyboard focus); saving and loading presets is covered by the tests.
- **Memory of the effects**: about 18 MB at 48 kHz (8 tracks + master, 4 slots each), more at higher sample rates; not measured
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
| Project file extras | Samples are referenced by path, not embedded, so a project moved to another computer needs its samples copied too. No autosave, no crash recovery, no backup of the previous version, no "export project with its samples". |
| VST3 plugins | Plugins can be *scanned* (library code, no UI). Playing them as instruments or effects is not implemented: it needs plugin processing inside the audio thread, state saving, parameter handling and plugin editor windows. |
| Undo / redo | Not implemented. |
| VST3 effects in the mixer | The four slots per track hold built-in effects only. No third-party plugin can be loaded. |
| Sends, groups, more than 8 mixer tracks | A channel goes to one of 8 tracks; the master strip has a fader, a meter and four effect slots. No sends or sidechain, no per-slot wet/dry beyond what each effect offers, no effect-chain presets. |
| Automation, MIDI recording | Not implemented. MIDI keyboards play the selected channel live, but their notes are not recorded into the piano roll; notes can also be played with the mouse on the piano roll keyboard (not from the computer keyboard). Audio is recorded into the playlist, but there is no punch-in, no count-in, no loop recording and takes always start at the beginning of the song. |
| Audio clip editing | Clips can be moved, shortened and lengthened (up to the length of the audio) and deleted. No trimming of the start, splitting, fades, gain control in the interface, time-stretch or pitch-shift; a clip keeps the length it had when it was added, so after a tempo change the audio no longer ends where the clip does. At most 5 minutes of audio per file and 64 audio clips. |
| Playlist seeking | Clicking the ruler does not move the playhead (so playback and recording always start at the beginning). |
| Pattern deletion and renaming | Patterns are created and cloned only; clips cannot be re-pointed to another pattern. |
| Channel deletion and reordering | Channels can be added (up to 8) but not removed or moved. |
| Pattern length, time signature, tempo changes | A pattern is always two bars of 4/4 with a 16th grid; one BPM for the whole song. Swing exists per channel (off-beat 16ths only). |
| Sample editing | Samples play once from the start: no loop points, envelope, trim or time-stretch. |
| Export options | Fixed settings: 48 kHz, 24-bit for WAV/AIFF/FLAC, MP3 and AAC at 192 kbit/s, Ogg Vorbis at about 192 kbit/s. No choice of quality or sample rate, no stems, no tags/metadata. |
| Packaging | An AppImage can be built (`scripts/build_appimage.sh`, not yet tested on a real Nobara machine); no Flatpak or RPM; install can also be `cmake --install`. |

## Proposed order of work

1. **Undo / redo** over the edits the arrangement and channels already funnel through one place.
2. **Project extras**: autosave and recovery, embedding samples, a project folder with its samples.
3. **Playlist and pattern polish**: seeking from the ruler, pattern rename and delete, pattern length.
4. **Continuous integration**: build and run the tests on every push (GitHub Actions on a Fedora container).
5. **Mixer**: sends, effect-chain presets, a spectrum display.
6. **VST3 hosting**, instruments first, then effects in the existing mixer slots. This is the largest item and
   deserves its own design note.
7. **Sampler and audio-clip improvements** (loop, envelope, trim, fades), MIDI recording, then automation.
8. **Packaging** and a decision on the license (see below).

## License

The repository carries an MIT `LICENSE`, which is a deliberate choice of the maintainer. JUCE, however, is available
only under the GPLv3/AGPLv3 or a paid commercial license, and a program linking JUCE under its free license must be
distributed under the GPL. This must be settled (relicense under GPLv3, or buy a JUCE commercial license) before
binaries are distributed.
