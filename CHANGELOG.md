# Changelog

All notable changes to Sun Loops Studio. Newest first.

## 0.26.0 - 2026-11-07

### Added
- **Pitch Corrector**, a mixer effect in the manner of Pitcher: follows the pitch of one voice or instrument and pulls it to the
  nearest note of a key and scale (the piano roll's 13 scales), with Speed (0 ms hard tune to 500 ms), Amount, Shift (+-12
  semitones), Range (low, voice, high) and Mix; eight presets. YIN pitch detection on a decimated copy refined at the full rate;
  shifting by two crossfaded read heads that restart where the sound matches (in phase), aligned with the analysis so fast
  corrections land on the sound they were measured on. About 1-2 % of one core.

### Changed
- The effects panel shows choice parameters (scales, modes) in a wider box, so long names are not cut.

## 0.25.0 - 2026-11-07

### Added
- **Time Shaper**, a mixer effect in the manner of Gross Beat, synced to the song: time patterns (half speed, reverse, tape stop,
  slow down, stutter 1/8 and 1/16, repeat 1/4, scratch) over a cycle of 1 beat to 2 bars, volume patterns (gates 1/8, 1/16, 1/32,
  trance gate, sidechain-like pump, fades) with depth, smoothing (crossfaded jumps, soft gate edges) and mix; ten presets. With the
  transport stopped it keeps time on its own clock. Up to four per project play their time patterns.
- Effects can now follow the song position and tempo (`EffectTransport`).

## 0.24.0 - 2026-11-07

### Added
- **Additive synth engine**: up to 64 harmonics from two timbres (harmonics, brightness, even harmonics, inharmonic stretch, a formant)
  and a **morph** between them with its own envelope (and the LFO's new *Morph* target); the filter and its envelope act on the
  harmonics themselves, so nothing aliases. Sounds: Additive Organ, Morph Pad, Vowel Choir, Glass Keys, Additive Bass.
- **Physical-model synth engine**: a plucked string (Karplus-Strong with hardness, pluck position, decay, damping, stiffness, a wooden
  body, release damping; velocity-sensitive) and struck models (marimba, vibraphone, glass, bell, metal plate) made of resonators at
  their modes. Sounds: Nylon Guitar, Steel Guitar, Harp, Plucked Bass, Marimba, Vibraphone, Glass Harmonics, Church Bell, Metal Hit.
- The synth window's sound menu is grouped by engine.

### Changed
- **The master limiter is transparent below -3 dBFS**: it was a plain `tanh`, which slightly distorted every level; now it is linear up
  to 0.7 and soft above, never passing 1.

### Fixed
- Started without a graphical display, the program crashed; it now prints why and exits.

## 0.23.0 - 2026-11-07

### Added
- **Piano roll toolbar**: **Snap** (bar, beat, 1/8, 1/16, 1/32, 1/8 and 1/16 triplets, none), **Scale** (key and 12 scales: the rows of
  notes outside it are darker, its root is named on the keyboard; *Snap to scale* puts new and moved notes on it), **Chord** (18 chords
  and their inversions placed with one click), **Velocity** (a velocity lane under the notes: drag across it to draw velocities).
- **Selections**: Ctrl+drag draws a selection rectangle (Shift adds), Ctrl+click adds or removes a note, Ctrl+A / Escape; selected
  notes move and resize together; right click deletes the selection; **Ctrl+C / X / V** (paste under the mouse) and **Ctrl+D**
  (duplicate after itself); **arrow keys** move by a semitone, an octave (Shift), a step of the scale (Ctrl), a snap step or a beat.
  Alt while dragging turns the snap off.
- **Tools** on the selection (or every note): quantize (starts, ends, half way), humanize, legato, staccato, strum (up / down),
  arpeggiate chords (up, down, up and down, random; 8ths or 16ths), chop, flip, octave up / down, snap to scale, velocity crescendo /
  decrescendo / random / fixed.
- **Generators**: 12 chord progressions in the chosen key (held, syncopated stabs, eighth-note pulse or arpeggio; one or two chords a
  bar; smooth voice leading) and basslines in six styles (roots, eighths, octaves, root and fifth, walking, syncopated) on a
  progression or **following the chords of another channel** (chord recognition every half bar). They replace the channel's notes in
  the pattern; Ctrl+Z undoes them.
- `MusicTheory` (scales, chords, the tools and generators) with 44 new automated checks.

## 0.22.0 - 2026-11-07

### Added
- **A sound library built into the program** (`resources/sounds/library`, packed into the program at build time):
  - **Grand Piano** from the Salamander Grand Piano V3 (Alexander Holm, CC BY 3.0): 30 notes a minor third apart, 4 velocity layers,
    played with the sampler's envelope (it sounds while the key is held).
  - **Electric Bass** and **Electric Bass Staccato** from Growlybass (Karoryfer Lecolds, CC0): 14 notes, 4 velocity layers; keys play
    at their real pitch (E1 = the open E string), a note recorded off pitch is tuned in its zone.
  - Two **recorded acoustic kits** from the DrumGizmo multi-microphone recordings (CC BY 4.0): **Acoustic Studio** (MuldjordKit) and
    **Acoustic Rock** (DRSKit), 16 pieces each (kicks, snares, rest and rimshot, toms, closed / half-open / pedal / open hi-hat, crashes,
    china, rides and bells) with up to 4 velocity layers, mixed to stereo from all their microphones.
  - The scripts that made them (`tools/sounds`) say exactly what was changed (cutting, levels, the stereo mixdown, Ogg Vorbis).
- **Kits made by code** (`DrumSynth`): **Electronic Boom** (long analog-style kick, snappy snare, clap, metallic hats, conga, cowbell,
  clave, shaker), **Electronic Punch** (clicky kick, noisy snare, six toms, crash, ride), **Electronic Modern** (long distorted kick,
  layered snare, crisp hats, a tuned sub) and **Hand Percussion** (modal synthesis: congas, bongos, timbales, cowbell, agogo, wood
  blocks, claves, shaker, tambourine, triangle). Four velocity layers each: a harder hit is louder, brighter and, for drums, drops
  further in pitch. The same sound on every computer.
- Every built-in kit follows the **General MIDI drum order** (kick on key 36, snare 38, hats 42 / 44 / 46 in choke group 1, crash 49).
  The drum machine's *Load kit...* lists the seven built-in kits; the browser's *Drum Kits* has a folder per kit.
- **Factory presets in folders** (Keys, Bass, Synth, Drums); the grand piano and the basses are presets. **Channel presets keep the
  sampler's settings and a built-in instrument** (older preset files still load).
- **A new demo project**: drums (Electronic Punch), grand piano, electric bass and a plucked synth, 100 BPM.

### Changed
- New drum channels start with the *Electronic Punch* kit instead of the three-sound basic kit (still available).
- Sampler channels that play a multi-sample instrument (the piano, a SoundFont, slices) show their notes in the rack, like synths,
  instead of a step row for one key.
- Projects refer to a built-in instrument as `builtin:program:<id>` (program kind `builtin`).

### Removed
- **The Cyber-Hub sound theme** (sounds, kit, browser folder, docs/cyber-hub.md). A project that used one of its sounds opens with it
  silent, a message saying it was removed, and the reference kept.

### Fixed
- The piano roll showed only C2 to C7: notes below C2 (a bass at its real pitch) or above C7 could not be seen or edited. It now
  covers every MIDI key (C-1 to G9).
- Three tests left a folder in `/tmp` on every run; they now remove it.

## 0.21.0 - 2026-11-07

### Added
- **Eight new mixer effects**, with ready-made presets (a *Presets...* button in the effects panel; 42 presets in all):
  - **Overdrive**: Drive, x10 Boost, Color, Tone, Character (even harmonics), Mix, Output; runs at 4x the sample rate between
    12th-order anti-aliasing filters, with antiderivative anti-aliasing of the curve (aliases of a 5 kHz sine driven into a square
    measured more than 50 dB down).
  - **Chorus** (1-3 voices, feedback for flanging, width), **Phaser** (4/6/8/12 stages, feedback, stereo width).
  - **Filter** (low-pass, high-pass, band-pass, notch; 12 or 24 dB; drive; LFO) and **Parametric EQ** (low shelf, bell with Q,
    high shelf, output).
  - **Maximizer**, a mastering limiter in three stages (soft clip, glue compressor, 1.5 ms look-ahead brick-wall limiter with a
    ceiling no sample passes), and **Transient**, an attack / sustain splitter with *Listen* for each part.
  - **Stereo Imager**: width 0-200 %, mid and side gain, mono bass, balance.
- **Reverb**: Type (Hall, Room, Plate) and Pre-delay. **Delay**: Mode (Stereo, Ping-pong, Tape with saturated, darker repeats),
  Spread and Wobble. Both keep their old sound by default, so older projects are unchanged.
- The slot menu groups the effects (EQ and filter, Dynamics, Drive, Modulation, Space and time, Stereo); choice parameters show
  their names. Effects can have up to 8 parameters (was 5); automation works on all of them.

### Notes
- The Maximizer delays its track by 1.5 ms (there is no delay compensation between tracks yet).
- Measured on test signals (frequency responses, harmonics, aliasing, timing, levels, ceilings); not heard.

## 0.20.0 - 2026-11-07

### Added
- **The piano roll's ruler works like the playlist's** (as asked): a left click or drag moves the pattern position, also while playing
  (16ths; Alt: no snap; an orange marker shows where Play starts); a **right drag draws a loop region** that Pattern mode repeats; a right
  drag inside the region moves it, on an edge resizes it; a **right click on the region** opens *Move / Resize / Delete loop region*
  (Delete removes only the highlight). Escape or a right click cancels Move and Resize.
- Each pattern keeps its own region; it is saved in the project file (`"loop"` in the pattern) and in the undo history. A pattern made
  shorter than its region loses the region.

## 0.19.1 - 2026-11-06

### Changed
- **Playlist zoom with the mouse wheel**: the wheel now zooms in (up) and out (down) around the mouse, anywhere on the playlist; Shift+wheel,
  a sideways wheel or a touchpad swipe scroll along the song (before: the wheel scrolled and Ctrl+wheel zoomed).
- The loop region's right-click menu no longer has *Zoom to region* (it could only zoom in); it keeps *Move*, *Resize* and *Delete loop region*.

## 0.19.0 - 2026-11-06

### Added
- **FM synth** (block F, second part): **Engine > FM (4 operators)** in the synth window. Four sine operators, each with a frequency ratio
  (x 0.125 to x 32), a level and an ADSR envelope; a **modulation matrix** (any operator into any other, the diagonal is feedback) with an
  output level per operator, and eight classic algorithms to start from. The amplitude envelope, the LFO (pitch or level) and the output
  level work as for the subtractive engine.
- Six FM sounds: FM E-Piano, FM Bell, FM Bass, FM Brass, FM Drawbars, FM Growl (window menu and browser presets).
- FM patches in project and preset files (operators, matrix, outputs). `FmOperator`, `FmMatrix` and `fmAlgorithms()` in `SynthPatch.h`,
  `Player::renderFm`, the matrix grid in the synth window.

### Limits
- Sine operators only, no filter on the FM engine, no velocity sensitivity per operator, no fixed-frequency operators; the matrix uses each
  operator's output of the previous sample. Measured on test notes, not heard.

## 0.18.0 - 2026-11-06

### Added
- **Subtractive synth** (block F, first part). Right-click a synth channel > **Synth settings...**: switch the engine from **Classic** (the
  original sine sound, still the default, so older projects sound the same) to **Subtractive**:
  - two oscillators (sine, triangle, band-limited saw and square with pulse width; octave, semitone, fine tuning, level) and white noise;
  - a filter (low-pass, high-pass, band-pass; 12 or 24 dB per octave) with cutoff, resonance, its own ADSR envelope (+-6 octaves) and key tracking;
  - an amplitude ADSR envelope (notes sustain while held) and an LFO on pitch, cutoff, level or pulse width;
  - nine ready-made sounds (Saw Lead, Square Bass, Acid Bass, Soft Pad, Pluck, Brass, 808 Sub, Organ, PWM Strings) in the window's menu and
    as factory presets in the browser; a *Play C4* button.
- The patch is saved in the project (only for subtractive channels, so files with classic synths stay readable by 0.17.0) and in channel presets;
  edits are in the undo history. `src/audio/SynthPatch.h`, `src/ui/SynthPanel.*`, `src/ui/MainComponentSynth.cpp`; the voice in `Player.cpp`
  (no allocation: tested over 2000 blocks with eight voices).

### Limits
- One filter and one LFO per channel, two oscillators, no unison, no velocity-to-filter, no FM or additive engine yet (the next parts of block F).
- Measured on test notes (pitch, filter, envelopes, LFO) and seen in a virtual display; not heard on speakers.

## 0.17.0 - 2026-11-06

### Added
- **Plugin hosting** (block I): third-party **VST3**, **LV2** and **LADSPA** plugins as instruments and as effects.
  - Browser > Plugin > *Scan for plugins (VST3, LV2, LADSPA)...* scans in the background (the usual folders, including the `lib64` ones of
    Fedora / Nobara) and sorts what it finds into *Instruments* and *Effects*. The list is saved and shown at the next start; a plugin that
    crashes the scan is skipped next time.
  - **Instruments**: double-click one (a new channel), drag it onto the rack or a channel, or right-click a channel > *Instrument > Plugin
    instruments*. A plugin channel gets its notes as MIDI at the exact sample, from the steps, the piano roll, the playlist and MIDI keyboards.
    Double-click the channel (or *Plugin window...*) for the plugin's own window, or a generic one with a slider per parameter.
  - **Effects**: a mixer slot > *Plugin effects*, or drag an effect from the browser onto a channel (first free slot of its track); on tracks,
    aux buses and the master. Bypass works as for the built-in effects.
  - Projects store each plugin's description and settings; a plugin that is not installed becomes a silent placeholder that keeps them.
    Changes made in a plugin's window mark the project as changed. Exports render with the plugins (the live sound pauses meanwhile).
    Cloning a plugin channel makes a second instance with the same settings. The LV2 folders of the last scan are kept with the list, so
    LV2 plugins load after a restart without scanning again.
  - `src/audio/HostedPlugin.h` (what the engine knows of a plugin), `src/plugins/PluginHost.*` (formats, scan, known list, instances),
    `src/ui/PluginWindow.h`, `src/ui/MainComponentPlugins.cpp`; two small test plugins (`tests/plugins/`, VST3 and LV2) built with the tests.

### Changed
- **License: GNU GPL version 3** (was MIT), the license under which JUCE may be used for free. Third-party code keeps its own MIT license.
- `scripts/setup_nobara.sh` and the CI also install `ladspa-devel` (LADSPA hosting is built only when `ladspa.h` is found).

### Limits
- No VST2 (the SDK cannot be obtained legally) and no Audio Units (macOS only). Plugins run in the program's process: a crashing plugin
  closes Sun Loops Studio (autosave keeps the work).
- Stereo only: extra outputs, side-chain inputs and MIDI output of plugins are not used. Plugin parameters cannot be automated, and
  edits in a plugin's window are not in the undo history. Projects with plugins cannot be opened by older versions.
- Tried with the two test plugins and the LADSPA example plugins only, in a virtual display with a silent device: no commercial plugin,
  no real audio.

## 0.16.0 - 2026-11-06

### Added
- **Advanced sampler** (block H). Right-click a sampler channel > **Sampler settings...**: an **envelope** (attack, decay, sustain, release; with it on, notes
  sustain while held instead of playing one-shot), a **forward loop** with start and end points, a **filter** (low-pass, high-pass, band-pass with cutoff
  and resonance, per note) and the **root key**. With the envelope off and no loop a sampler plays exactly as before.
- **SoundFont 2 (.sf2).** *Load SoundFont (.sf2)...* (channel menu, sampler window, or drag a `.sf2` from the browser): choose a preset; its samples become
  **zones** by key and velocity with their root key, tuning, level, pan and loop, played by Sun Loops Studio's own sampler voices (up to four zones per
  note). The file is parsed with **TinySoundFont** (MIT, `third_party/tinysoundfont`). Projects store the file and preset; *Export project with samples*
  copies the `.sf2`.
- **Slices to notes.** The sample editor's new **Slices to sampler channel** makes a sampler channel that plays slice *n* on key C4 + *n*, with one note
  per slice in the current pattern at its original time. Markers can now be placed on a sampler channel's sample too.
- `src/audio/SamplerProgram.h` (settings, zones, slice programs; plain C++), `src/library/SoundFont.*`, `src/ui/SamplerPanel.*`,
  `src/ui/MainComponentSampler.cpp`; the sampler voice in `Player.cpp` gained envelope, loop, filter and zone pan (no allocation, tested).

### Limits
- One envelope and one filter per channel; no filter envelope, LFO, vibrato, round-robin or crossfaded / ping-pong loops; zones cannot be edited by hand.
- SoundFont modulators, LFOs, the file's filter and modulation envelope, chorus / reverb sends and exclusive classes are ignored; no SFZ, GIG or GUS.
- Channel presets do not store the sampler settings or a SoundFont. Older versions open such projects as one-shot samplers without the program.
- Measured on test tones and seen in a virtual display only: not heard on real instruments or hardware.

## 0.15.0 - 2026-11-06

### Added
- **Song position from the ruler.** A left click on the Playlist's ruler moves the playhead there, also **while the song plays** (playback carries on from
  the new place at once); dragging scrubs. Stop returns to the last place you clicked, so Play starts there again; an audio clip already under the playhead
  starts from that point of its sound. The click snaps to the grid (Alt: free) and switches to Song mode.
- **Loop region.** Right click and drag on the ruler draws a loop region (a blue *LOOP* bar, shaded over the tracks); in Song mode the song repeats it. Right
  click on it: **Move**, **Resize**, **Zoom to region**, **Delete loop region** (removes only the highlight). Right drag moves it, right drag on an edge resizes it.
  Saved in the project; exports ignore it.
- **Channels:** up to **32** (was 8); right-click menu **Clone channel**, **Move up / Move down**, **Delete channel...** (notes in every pattern and channel automation follow).
- **Undo / redo of everything** (Ctrl+Z / Ctrl+Y and the File menu): channel settings, instruments, kits, notes, patterns, pattern length, the playlist, the mixer,
  effects, sends, VCA groups, tempo, master volume. One mouse gesture is one step; 100 steps. The Playlist keeps its own clip history with the mouse over it.
- **Pattern length** 1, 2, 4 or 8 bars for the whole project (the **+** next to the pattern box > *Pattern length*); the timeline holds at least 64 bars.
- **Safety:** autosave once a minute while there are unsaved changes, **recovery after a crash** at the next start, the previous version kept as `<name>.slp.bak`
  when saving over it, and **File > Export project with samples...** (a folder with the project and copies of all its sounds, referred to by relative paths;
  relative paths are now understood when opening a project).
- **Continuous integration:** `.github/workflows/build.yml` builds on Fedora 41 and runs the tests on every push.

### Changed
- The old note-only undo of the Piano Roll and the step sequencer is replaced by the project-wide history.
- Projects store `patternBars` and `loopRegion`; older versions ignore them (a project with 4- or 8-bar patterns opens in an older version with its notes cut to two bars).

### Limits
- Recording still starts at the beginning of the song (no punch-in); the loop region only works in Song mode and live.
- The pattern length is the same for every pattern; no time signatures or tempo changes.
- Not heard on real hardware; see the roadmap.

## 0.14.0 - 2026-11-06

### Added
- **Drum machine.** A new kind of channel (*+ Add channel* > *Drum machine (16 pads)*, the *Drum machine* item of the browser's Instruments, or right-click a channel >
  *Instrument*). Double-click it (or right-click > *Drum machine...*) to open its window: a **4 x 4 pad grid** (click to hear a pad, Shift = soft, Ctrl = hard), an
  **inspector** for the selected pad and a **step sequencer with one row per pad** (32 steps, click to toggle, drag to paint, right-click to erase, Shift / Ctrl for
  soft / accented steps) that edits the channel's notes in the current pattern. Pad *n* answers to MIDI note 36 + *n* (C2 to D#3), so the piano roll, MIDI keyboards and
  imported notes play pads too.
- Each pad has up to **four sound layers chosen by velocity** (every layer has the lowest velocity it answers to), its own **level**, **pan**, **tuning** (+-24 semitones),
  **swing** (off-beat 16ths, per pad), **mute / solo** and a **choke group** (1 to 8: a pad that starts cuts the sounding pads of its group, so a closed hi-hat stops an
  open one). Sounds are loaded with *Load* or dropped from the browser on a pad or on a layer row.
- **Kits.** *Load kit...* offers the Sun Loops Basic Kit, the Cyber-Hub Kit, any folder (a folder with a `kit.json`, or just a folder of audio files: the first 16 in name order
  fill the pads) and every folder in *Drum Kits*. *Save kit...* writes a **kit folder**: `kit.json` plus copies of the sounds in `samples/` (built-in sounds stay references), so
  the folder can be moved or shared. The project file stores each drum channel's kit (names, settings, where every sound comes from); the audio is read when the project opens.
- `src/audio/DrumKit.h` (model, plain C++), `src/library/DrumKitFile.*` (kit files and descriptions), `src/ui/DrumMachine.*` (window), the engine in `Player.cpp` (one more
  instrument; no allocation on the audio thread, tested).

### Limits
- Kit edits (sounds, levels, names ...) cannot be undone; the steps can (like other notes). 16 pads, 4 layers, 30 seconds per sound; no round-robin layers.
- The whole channel goes to one mixer track (no output per pad); choke groups work inside one channel; a drum channel ignores the channel's Swing knob (the pads have their own).
- A drum channel cannot be saved as a channel preset (save the kit instead). A sound dropped on a drum channel's row in the rack is refused: drop it on a pad.
- Projects with a drum machine cannot be opened by older versions (they report an unknown instrument). MIDI import still skips percussion.
- Layer choice, choke and levels were tested on synthetic tones and in a virtual display only: **not heard on real drums or real hardware**.

## 0.13.0 - 2026-11-06

### Added
- **Sample editor.** Right-click an audio clip in the playlist, or a sampler channel in the Channel Rack, and choose *Edit sample...*. A window
  shows the waveform (both channels, zoom with the wheel, scroll with Shift + wheel) with a selection and a **preview** that plays through the
  audio engine (Play / Stop, Space, optional loop of the selection) while a **real-time spectrum analyser** follows what is heard.
  Edits are destructive on a copy and each one is one step of Undo / Redo (Ctrl+Z, Ctrl+Shift+Z): **cut**, **crop**, **silence**, **reverse**,
  **invert phase**, **gain** (dB), **normalise** (to a peak level), **remove DC offset** and **fade in / out** (linear, curve or S curve).
  **Noise reduction by spectral gating:** select a stretch that holds only the noise, choose *Reduce noise...* (reduction in dB, sensitivity)
  and the rest of the sample is lowered wherever it does not rise above the noise (2048-point STFT, per-channel noise profile, smoothed in
  frequency and time against "watery" artefacts).
- **Slicing** (audio clips): *Find hits* puts a marker at the start of every hit, note or word it can find (spectral-flux onset detection with a
  *Sensitivity* slider), *Equal parts...* puts markers on a grid, double-click on the waveform adds or removes a marker. On *Apply* the clip is cut
  at the markers into separate clips on the playlist (one Undo step), ready to be rearranged.
- Applying never touches the original: the result is written as a **new 32-bit float WAV file in `Samples/Edited`** (never overwriting an earlier edit) and
  that file is what the clip or channel uses, so the project file needs nothing new. A clip that is stretched must first be taken *Back to the original*.
- `src/audio/SampleEdit.*` (all the editing, noise reduction, onset detection and WAV writing, plain C++ and tested headless), `src/audio/Auditioner.h` (the
  preview voice mixed after the master chain) and `src/ui/SampleEditor.*`.

### Limits
- After *Apply* a clip plays the whole edited audio from its start (shortened if a neighbour is in the way); a trimmed clip is edited as the part it plays.
- Slicing is for playlist clips only (a sampler channel cannot be sliced into notes yet: that belongs to the advanced sampler block); slices are not turned
  into a pattern or piano-roll notes.
- Noise reduction and hit detection were measured on synthetic signals only and **not judged by ear on real recordings**; nothing was heard on real hardware.

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
