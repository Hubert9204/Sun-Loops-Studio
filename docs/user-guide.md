# User guide

*[Italiano](user-guide.it.md)*

This guide describes what Sun Loops Studio 0.6 does today. Limits and missing features are listed at the end.

## The window

A **toolbar** on two rows, a status line, and below it the **browser** (left sidebar) and the **workspace** where
the panels float.

Toolbar, first row: **File** (projects, see below), **Play / Stop**, **Pattern / Song** switch, the **pattern box** with **+**, **BPM** (40-240),
**Master** volume with its meter, the **Metronome** button, **Record** (with an options arrow) and **Audio...**
(device settings). Exporting is in the **File** menu.

Toolbar, second row: buttons that show or hide the browser (**Browser F8**) and the panels (**Rack F6**,
**Playlist F5**, **Piano F7**, **Mixer F9**),
**Reset** (default panel layout) and **Clear notes** (empties the selected channel in the current pattern).

The status line shows the audio device (sample rate, buffer size, reported latency) and the last message.

## Projects

**The demo song (0.52.0).** On its first start the program opens with "Sunrise Drive" (since 0.53.0 only then; later starts begin
with a new, empty project), 44 bars of synthwave in A minor at 110 BPM, in Song mode: press
**Space** to hear it. It is made to show the program at work - open the panels while it plays:

| Bars | Section | What plays |
| --- | --- | --- |
| 1-4 | Intro | the start-up sound as an audio clip, grand piano, SunBurst's "Warm Pad" with its filter opening (automation) |
| 5-12 | Verse | the drum machine (Electronic Punch), the sampled electric bass, piano, a marimba (physical model) moving left and right (pan automation), FM bells |
| 13-16 | Build | a snare roll, a reversed cymbal (audio clip, reverse, fade-in) and a tape stop on the drums (Time Shaper automation) |
| 17-24 | Chorus | SunBurst "Sun Lead", HexForge "Chrome Bass" with a wobbling filter (plugin automation), the pad pumping with the drums (sidechain) |
| 25-28 | Breakdown | an additive choir and subtractive strings, the master low-pass sweeping open (master effect automation), the piano's reverb send growing |
| 29-32 | Build | the second riser, the same cymbal time-stretched a minor third lower |
| 33-40 | Last chorus | everything, plus HexForge "Neon Pluck" arpeggios and the delay bus coming up (aux automation) |
| 41-44 | Outro | the synths' VCA group fading out, the tempo slowing from 110 to 80 BPM (tempo markers) |

Every channel has its own mixer track with insert effects (EQ, compressor, transient, chorus, phaser, delay, overdrive, filter,
stereo imager, Time Shaper), aux A is a hall reverb and aux B a ping-pong delay, the master has a filter, an EQ and the maximizer, and
two VCA groups hold the synths and the drums and bass. Without SunBurst and HexForge (a build without them) their four channels play
built-in synths. *File > Open the demo song* brings it back at any time; it is not saved anywhere until you save it.

Everything in the song is saved in one `.slp` file: channels (instrument, sample, volume, pan, pitch, swing,
mixer track), all patterns and their notes, the playlist, the mixer (faders, balance, mute/solo, effect chains and
their settings), the audio clips of the playlist (as paths of their files), BPM, master volume and whether Song mode is on. The window title shows the project name, with a
`*` after it while there are changes that are not saved.

| Command | Key | What it does |
| --- | --- | --- |
| **New** | `Ctrl+N` | Starts an empty project (one synth channel, one empty pattern). |
| **Open...** | `Ctrl+O` | Chooses a `.slp` file. |
| **Open recent** | | The last eight projects that still exist. |
| **Save** | `Ctrl+S` | Writes the project. |
| **Save as...** | `Ctrl+Shift+S` | Asks for a name (the `.slp` ending is added if you leave it out) and writes the project there. |
| **Export project with samples...** | | Asks for the name of a new folder and writes the project into it together with a copy of every sound it uses (see below). |
| **Undo / Redo** | `Ctrl+Z` / `Ctrl+Y` | Undoes or redoes the last edit, of any kind (see *Undo and redo*). |
| **Import audio or video into the playlist...** | | Chooses audio or video files and puts them on the playlist (see *Audio clips*). |
| **Export audio** | | A submenu with the formats (see *Export*). |
| **Show the projects folder** | | Opens `~/Music/Sun Loops Studio/Projects`. |

**Saving without a name.** A project you have not named yet is called *Untitled*. **Save** (or `Ctrl+S`) does not
stop to ask: it writes the project at once as `Untitled <date> <time>.slp` in
`~/Music/Sun Loops Studio/Projects`, and from then on that is the project's file, so the next **Save** updates the
same file. Use **Save as...** whenever you want a proper name. Saving a named project writes the file next to its
old copy safely: the new text goes to a temporary file that then replaces the old one, so a failed save leaves the
previous file intact.

**Unsaved work.** **New**, **Open...** and closing the window ask first when there are unsaved changes: *Save*,
*Don't save* or *Cancel*. Pressing Escape cancels. Selecting a pattern or switching Pattern/Song is not counted as a
change. You can also start the program with a project as argument (`sun-loops-studio song.slp`); a `.slp`
file opened from the file manager does the same once the desktop knows the type (installed with
`cmake --install`).

**Samples and audio clips are not copied into the file.** A sampler channel or an audio clip stores where its audio came
from: a built-in sound or the full path of your file. If you move or delete the file, the project still opens: that channel is silent, a
message lists the missing files and the reference is kept, so putting the file back (or loading another with
*Load sample...*) fixes it. Keep your samples where they are, or in `~/Music/Sun Loops Studio/Samples`.

**Taking a project to another computer.** *File > Export project with samples...* asks for the name of a new folder (it must not exist yet,
or be empty) and writes `<name>.slp` into it plus a `Samples` folder with a copy of every sound the project uses: sampler channels, drum-kit
layers and audio clips. The project refers to the copies by **relative paths**, so the whole folder can be moved, zipped or copied to another
computer and opens there with its sounds. Built-in sounds stay built-in; a file that cannot be found is listed and keeps its old path.

**Backup, autosave and recovery.** Saving over an existing project first keeps the previous version next to it as `<name>.slp.bak` (one
generation). While there are unsaved changes the project is also written **once a minute** to `~/Music/Sun Loops Studio/Autosave/Autosave.slp`.
That file is deleted when the program closes normally; if the program crashed (or was killed), the next start asks **Recover your work?**
(after the crash report question, see *Crash reports* below) and
*Open the recovered project* opens it (marked as not saved, under its old name if it had one: `Ctrl+S` puts it back where it belongs).

**Damaged files.** A file that is not a Sun Loops Studio project, is damaged or was written by a newer version is
refused with a message and your current project is left untouched. Values that are out of range are clamped and notes
or clips that cannot be placed are dropped, with a message saying so.

## Crash reports, session log and updates (0.43.0)

**Session log.** Every session writes a log to `~/.local/state/SunLoopsStudio/logs/session.log` (or `$XDG_STATE_HOME/SunLoopsStudio`):
the version and system at the start, the audio device, every message of the status line, problems and the update check. When the file
reaches **5 MB** it becomes `session.1.log` (then `session.2.log`, `session.3.log`; older ones are deleted). *File > Updates and crash
reports > Show the session log folder* opens the folder.

**When the program crashes.** If Sun Loops Studio is ended by a crash (SIGSEGV, SIGABRT, SIGFPE, SIGILL or SIGBUS, or an uncaught
error), it writes what happened to `crash_last_session.log` in the same folder before closing: the signal, the stack trace (the
functions that were running), the version, the system and the audio driver, device, sample rate and buffer. At the next start it asks:

> *Sun Loops Studio closed unexpectedly during the last session. Do you want to send an anonymous bug report to GitHub to help us fix it?*

| Button | What it does |
| --- | --- |
| **Send report** | Creates an issue in the project's GitHub repository (Hubert9204/Sun-Loops-Studio) with the crash, the system and the stack trace in Markdown code blocks, and shows its number. The crash file is moved to `crashes/` only after GitHub confirmed it; if sending fails, the reason is shown and the file stays. |
| **Show log before sending** | Shows the whole report exactly as it will be sent, then Send or Ignore. |
| **Ignore / Delete** | Deletes the crash file; nothing is sent. |

Closing the window keeps the file: the question comes back at the next start. Then **Recover your work?** follows as before.

**What is sent, and what is not.** The report holds the signal, the stack trace, the program version, the operating system,
kernel and processor, the audio driver, device name, sample rate and buffer size, and the last 40 lines of the session log before
the crash. Your home folder is replaced by `~` and your user name by `<user>`. No project, no sound and no file is sent, and nothing is
sent without your click.

**The GitHub token.** Creating an issue through GitHub's API needs a token. *File > Updates and crash reports > GitHub token for crash
reports...* takes one (a fine-grained token with *Issues: Read and write* on the repository only); it is kept in the settings file,
readable only by you. Without a token, **Send report** opens GitHub's *new issue* page in your browser with the report filled in
(you need a GitHub account): press *Submit new issue* there, then *I sent it: delete the file* in the program.

**Checking for updates.** A few seconds after the start the program asks GitHub, in the background, for the latest release
(`releases/latest`, 5 seconds at most; no network or no answer is simply ignored). When a newer version exists (compared by
Semantic Versioning: 0.43.0 is newer than 0.42.0, 1.0.0 is newer than 1.0.0-rc.1), a window shows it with the start of its notes:
**Download** opens the AppImage (or the release page), **Later** asks again at the next start, **Skip this version** stops asking
about that one. *Check for updates at start-up* in the same menu turns the automatic check off; *Check for updates now* asks at
once and also tells you when you are up to date. The check sends no personal data (only the program name and version, as the
User-Agent GitHub requires). Releases are published from 0.51.0 on, so the check finds every version after that.

## Panels

There are four panels: Channel Rack, Playlist, Piano Roll and Mixer. They all behave the same way.

| Action | How |
| --- | --- |
| Move | Drag the title bar. The panel stays inside the workspace. |
| Resize | Drag any edge or corner. Each panel has a minimum size. |
| Close | Click the **x** in the title bar. |
| Maximise / restore | Double-click the title bar. |
| Bring to front | Click anywhere on the panel. The active panel has an orange title bar. |
| Open / raise / close | Toolbar button or key. A closed panel opens; an open panel that is behind others comes to the front; a panel that is already on top closes. |
| Default layout | **Reset** |

Panels may overlap, so you can keep the piano roll over the rack while you write and bring the mixer up when you need
it. Positions, sizes and open/closed state are saved when you change them and restored at the next start (file:
`~/.config/SunLoopsStudio/SunLoopsStudio.settings`; delete it to start from the default layout).

Default layout: the browser open on the left; the Channel Rack across the top of the workspace with the Playlist
below it; Piano Roll and Mixer closed. The Mixer opens large, over everything else.

Layouts saved by versions before 0.5 are ignored once (the panel settings were renamed); **Reset** or just moving
the panels saves the new ones.

## Helios: ask for help (0.40.0)

Press **F1** or the **Helios** button and type what you want to do, in English or Italian ("how do I record two microphones?",
"come aggiungo un riverbero?"). Helios searches **this manual** and the program's **commands** on your computer: no AI, no internet,
nothing is sent anywhere.

- Results marked **Do:** are commands (open the Mixer, record, export, open an FL Studio project, slice a loop...): double-click one,
  press **Enter** or **Do it** and Helios does it.
- The others are sections of the manual (English, or marked *(italiano)*): click one to read it on the right.

How it finds things: it looks for the words of your question (without accents, plurals and common endings, and ignoring words like
"how", "the", "come", "un"), counts titles more, understands a few FL Studio names (insert = mixer, Slicex = Slicer, Newtone = pitch
editor, VST = plugin) and prefers the manual in the language of the question. It does not understand sentences: if nothing useful
comes up, try fewer or other words.

## Keyboard

| Key | Action |
| --- | --- |
| Space | Play / Stop |
| Ctrl+N / Ctrl+O | New project / Open project |
| Ctrl+S / Ctrl+Shift+S | Save / Save as |
| F5 | Playlist |
| F6 | Channel Rack |
| F7 | Piano Roll |
| F8 | Browser (fold / unfold) |
| F9 | Mixer |

The keys work when the main window is active. Typing into a text field (for example renaming a channel) takes the
keyboard for that field.

### Playing from the computer keyboard (Typing keys)

With **Typing keys** on (the toolbar button, on by default; remembered), the computer keyboard plays the **selected channel**
like a MIDI keyboard (0.32.0), whether the song is playing or not; **Rec notes** records it and the score log keeps it.

```
  W E   T Y U   O P          C#  D#    F#  G#  A#    C#  D#
 A S D F G H J K L ; '       C  D  E  F  G  A  B  C  D  E  F
```

**A** plays **C5** (MIDI note 60). **Z** / **X** move the keyboard an octave down / up, **C** / **V** make the notes softer /
louder (velocity 1, 20, 40, 60, 80, 100, 110, 127; 100 at first); the status line shows where A is and the velocity. On an
Italian keyboard the keys where ; and ' are (**ò** and **à**) play E and F; the letters are what count, so on a German
keyboard Z and Y swap places. The keys take effect when no text field has the keyboard; with Ctrl or Alt held they keep
their usual meaning, and with the mouse over the Playlist its own keys (S splits, 1 to 5 choose a tool) come first. Note names follow FL Studio: middle C (MIDI 60) is **C5**.

## Browser

*(New in 0.22.0: **Drum Kits** lists every built-in kit (each pad can be dragged), and **Presets > Factory** is filed in **Keys**
(the Grand Piano), **Bass** (the Electric Bass and its staccato version), **Synth** and **Drums**: drag a preset onto a channel or onto
the empty part of the rack. The Cyber-Hub sounds of earlier versions were removed: a project that used them opens with those sounds
silent and a message. The program still plays its start-up sound when it opens: switch it off in **File > Play the start-up sound**.)*

The sidebar on the left lists your sounds as a tree. Click the arrow of a folder to open it.

| Folder | Contents |
| --- | --- |
| Samples | The built-in sounds (kick, snare, hat), then the audio files (WAV, FLAC, OGG, AIFF) found in `~/Music/Sun Loops Studio/Samples`, with its sub-folders. |
| Drum Kits | A built-in kit, then one entry per sub-folder of `~/Music/Sun Loops Studio/Drum Kits`. |
| Instruments | *Synth* and *Sampler*. |
| Presets | The factory channel presets (six basic ones and, since 0.18.0, the subtractive synth sounds), then your own `.slspreset` files from `~/Music/Sun Loops Studio/Presets`. |
| Plugin | *Scan for plugins (VST3, LV2, LADSPA)...* searches the usual plugin folders in the background; the list is kept for the next start. Found plugins are sorted into **Instruments** and **Effects** (see *Plugins* below). |

The three folders under `~/Music/Sun Loops Studio` are created when the program starts. **Folder** opens that
location in your file manager; **Refresh** looks for new files (the browser does not watch the disk).

- **Search**: type in the box at the top to filter every folder at once; matching folders open by themselves and the
  line under the tree counts the results. **Esc** clears the search.
- **Drag and drop**: drag a sound onto a channel of the Channel Rack (the row is outlined in green) to load it
  there; drop it on the empty space below the rows to create a new channel for it. A preset sets the channel's
  instrument, sample, volume, pan, pitch and swing; an instrument item switches the channel's instrument.
- **One click** on a sound (an audio file or a built-in sample) plays it once, whether the song is playing or not (0.31.0);
  click it again to hear it from the start, click another to hear that one. Long files play their first 30 seconds.
- **Double-click** an item to apply it to the selected channel.
- **Fold away**: the arrow button in the browser's title, **F8** or the **Browser** toolbar button. A thin tab stays
  on the left edge to bring it back. The state is remembered.

Only drags that start in the browser are supported; dropping files from the desktop file manager onto the program is
not implemented yet (copy them into the Samples folder and press Refresh).

## Channel Rack

One row per channel: a **Track N** display and the name, **M** (mute), **S** (solo), volume, pan, the **Pitch** and
**Swing** knobs, a level meter and, on the right, the step area.

- **Volume** goes from 0 to 1.5 (default 0.8) and **pan** from left to right; double-click a slider to reset it.
- **Pitch** tunes every note of the channel from -12 to +12 semitones in steps of 0.1 (on a sampler channel it
  changes the speed of the sample, so it also changes its length). Drag the knob up or down; double-click for 0.
- **Swing** from 0 to 100 %: the off-beat 16th notes (the 2nd, 4th, 6th... of each beat) are delayed, by up to half
  a 16th step at 100 %. Notes that would be pushed past the end of the pattern stay where they are.
- **Track N** is the mixer track the channel plays through. Click it for a menu of the mixer's tracks (grouped by 50 when there are many), or drag it up or
  down, or scroll over it. By default channel 1 goes to Track 1, channel 2 to Track 2, and so on.
- **The step area** (0.31.0, as in FL Studio) shows either **squares** - the step sequencer - or a **preview of the
  Piano Roll**, chosen by the notes themselves:
  - Every new channel starts with squares, one per 16th note of the pattern. **Left click** (or drag) writes steps,
    **right click** (or drag) erases them. A step plays the channel's root note (MIDI note 60 unless the sampler's *Root* is set otherwise),
    or the kick (pad 1) on a drum channel. The step the song is on lights up while it plays.
  - **The grid is exactly the pattern** (0.42.0). A new project starts with **one bar of 4/4: 4 quarters, 16 steps**,
    all of them usable; nothing is greyed out and nothing repeats or grows by itself.
  - The bar at the top of the Channel Rack reads the length (*Pattern: 4 quarters (1 bar) - 16 steps*) and has two
    buttons: **+** adds **one quarter** (4 more steps) and **-** takes one away. The shortest is 4 quarters (the - button
    is then disabled), the longest 64 (16 bars). The length is the same for every pattern; steps past a shorter end are
    kept but not played, and come back with +. Playlist clips stay where they are (see *Patterns*). Ctrl+Z undoes it.
  - As soon as the channel has a note that is not a plain step - another pitch, a longer note, a note off the 16th
    grid, written in the Piano Roll, the drum machine, recorded or generated - the row shows the **preview of the
    Piano Roll** instead. **Left click** on the preview opens the Piano
    Roll, or brings it forward - for drum channels too since 0.53.0 (the drum machine opens with a double click on the channel or from its
    menu); right click opens the channel's menu.
  - Delete every note in the Piano Roll and the row goes back to squares.
  - Squares are drawn from the notes every time, so they always show what plays.
  - Projects from before 0.31.0 open with every channel's sequence as long as the pattern, so they play exactly as before.
    Projects from 0.31.0 to 0.41.0 whose sequences repeated a shorter part open with those repeats written out as steps, so
    they sound the same too.
- The **Load** button of a sampler channel picks a sample file.
- Click a row to select that channel (the Piano Roll and the Mixer follow the selection).
- **+ Add channel** under the last row adds a synth, sampler or drum-machine channel, up to 32 channels in total
  (channels can share mixer tracks, and the mixer can have up to 500).

### Right-click menu on a channel

| Item | Effect |
| --- | --- |
| Drum machine... | (Drum channels) opens the drum machine window. |
| Piano roll | Selects the channel and opens the Piano Roll for it. |
| Instrument > Synth / Sampler | Chooses what makes the sound. Switching to Sampler on a channel without a sample asks for one. The sample is kept if you switch back to Synth. |
| Instrument > Plugin instruments | The instrument plugins found by the last scan: the channel plays the chosen plugin (see *Plugins*). |
| Plugin window... | (Plugin channels) opens the plugin's own window. A double-click on the channel does the same. |
| Load sample... | Pick a WAV, FLAC, OGG or AIFF file (the first 30 seconds are used). The channel is renamed after the file. |
| Rename... | Up to 16 characters. |
| Clone channel | Adds a copy right under it: same instrument, sound and settings, and the same notes in every pattern. |
| Move up / Move down | Moves the channel one row; its notes in every pattern and its automation clips go with it. |
| Delete channel... | After a confirmation, removes the channel, its notes in every pattern and the automation clips that move its volume or pan. The last channel cannot be deleted. `Ctrl+Z` brings it back. |
| Assign free mixer track | Opens a window listing the mixer tracks (a scrolling list; the last row offers a new track): free ones (no instrument on them) in green, the others with the instruments that are on them, and the current one marked. The first free track is selected for you; **Select first free track** picks it again, or choose any track (sharing one is allowed), then **Assign** (or Return; Escape cancels). The channel's **Track N** display and the mixer follow, and the mixer selects that track. |
| Save as preset... | Asks for a name and writes a `.slspreset` file (instrument, sample, volume, pan, pitch, swing; not the notes, the sampler settings or a SoundFont) into `~/Music/Sun Loops Studio/Presets`; it appears in the browser's Presets folder at once. |
| Synth settings... | (Synth channels) opens the synth window: ready-made sounds, the engine and every setting of the subtractive synth (see *Synth*). |
| Sampler settings... | (Sampler channels) opens the sampler window: envelope, loop, filter, root key (see *Sampler and SoundFonts*). |
| Load SoundFont / SFZ (.sf2, .sfz)... | Chooses a `.sf2` file, then one of its presets, or an `.sfz` instrument: the channel becomes a sampler that plays it (see below). Both can also be dragged from the browser onto a channel. |

Double-clicking a synth channel's note preview, or the name of any channel, also opens its Piano Roll. On sampler
channels double-clicking the steps is not special: every click toggles a step.

## Synth

Right-click a synth channel > **Synth settings...** opens its synth window. Every synth channel starts with the **Classic** engine, the
original sound of Sun Loops Studio (a sine with two harmonics), so older projects sound as they did. Switch **Engine** to **Subtractive**,
or pick a ready-made sound from the menu at the top (*Saw Lead, Square Bass, Acid Bass, Soft Pad, Pluck, Brass, 808 Sub, Organ, PWM Strings*),
to shape the sound. **Play C5** sounds the channel while you hold it. The same sounds are in the browser under *Presets > Factory*.

| Section | Settings |
| --- | --- |
| Oscillator 1 and 2 | Wave (sine, triangle, saw, square), level, octave (-3..+3), semitone (-12..+12), fine tuning in cents, pulse width (square only). Saw and square are band-limited (PolyBLEP), so high notes do not alias much. |
| Noise | Level of white noise added to the oscillators. |
| Filter | Off, low-pass, high-pass or band-pass; **24 dB / octave** puts two filters in a row; cutoff, resonance, how far the filter envelope moves the cutoff (up to 6 octaves up or down) and key tracking (the cutoff follows the note, from C5, MIDI 60). |
| Amplifier envelope | Attack, decay, sustain, release: the note sounds while it is held, then fades with the release. |
| Filter envelope | Attack, decay, sustain, release of the cutoff movement. |
| LFO | Target (pitch: vibrato up to 2 semitones; cutoff: up to 3 octaves; level: tremolo; pulse width; morph: additive only), wave, rate (0.05-20 Hz), depth. The LFO starts again with every note. |
| Output | The level of the synth before the channel's volume. |

### FM engine

**Engine > FM (4 operators)** replaces the oscillators, noise and filter with four sine **operators**. Each has a frequency **ratio** to the
note (x 1 is the note, x 2 an octave up, x 3.5 a bell-like overtone), a **level** and its own **envelope**. The **matrix** says who modulates
whom: the row is the operator being modulated, the column the modulator, the diagonal (orange) is an operator's feedback, and the
**Out** column (green) is how much of each operator you hear. Drag a cell up or down to change it. The **Preset** menu above it sets a
classic arrangement (*2 > 1*, stacks of three and four, two pairs, three modulators into one, one into three, feedback, all heard);
after a change by hand it shows *(your own)*. The amplitude envelope, the LFO (pitch or level) and the output level apply as for the
subtractive engine. Ready-made FM sounds: *FM E-Piano, FM Bell, FM Bass, FM Brass, FM Drawbars, FM Growl*.

### Additive engine (0.24.0)

**Engine > Additive** builds the sound from up to 64 harmonics. **Timbre A** and **Timbre B** each set them: how many (**Harmonics**),
**Bright** (the slope: 60 % is a saw, 100 % flat, 0 % very dark), **Even** (the level of the even harmonics: 0 % gives a square-like,
hollow sound), **Stretch** (inharmonicity: the upper harmonics run sharp, as in bells and a piano's high strings) and a **Formant** (a
vowel-like peak: amount, **Freq** 150 Hz - 6 kHz, **Width**). **Morph** sets where the sound sits between A and B, and the **morph
envelope** (with its amount, also negative) moves it over time; the LFO can move it too (target *Morph*). The **filter** and its envelope
act on the harmonics themselves, so they never alias, and a resonant peak is exact. Harmonics above half the sample rate are left out.
Ready-made: *Additive Organ, Morph Pad, Vowel Choir, Glass Keys, Additive Bass*.

### Physical engine (0.24.0)

**Engine > Physical model** models the instrument instead of a waveform. **Model**: a **plucked string** (Karplus-Strong: one period of
noise rings in a loop that loses its highs a little on every trip), **marimba**, **vibraphone**, **glass**, **bell** or **metal plate**
(resonators at the modes of the bar, glass, bell or plate, rung by a mallet). **Hardness** is the pick or the mallet (soft and dull to
hard and bright; the velocity adds to it, and makes the note louder), **Position** where it is plucked or struck. **Decay** is how long it
rings (0.15 to 15 s), **Damping** how much faster the highs die, **Stiffness** (strings) makes the upper partials sharp like a steel
string, **Body** (strings) adds the resonances of a wooden body, **Rel. damp** how quickly a released note is damped (0: it rings on, like
a harp or a bell). The envelope has an attack and a release only: the model decays by itself. Ready-made: *Nylon Guitar, Steel Guitar,
Harp, Plucked Bass, Marimba, Vibraphone, Glass Harmonics, Church Bell, Metal Hit*.

Legato and slide (Shift+click in the Piano Roll) work as with the classic engine. A synth preset (*Save as preset...*) stores the sound too.
Every change can be undone with `Ctrl+Z`.

## Sampler and SoundFonts

Right-click a sampler channel > **Sampler settings...** opens a window for that channel:

- **Sound**: the sample (or the SoundFont preset / slice program) the channel plays, **Load sample...**, **Load SoundFont (.sf2)...**, and **Use the
  sample** (forgets a program, the channel plays its own sample again). **Root key**: the key at which the sample plays at its own pitch (C5 = 60).
- **Envelope**: off (the default), a note plays its sample once to the end, whatever its length, as in every earlier version. On, a note sounds while
  it is held: **Attack** (fade in), **Decay** to the **Sustain** level, and **Release** after the note ends. Notes played by hand (piano roll, MIDI
  keyboard) are held too.
- **Loop**: *Forward* repeats the part between **Loop start** and **Loop end** (in % of the sample) while the note is held, so a short sound can last.
- **Filter**: *Low-pass*, *High-pass* or *Band-pass*, with **Cutoff** (20 Hz to 20 kHz) and **Resonance**. Each note is filtered on its own.

**SoundFonts.** *Load SoundFont (.sf2)...* (in the channel menu or in the sampler window) asks for a file and lists its presets (by bank when there
are several); choose one. The preset's samples become **zones**: each answers to a range of keys and velocities, with its own root key, tuning, level,
pan and loop, exactly as the file says, and the envelope is set from the file (you can change it). The project stores the file and the preset, and
*Export project with samples* copies the `.sf2`. Not taken over from the file: modulators, LFOs and vibrato, its filter and modulation envelope,
chorus / reverb sends and exclusive classes. Files up to 512 MB.

**SFZ instruments (0.41.0).** An `.sfz` file (a text file that maps sample files to keys and velocities, common for free sampled
instruments) loads the same way: *Load SoundFont / SFZ...*, or drag it from the browser. Read: `<control>` *default_path*, the
`<global>` / `<master>` / `<group>` / `<region>` levels with *sample*, *key*, *lokey*, *hikey*, *pitch_keycenter* (keys by number
or name: c4 = 60), *lovel*, *hivel*, *tune*, *transpose*, *volume*, *pan*, *loop_mode* (continuous / sustain loop) with
*loop_start* and *loop_end*. Everything else (filters, envelopes, round robin, crossfades, `#include`) is left out, and the status line
says how many settings were. Samples are found relative to the `.sfz`'s folder. The project stores the `.sfz`; *Export project with
samples* copies its whole folder.

**Slices to notes.** In the sample editor (a clip or a sampler channel), place markers (*Find hits*, *Equal parts...* or double-click) and press
**Slices to sampler channel**: the edited audio is saved as a new WAV file, a **new sampler channel** plays slice 1 on C5, slice 2 on C#5 and so on
(one key each, at their own pitch), and the current pattern gets **one note per slice at the time it had in the audio**, so the pattern plays the
loop as it was and you can move, repeat or delete the notes to make a new groove.

### Slicer (0.37.0)

The **Slicer** cuts a loop into slices, as FL Studio's Slicex: slice 1 plays on **C5**, slice 2 on C#5 and so on, and the pattern plays
them with one note per slice. Because the notes are in beats, the loop **follows the project's tempo**: slow the song down and the slices
are struck further apart, each at its own speed and pitch.

- **A new loop**: *+ Add channel* > *Slicer (slice a loop)...*, choose an audio file. It is cut where its sounds start, the current
  pattern gets one note per slice, and the Slicer window opens.
- **A channel's sample**: right-click a sampler channel > *Slicer...* (also for slices made with *Slices to sampler channel*).

In the window:

| Action | How |
| --- | --- |
| Hear a slice | Click it (it plays on its key through the channel, while the button is held) |
| Move a marker | Drag it (the first slice always starts at the beginning) |
| Add a marker | Double-click |
| Delete a marker | Right-click it |
| Slice again | *Slice by sound* (with **Sensitivity**: higher finds more), *Slice by grid* (1/4 to 1/32 at the **Loop tempo**), *Equal parts...* |
| One slice at a time | *A slice cuts the one before* (on by default, as in Slicex): a new slice stops the one still sounding (5 ms fade); off, they overlap |
| Change a slice | Select it, then **Pitch** (-24 to +24 semitones), **Level**, **Pan**, **Reverse**; *Reset slice* puts them back |
| Notes for the loop | *Write the loop into the pattern*: the current pattern's notes of this channel are replaced by one note per slice |

The **Loop tempo** is the tempo the loop was played at: it is guessed when the window opens (the tempo nearest to the project's at
which the loop is 1, 2, 4, 8 or 16 bars) and can be typed. It sets where *Write the loop into the pattern* puts the notes and the
grid of *Slice by grid*. Every change is heard at once and is undone with Ctrl+Z (which closes the window). Everything is saved
with the project (the loop is the audio file's own: keep it, or use *Export project with samples*). At most 128 slices; slices
past key G10 are not played.

## Drum machine

A **drum channel** is one channel with 16 pads. Add one with **+ Add channel** > *Drum machine (16 pads)*, by dragging *Instruments > Drum machine* from the browser onto the rack,
or by right-clicking a channel > *Instrument* > *Drum machine (16 pads)*. A new drum channel starts with the Sun Loops Basic Kit (kick, snare and hat on pads 1 to 3).
**Double-click** the channel (or right-click > *Drum machine...*) to open its window; its volume, pan, pitch, mute / solo, mixer track and automation work like those of any channel.

**Pads.** The 4 x 4 grid has pad 1 at the bottom left. Pad *n* plays when the channel gets the MIDI note **36 + n** (pad 1 = C3, pad 16 = D#4): the same notes show up in the
Piano Roll, a MIDI keyboard plays the pads, and imported notes in that range play them. Click a pad to hear it and select it (Shift+click = soft, Ctrl+click = hard,
so you can hear the velocity layers). Right-click a pad for *Load a sound*, *Clear this pad*, *Mute* and *Solo*. Drag a sound from the browser onto a pad to put it on layer 1.

**The selected pad** (right side):

| Control | Meaning |
| --- | --- |
| Name, Mute, Solo | The pad's name (up to 24 characters, Return to accept), and silencing it / playing only the soloed pads (mute wins over solo). *Play pad* plays it. |
| Choke group | *None*, or group 1 to 8. When a pad starts, the pads of the same group that are still sounding are cut (a short fade): a closed hi-hat stops an open one. A pad in a group also cuts its own earlier hit. |
| Layers 1 to 4 | Each layer holds one sound (*Load*, or drop one from the browser on its row; *x* empties it) and the **lowest velocity it answers to** (1 to 127). A note plays the layer with the highest minimum that its velocity reaches; below every minimum the lowest layer plays. A new second, third and fourth layer starts at 64, 96 and 112. |
| Level, Pan, Tuning | Level 0 to 150 %, pan -100 to +100, tuning -24 to +24 semitones (tuning changes the length of the sound too). Double-click a slider to reset it. |
| Swing | 0 to 100 %: delays this pad's off-beat 16th steps (up to half a 16th). The channel's own Swing knob is hidden: a drum channel does not use it. |

**Steps.** The grid at the bottom has one row per pad and one step per 16th of the pattern (16 for the default bar). Click a step to switch it on or off (you hear the pad), drag to paint or erase a run, right-click to erase.
Shift+click puts a soft step (velocity 0.45), Ctrl+click an accented one (1.0), a plain click 0.8; the brightness of a step shows its velocity. A click on a row label selects (and plays) that pad.
The grid edits the channel's notes of the **current pattern** (undo and redo of notes work), shows notes made elsewhere (a note belongs to the step it starts in), and a line follows the playhead in Pattern mode.

**Built-in kits** (0.22.0). Every kit has its pads in the General MIDI drum order (pad 1 = key 36 = kick, 3 = snare, 7 / 9 / 11 = closed, pedal and open
hi-hat in choke group 1, 14 = crash), so a General MIDI drum part plays the right sounds, and up to four velocity layers per pad
(the layer limits are shown next to each layer: a harder note plays a louder and brighter recording):

| Kit | What it is |
|---|---|
| Acoustic Studio | Recorded kit (DrumGizmo MuldjordKit, CC BY 4.0): two kicks, snare and snare rest, four toms, hi-hats, two crashes, china, two rides and a ride bell |
| Acoustic Rock | Recorded kit (DrumGizmo DRSKit, CC BY 4.0): kick and kick with beater click, snare, rest and rimshot, three toms, closed / half-open / pedal / open hi-hat, two crashes, ride and ride bell |
| Electronic Boom | Made by the program in the manner of the classic analog drum machines: long round kick, snappy snare, hand clap, metallic hats, conga, cowbell, clave, shaker |
| Electronic Punch | Short clicky kick, noisy snare, six toms, crash and ride: the house and techno kind |
| Electronic Modern | Long distorted kick, layered snare and clap, crisp hats, a tuned "808" sub on pad 8 |
| Hand Percussion | Made by the program with modal synthesis: congas (open, muted, low), bongos, timbales, cowbell, agogo, wood blocks, claves, shaker, tambourine, triangle |
| Sun Loops Basic Kit | The three small synthesised sounds of the first versions |

New drum channels start with *Electronic Punch*. The recorded kits are mixed from all their microphones to stereo (close microphones
panned, overheads and room wide), so they sound like a whole kit in a room; how they were prepared is in `tools/sounds`.

**Kits.** *Load kit...* replaces all 16 pads (the steps stay): the built-in kits above, *From a folder...* (a folder with a `kit.json`, or simply a folder of audio
files: its first 16 files in name order fill the pads and name them), and every sub-folder of `~/Music/Sun Loops Studio/Drum Kits` under *My kits*. *Save kit...* asks for a name and writes a
folder of that name in *Drum Kits* with `kit.json` and copies of the sounds in `samples/` (sounds that are built into the program stay references); it asks before replacing a kit of the same
name. *Empty kit* clears all pads after asking.

**In a project.** The project file stores each drum channel's kit: the pads' names and settings and where every sound comes from (a file path, or a built-in sound), not the audio. When a project is opened
the sounds are read again; a file that is gone leaves a silent layer that keeps its path (and a message), so saving again does not lose the reference. Save the kit as a folder to make it portable.

Not available: undo of kit edits (the steps can be undone), an output or effects per pad (the whole channel goes to one mixer track), round-robin layers, editing a pad's sound in the sample editor (edit it
on a sampler channel, then load the saved file), more than 16 pads or 4 layers, sounds longer than 30 seconds, saving a drum channel as a channel preset (save the kit), mapping imported MIDI percussion to pads.
Projects that contain a drum machine cannot be opened by versions older than 0.14.

## Plugins

Sun Loops Studio hosts third-party **VST3**, **LV2** and **LADSPA** plugins, as instruments and as effects.

1. In the browser, open **Plugin** and click **Scan for plugins (VST3, LV2, LADSPA)...**. The scan runs in the background
   (the status line shows the file being read) and looks in the usual folders, `lib64` ones included (where Fedora and
   Nobara install them): VST3 in `~/.vst3`, `/usr/lib/vst3`, `/usr/lib64/vst3` and the same under `/usr/local`; LV2 in `~/.lv2`,
   `/usr/lib/lv2`, `/usr/lib64/lv2` and under `/usr/local`; LADSPA in `LADSPA_PATH` (if set), `/usr/lib/ladspa`, `/usr/lib64/ladspa`,
   under `/usr/local` and in `~/.ladspa`. The list is saved and shown again at the next start; scan again after installing new plugins.
2. **Instruments**: double-click one (a new channel plays it), drag it onto the empty space under the rack (a new
   channel) or onto a channel (that channel now plays it), or right-click a channel > *Instrument > Plugin instruments*.
   The channel gets its notes from the step grid, the piano roll, the playlist and a MIDI keyboard like any other instrument.
   Its window opens by itself; later, double-click the channel or choose *Plugin window...*.
3. **Effects**: in the mixer, click a slot > *Plugin effects* and choose one, or drag an effect from the browser onto a
   channel (it goes into the first free slot of that channel's mixer track). *Open plugin window* shows its controls;
   the power button bypasses it as for the built-in effects.

A plugin's settings (what you change in its window) are saved in the project. A project that uses a plugin which is not
installed on this computer still opens: the plugin is silent (an effect lets the sound through), its settings are kept,
and saving the project keeps them for the computer that has it. Exports use the same plugins; while an export runs, the
live sound is muted.

Plugins run inside Sun Loops Studio: **a plugin that crashes closes the program** (autosave and recovery keep your work).
A plugin that crashed the scan is skipped by the next scan. Cloning a plugin channel makes a second copy of the plugin with
the same settings.

### SunBurst, the built-in synthesizer plugin (0.48.0)

**SunBurst** is a polyphonic synthesizer for electronic sounds that comes with Sun Loops Studio. It is a real **VST3** plugin
(it also works in other programs that load VST3, and as a program of its own, *SunBurst* Standalone), installed next to Sun
Loops Studio and found by it at start-up without a scan. Add it with **Browser > Instruments > SunBurst (synthesizer)**
(double-click or drag), or with the Channel Rack's **+** > *SunBurst synthesizer*. Its window opens at once; reopen it like any
plugin's. Its sound and settings are saved with the project, and every one of its parameters can be automated.

Its window has three pages and a keyboard to try sounds with the mouse:

- **SYNTH**
  - **Oscillators 1-3** (the LED switches each on): *Sine, Triangle, Saw, Square* (with *Width*, the pulse width), *Noise* and
    **Wavetable** (14 tables: Analog Morph, Pulse Width, Harmonic Sweep, Formant, Digital Bells, Organ, Hard Sync, Growl, and since
    0.49.0 Brass, FM Bell, Wavefold, Vowel Bass, Chord Saw, Metallic; *WT POS*
    moves through them). *Octave, Semi, Fine, Level, Pan*. **Unison** stacks up to 16 voices: *Detune* spreads their pitch (up to
    a wide "fat saw"), *Blend* sets the outer voices against the centre one, *Spread* places them across the stereo field;
    *Random phase* starts each note at a random point (a fuller sound; off for clicky, punchy attacks). All tables are
    band-limited: no aliasing on high notes.
  - **Sub & noise**: a sub-oscillator one or two octaves below (sine or square) and a noise generator with a colour (white to dark).
  - **Filter**: low-pass, high-pass, band-pass (12 or 24 dB/octave), notch and peak, with *Cutoff, Reso* (resonance), *Drive*,
    *Env amt* (from envelope 2), *Key* (follows the note) and *Velocity*.
  - **Envelopes**: *AMP* (the volume, with *Velocity*), *FILTER* (envelope 2) and *MOD* (envelope 3, with *To pitch* for pitch
    sweeps, zaps and risers), each attack, decay, sustain, release.
  - **LFO 1-3**: sine, triangle, saw up and down, square, sample & hold, smooth random; *Rate* in Hz or synced to the tempo
    (*Sync*, *Division* from 4 bars to 1/32 with dotted and triplet values), *Fade in*, *Retrigger* (from the start with each note,
    or free running), *Unipolar*.
  - **Voice**: *Poly*, *Mono* or *Legato* (no new attack while a key is held), polyphony (up to 32), *Glide*, pitch-bend range.
- **MOD MATRIX**: 16 slots, each *source > destination* with an amount from -1 to +1. Sources: the three LFOs, envelopes 2 and 3,
  velocity, the mod wheel, aftertouch, pitch bend, key, a random value per note and the four **macros**. Destinations: each
  oscillator's pitch, wavetable position, pulse width, level, detune and pan; all pitch, sub and noise levels; cutoff, resonance,
  drive, amplitude, pan; the LFOs' rates; and the effects' distortion drive, filter cutoff, chorus, delay and reverb mix.
- **EFFECTS & ARP**: distortion (soft, hard, tube, fold, bit crush), a three-band EQ, a multimode filter, chorus, a stereo or
  ping-pong delay (in time with the song or in ms, with feedback and tone) and a reverb (size, damping, width, pre-delay, mix),
  in this order, each with its switch. The **arpeggiator** plays the held keys up, down, up and down, at random, as played or as
  chords, at 1/4 to 1/32 (with triplets) over 1 to 4 octaves, with a gate length.

The top bar holds the **preset browser** (click the name for the menu by category, **<** / **>** step through them), **Save**
(a name and a category; your presets go to `~/.config/SunBurst/Presets` and appear in the menu), the four **macros**, the
**master** volume and the output meter. **101 factory presets** (43 until 0.48) in eight categories, levelled to a similar loudness,: Lead, Pad, Strings, Bass (fat saw,
Reese, sub, acid, growl, wobble), Arp, Pluck, Keys and FX. In every factory preset the mod wheel opens the filter, Macro 1 =
brightness, Macro 2 = reverb, Macro 3 = unison width, Macro 4 = filter drive.

Tempo: inside Sun Loops Studio SunBurst follows the song's tempo (synced LFOs, delay and arpeggiator; 0.48.0 passes the tempo
and position to every plugin instrument). The window can be resized (drag its corner); it keeps its proportions.

### HexForge, the FM synthesizer plugin (0.50.0)

**HexForge** is a second synthesizer plugin that comes with Sun Loops Studio (VST3 and Standalone, found at start-up like
SunBurst): a **hybrid FM + subtractive** synth for sharp, aggressive, metallic sounds - cyberpunk, dark synth, industrial,
drum & bass, old-school dubstep and neurofunk. Add it with **Browser > Instruments > HexForge (FM synthesizer)** or the Channel
Rack's **+** > *HexForge FM synthesizer*. Its window has a dark steel look with red and cyan lights, and four pages:

- **OPERATORS**: six **operators**, each with a **wave** - 32 classic band-limited waves (sine, triangle, saw, ramp, square,
  pulses, half / full / quarter sine, camel, organ, clarinet, bright and soft saws, trapezoid, staircase, exponential,
  parabola, folded sine, sync, vowel, bell, metal, digital...) or one of the two **spectral waves** - a **ratio** to the note
  (1/4 to 16), *Fine*, *Level* (how strong it is, also as a modulator), *Out* (how much of it is heard: 0 = a pure modulator),
  *Pan*, *Velocity* and its own **envelope** (attack, decay, sustain, release). The **FM matrix** next to them is one 6 x 6
  grid of knobs: the knob in row *R*, column *C* is how much operator *C* modulates operator *R*; the diagonal (lit) is each
  operator's **feedback**. No algorithms to pick: turn the knobs you want. **Engine**: *FM depth* (all the matrix at once),
  **unison** (1-8 voices, *Detune*, *Spread*), *Octave* and a **pitch drop** (a fast fall at the start of each note, for
  zaps and punchy basses).
- **FILTER & AMP**: the **ladder filter**, modelled on Moog's four-pole transistor ladder (its resonance rings and
  self-oscillates, a hot signal saturates): *Ladder LP 24 / LP 12 / HP 24 / HP 12 / BP*, *Cutoff, Resonance, Drive*, the
  amount of its **own envelope** (*Filter envelope* panel, ADSR), *Key track*, *Velocity*; the **Filter LFO** (amount, shape,
  rate or tempo division) sweeps the cutoff for wobbles. *Amplifier* (ADSR, velocity), *LFO 2* for the modulation page, and
  *Voice* (poly / mono / legato, polyphony up to 16, glide, bend range).
- **SPECTRAL & MOD**: draw **Spectral A** and **Spectral B** as 64 harmonics with the mouse, start from *Saw, Square, Odd, Bright,
  Formant, Metal, Random* or *Clear*, or **Load WAV...** a single-cycle wave (its first 64 harmonics are read). They are saved
  with the sound. **Modulation**: 8 slots, *source > destination* with an amount (filter LFO, LFO 2, filter envelope, velocity,
  mod wheel, aftertouch, key, random, macros > operator levels, FM depth, pitch, cutoff, resonance, drive, unison detune, pan,
  amp, distortion, LFO 2 rate).
- **EFFECTS**: distortion (on by default, tube), EQ, chorus, tempo delay, reverb.

The voices run at twice the sample rate and are filtered back down, so hard FM and the ladder stay clean. **40 factory
presets** in eight categories - Neuro, Reese, Dubstep, DnB, Cyberpunk, Dark Synth, Industrial, FX & Metal - with levelled
volumes; in each the mod wheel adds FM, Macro 1 = cutoff, Macro 2 = FM depth, Macro 3 = distortion, Macro 4 = unison width.
Your presets go to `~/.config/HexForge/Presets`.

## Piano Roll

Edits the notes of the selected channel in the current pattern. The title shows the channel ("Piano Roll - Lead").
It is closed at start and opens from a channel, as above.

| Action | How |
| --- | --- |
| Play a note | Left button on a key of the keyboard (held = sounds). |
| Add a note | Click an empty cell (press and release without moving: since 0.44.0 a drag on empty cells selects instead). The key sounds while you hold. Drag the new note's right edge to change its length. |
| Move a note | Drag it (Alt while dragging: no snap). Dragging a **selected** note moves every selected note. |
| Change its length | Drag its right edge, or its **left edge** to move its start keeping its end (0.32.0); over either edge the cursor becomes a left-right arrow. On a selected note every selected note changes by the same amount. |
| Reuse a length | Click a note: its length becomes the length of the next notes you draw (0.32.0, as in FL Studio). |
| Delete a note | Right-click it (a selected note: the whole selection). |
| Select notes | **Drag on an empty part** (0.44.0, as on the desktop) draws a selection rectangle; on release every note it touches or holds is selected (with Shift it adds to the selection; **Ctrl + drag** does the same); **Ctrl + click** a note adds or removes it; **Ctrl + A** selects all, **Escape** none. Selected notes are bright with a white border. |
| Copy, cut, paste, duplicate | **Ctrl + C / X / V** with the mouse over the piano roll (paste puts the first note under the mouse; the copied notes can be pasted in the same piano roll or in another channel's or pattern's - select the channel or pattern, then Ctrl + V), **Ctrl + D** (a copy right after the selection). **Delete** removes the selection. |
| Move with the keys | **Up / Down**: a semitone (Shift: an octave, Ctrl: a step of the scale); **Left / Right**: a snap step (Shift: a beat). |
| Legato / slide | Shift + click a note: normal -> **legato** (blue, "L") -> **slide** (amber, "S") -> normal. |
| Scroll pitch | Mouse wheel. |
| Scroll time | Shift + wheel. |
| Zoom time | Ctrl + wheel. Until you zoom, the whole pattern fits the panel width. |
| Move the pattern position | Left click (or drag) on the **ruler**, also while playing (snapped to 16ths; Alt: no snap). Stopped, the orange marker shows where Play starts. The playback switches to Pattern mode. |
| Loop part of the pattern | **Right drag on the ruler** draws a loop region (blue, "LOOP"): Pattern mode then repeats only that part. Stopped, Play starts at the region. |
| Move / resize the region | Right drag inside it moves it; right drag on one of its edges resizes it. |
| Region menu | **Right click on the region**: **Move** (it follows the mouse; click to place it), **Resize** (the nearest edge follows the mouse; click to finish), **Delete loop region** (removes only the highlight: no note is touched). During Move and Resize a right click or Escape puts it back. |

**The toolbar** (0.23.0), above the notes:

- **Snap**: Bar, 1/2, 1/4 (a beat), 1/8, 1/16 (the default), 1/32, 1/64, 1/8 and 1/16 triplets, None. Notes are drawn, moved and resized on it.
- **Scale**: a key and a scale (major, the minors, the modes, the pentatonics, blues). The rows of the notes outside the scale are
  darker and the root is named in green on the keyboard. **Snap to scale** makes new and moved notes land on the scale.
- **Chord**: with a chord chosen (major, minor, sevenths, ninths, sus, 6, power ...) and its inversion, one click places the whole
  chord, its root on the clicked key; drag to set the length of all its notes.
- **Velocity**: shows the **velocity lane** under the notes: a stalk per note. Drag across it to draw the velocities (only of the
  selected notes, when some are selected). Its top edge (with five small dots in the middle) can be dragged up or down to make the
  lane taller or shorter - the pointer becomes an up-down arrow over it; a double click there puts the starting height back. The
  height is remembered (0.54.0).
- **Tools** works on the selected notes (or all when none is selected): Quantize (starts; starts and ends; half way), Humanize (light /
  medium / strong), Legato, Staccato, Strum chords (up / down, fast / slow), Arpeggiate chords (up, down, up and down, random; 8ths
  or 16ths), Chop (8ths, 16ths, 32nds), Flip, Transpose an octave, Snap to the scale, Velocity (crescendo, decrescendo, random, fixed).
- **Generate** writes into this channel's pattern (replacing its notes; **Ctrl+Z** undoes it), in the key chosen under Scale:
  a **chord progression** (twelve, from pop I-V-vi-IV to jazz ii-V-I and minor ones; held, syncopated stabs, eighth-note pulse or
  arpeggio; one or two chords a bar, voiced so that each voice moves as little as possible), or a **bassline** (roots, eighths,
  octaves, root and fifth, walking, syncopated) either on one of those progressions or **following the chords of another channel**:
  the program finds the chord played in every half bar (the lowest note decides between two that fit equally).

**Playing the notes.** The keyboard on the left and the notes you draw are playable:

- Press the left button on a **key** and the note sounds for as long as you hold the button; the key lights up.
  Keep the button down and move up or down to play the keys in turn (a glissando).
- When you **create** a note (click on an empty cell), **move** it or **resize** it, it sounds while the button is down.
  Moving it to another row changes the note you hear. Right-click does not play anything.
- The sound is that of the selected channel: its instrument or sample, volume, pan, **Pitch**, mixer track and effects. It
  works whether the transport is playing or stopped. On a sampler channel each press plays the sample once, at the
  pitch of the key.
- If the sound seems missing, check the channel's **M** button, the track's **M** and the master volume.

**Legato and slide.** Two notes of the same channel can be joined: with **legato** the sound that is playing goes on and only its pitch
jumps to the new note (no new attack, as on a monosynth); with **slide** the pitch glides to the new note in at most an eighth note
(or the note's length, if shorter). The line drawn from the old note to the new one shows the join. For it to work the note before must
still be sounding when the linked note starts, so the two should touch or overlap (turning legato on lengthens a note that stops up to
a 16th early; a note that stops earlier leaves a gap and the linked note then just plays normally, with its own attack). It works on synth
and sampler channels and is saved in the project.

Notes snap to 16th notes. A pattern is one bar (4 quarters) long unless you chose another length (see *Patterns*). A synth note ends at its length or at the end of the pattern,
whichever comes first. When you open a channel the view scrolls to its notes.

## Patterns

A **pattern** holds the notes of every channel. The pattern box in the toolbar selects the current pattern: the
Channel Rack and the Piano Roll show and edit that pattern. The **+** button next to it offers **New empty pattern**,
**Clone current pattern** and **Pattern length (all patterns)**: 1 (the default), 2, 4 or 8 bars; the **-** and **+** buttons of the
Channel Rack change it by **one quarter** at a time (4 to 64 quarters). The length applies to every pattern; the step
sequencer, the piano roll and the drum machine follow. Pattern clips stay where they are (0.42.0): a clip that was a whole number of
patterns long keeps that number of repeats, any other keeps its length. Notes past the end of a shorter pattern are kept but not played, and come back if you make the patterns longer again. The change
is refused if the playlist would no longer fit in the timeline. New patterns get the next name automatically (Pattern 3, Pattern 4, ...). The demo song (0.52.0)
has 12 patterns of four bars (Piano, Beat, Build, Bass, Marimba, Lead, Pad, Growl, Bells, Choir, Arp, Outro), each on its own track.

**Loop Starter (0.30.0).** *Loop Starter...* in the **+** menu makes a starting loop for a track. Choose the **genre** (house, techno,
hip-hop, trap, drum & bass, pop, lo-fi, reggaeton, rock or synthwave, each with its tempo), the **key**, the **mode** (minor, major or
the genre's usual one), the **parts** (drums, bass, chords and melody, or fewer), whether to use the genre's **tempo**, and a
**variation** (a number). *Make it* adds a new pattern named after the genre and the variation, with the parts on channels named
"House Drums", "House Bass" and so on, each on its own mixer track with a sound that suits it: a built-in drum kit, the grand piano
or electric bass of the sound library, or a factory synth patch. The program switches to Pattern mode, so **Play** plays the loop.

Every genre keeps its backbone (four-on-the-floor kick for house, techno and reggaeton; the snare on 2 and 4 for hip-hop, drum &
bass, pop, rock and synthwave; trap's half-time clap with hi-hat rolls; reggaeton's dembow) and varies the rest: kick patterns,
ghost notes, hats, fills, the chord progression, the bassline's style and the melody (a motif answered every fourth bar, on chord
notes at every bar's start, in the key). The same genre, key and variation always give the same loop, so a loop you liked can be made
again; the window remembers your choices and offers the next variation. Making another loop in the same genre uses its channels
again (with the new loop's sounds) instead of adding more. Everything is made by rules and chance, without a trained model and
without the internet. Undo removes the loop.

The **pattern box** at the top of the Playlist has three buttons: **New** (empty or cloned, like the **+**), **Rename**
(a name of up to 40 characters) and **Delete**. Delete removes the selected pattern, its notes and **every playlist clip
that plays it**; the last remaining pattern cannot be deleted. Choosing a pattern in that box also opens it in the Rack and
the Piano Roll.

Channel settings (volume, pan, mute, solo, pitch, swing, instrument, sample, mixer track) belong to the channel, not
to the pattern: they are the same in every pattern. Mixer tracks and their effects belong to neither.

## Undo and redo

**Ctrl+Z** undoes and **Ctrl+Y** (or Ctrl+Shift+Z) redoes the last edit, **whatever it was**: notes (Piano Roll, step sequencer, *Clear notes*,
a take of **Rec notes**), channel settings (volume, pan, pitch, swing, mute/solo, instrument, sample, name, mixer track), channels added,
cloned, moved or deleted, drum-kit edits, patterns added, renamed or deleted, the pattern length, the playlist, the mixer (faders, balance,
effects and their settings, sends, VCA groups), tempo and master volume. The File menu has the two commands too. Everything changed while one
mouse button stays down (a knob turned, a note dragged, steps painted) is one step, recorded when you let go of the button; the last
100 steps are kept. Opening or creating a project
starts a new history.

With the mouse over the Playlist the same keys use the Playlist's own, finer history of clip edits (its **Undo** / **Redo** buttons do the same).

## FL Studio projects (.flp)

Projects are saved in the program's own format, **.slp**, as before. Since 0.34.0 the **File** menu can also read and write FL
Studio's **.flp** files, with code written for this program (no FL Studio code or library is used):

- **Open an FL Studio project (.flp)...** opens one as a **new, untitled project**: the .flp is never changed, and **Save** writes a
  .slp. Carried over: the tempo, the channels (names, sample files and their root key), every pattern's notes (pitch, position,
  length, velocity), the first arrangement's playlist (pattern clips and audio clips on their tracks) and the tracks' names and
  colours. FL Studio's own synths and plugins have no equivalent here: a default synth plays their notes. A sample whose stored
  path does not exist (a Windows path, FL Studio's own folders) is looked for by its name next to the .flp and in the folders beside
  it. Patterns get one length here (1, 2, 4 or 8 bars: the shortest that holds the longest), the FL Studio playlist tracks that hold something
  go onto as many tracks here (at least 4, at most 128) in their order, and clips keep their position and length.
- **Export as FL Studio project (.flp)...** writes the current project in FL Studio 20.8's layout at 960 PPQ (exact: twice this
  program's 480). Sampler channels keep their sample file; synths, drum machines, plugins and built-in sounds become empty Sampler
  channels that keep their notes; a pattern clip repeated over several pattern lengths becomes one clip per repeat; audio clips play
  their file from its start. A clip that starts inside its pattern (after a split) is written from the pattern's start, and the
  message says so.
- Neither direction carries effects, the mixer, automation or plugin settings; a message after each lists what was left out.
- **Checked**: the files written here were read back by this program and by an independent reader (PyFLP), and a real FL Studio
  20.8 project from PyFLP's tests was opened. **Not checked**: opening the exported files in FL Studio itself, which is not
  available here.

## Importing a MIDI file

*File > Import a MIDI file into patterns...* (or drop a `.mid` / `.midi` file on the Playlist) turns a Standard MIDI File into **new synth
channels** (one per track and MIDI channel that has notes, named after the track) and **new patterns** of the project's pattern length, placed one after the
other after the existing clips, each on its own playlist track (0.44.0; before, all on track 1). Long notes that cross the end of a pattern are cut there and sound again at the start of
the next pattern. The first tempo of the file sets the BPM. The status line reports what was done and what was left out.

Left out: General MIDI **drum** tracks (MIDI channel 10), tempo changes after the first, controllers, pitch bend, program changes; tracks that
do not fit in the free channels (there are 32 in all); files over 8 MB or with more than 200 000 notes; files timed in SMPTE frames. The
imported channels are plain synths: give them a sampler or another sound afterwards.

## Playlist

The Playlist arranges patterns in time. A **pattern clip** (0.49.0) is a see-through block in the pattern's colour with a small
piano roll of the pattern's notes inside, repeated as often as the clip repeats the pattern, so you can tell patterns apart
at a glance; on tall tracks the name sits in a title bar above the notes, on short ones over them. Its **tracks** (0.44.0):

- A new project starts with **4 free tracks** (*Track 1-4*, for recordings, audio files or anything) and, below them, **one track
  for each pattern**, named and coloured after it. Every pattern made afterwards (*New*, *Clone*, the Loop Starter, a MIDI import)
  adds its own track at the end and the playlist scrolls to it: with 4 patterns there are 4 pattern tracks + the 4 free ones = 8.
- A click on an empty place of a pattern's track puts **that pattern** there; on a free track it puts the pattern chosen in the
  pattern box. Clips can still be moved to any track.
- The **-** and **+** buttons in the top-left corner, above the track names, remove the **last** track and add a free track at
  the end. To remove a particular track, right-click its name > *Delete this track* (the menu also has *Add a track at the
  end*). A track that holds clips is removed only after a question, and its clips go with it; **Ctrl+Z** brings everything back.
  At least one track stays; at most 128.
- Deleting a pattern removes its track too when nothing else is on it; a track that still holds clips (a recording, say) stays,
  as a free track with the pattern's old name.
- Up to 8 tracks share the height of the panel; with more, they scroll: the scroll bar on the right, the **mouse wheel over the
  track names**, or **Alt + wheel** anywhere.
- Projects saved before 0.44.0 open with their 8 tracks as they were.

The timeline is at least 32 pattern lengths and at least 64 bars long. The ruler shows bar
numbers. The orange vertical line marks the end of the song: in Song mode playback loops there (or at the loop region, below).

### The ruler: song position and loop region

- **Left click on the ruler** moves the song position (the orange playhead) there, **also while the song plays**: playback carries on from the
  new place at once. Drag along the ruler to scrub. The click snaps to the grid (**Alt**: no snap) and switches to Song mode. **Stop** goes back
  to where you last clicked, so Play starts there again. An audio clip that is already under the playhead starts from that point in its sound.
- **Right click and drag on the ruler** draws the **loop region**, a blue bar marked *LOOP* (also shaded over the tracks). In Song mode the song
  repeats it: when the playhead reaches its end it goes back to its start; if you start after it, the end of the song goes back to it. With the
  song stopped, the playhead moves to the start of a new region.
- **Right click on the loop region** opens its menu: **Move** (the region follows the mouse; click to place it), **Resize** (the nearest edge
  follows the mouse; click to finish) and **Delete loop region**. *Delete* removes only the highlight: no clip, pattern or
  note is touched. During *Move* and *Resize* a right click or Escape puts the region back as it was.
- **Right drag on the region** moves it at once; right drag on one of its **edges** resizes it.
- The region snaps to the grid of the Grid box (at least a 16th) and is saved in the project. It only applies to live playback in Song mode:
  exports always render the whole song.

| Action | How |
| --- | --- |
| Place a clip (Select tool) | Click anywhere on a track. The clip starts on the **grid square** you clicked (the Grid box: a bar, a beat, a 16th...; with **Alt**, exactly where you clicked), holds the pattern selected in the pattern box and is one pattern long. Drag right straight away to make it longer. |
| Move a clip | Drag it: it moves by **grid squares** (0.42.0), not by whole patterns, so clips can start out of step with each other (a beat or a 16th later); hold **Alt** for no snap at all (micro-timing). It can change track. |
| Repeat / shorten | Drag the right edge of the clip, by grid squares (Alt: freely): the pattern repeats as long as the clip lasts; a length that is not a whole number of patterns stops in the middle of the last repeat (thin marks show where each repeat starts). |
| Delete | Right-click the clip > Delete, or select it and press Delete (or use the Erase tool). |
| Select its pattern | Double-click the clip. |
| Zoom in / out | Mouse wheel (up: in, down: out), around the mouse, anywhere on the playlist. |
| Scroll time | Shift + wheel, a sideways wheel or touchpad swipe, or the scroll bar. |

### Tools, grid, zoom and undo

The toolbar under the pattern box has five tools (keys **1** - **5** while the mouse is over the timeline):

| Tool | What it does |
| --- | --- |
| **Select** (1) | The normal tool described above and below: click a clip to select it, drag to move it, drag its **left or right edge** to trim or lengthen it (an audio clip keeps its sound in place: only more or less of it is heard), right-click for a menu (*Split here, Duplicate, Copy, Delete*, plus *Position, gain, pan and fades...*, *Reverse* and *Stretch and pitch...* on an audio clip or *Open this pattern* on a pattern clip), double-click a pattern clip to open that pattern. |
| **Split** (2) | Click a clip to cut it in two at that point (snapped to the grid). Key **S** splits the selected clip at the mouse. |
| **Cut** (3) | Drag across a clip to remove that stretch of it: the sound after the gap stays where it was. |
| **Erase** (4) | Click a clip to delete it, or drag across several. |
| **Zoom** (5) | Click zooms in, right-click or Shift+click zooms out, drag a stretch to zoom to it. |

**Grid** (the box next to the tools) is what audio clips snap to when moved, trimmed, split or cut: a bar, half a bar (1/2,
0.32.0), a beat and so on down to a single tick (1 beat = 480 ticks); hold **Alt** while dragging to ignore it. Since 0.42.0
pattern clips snap to the same grid (before, they sat on whole patterns).
**-**, **+** and **Fit** zoom out, in and to the whole song; the **mouse wheel** zooms in and out around the mouse, and
Shift+wheel and the scroll bar move in time. The ruler shows **minutes:seconds.milliseconds** and the bar numbers (only as many as fit at the
current zoom), and the two lines under the timeline read out, for the position under the mouse, *time - bar:beat:tick*, and
for the selected clip its name, start, length and end (each as bar:beat:tick and as a time), for an audio clip also the length and
sample rate of the file, where in the file the clip starts (in seconds and in samples) and its gain. On an audio clip, *Position,
gain, pan and fades...* (right-click) lets you type exact values.

**Per-clip settings (0.27.0).** Each audio clip has its own **pan** (-100 left to 100 right: a balance like a mixer track's, the
centre leaves the sound alone), **fade in** and **fade out** (seconds, sine curves, drawn on the clip and shaping its waveform)
and **direction**: *Reverse* in the right-click menu (or *Direction* in the dialog) plays the same part of the audio backwards,
and the waveform turns round with it. Together with *Stretch and pitch...* (below) that gives every audio clip its own pitch,
length, direction, pan and fades. Splitting or cutting a clip keeps the fades at its outer edges (the left piece keeps the
fade-in, the right one the fade-out). Fades longer than the clip share it in proportion. A reversed clip cannot be opened in the
sample editor: switch *Reverse* off first. These settings are saved with the project; older projects open with none.

**Undo / Redo** (buttons, **Ctrl+Z** and **Ctrl+Y**) cover everything done to clips in the Playlist. **Ctrl+C / X / V** copy,
cut and paste the selected clip at the mouse and **Ctrl+D** duplicates it; **Delete** removes it. The undo history is
forgotten when something outside the Playlist changes the arrangement (opening a project, a new recording, deleting a pattern).
Split and Cut also work on pattern clips, at any grid square (0.42.0): the right-hand part goes on from the same place in the
pattern (it remembers how far into the pattern it starts), so a split clip sounds exactly as before. A clip made by splitting an audio clip remembers where it
starts in the sound **in seconds, at the BPM it was cut at**: if you change the BPM afterwards the sound still plays at its
original speed, so the cut points no longer fall on the beat they were cut on.

**Clips may lie over each other on a track** (0.33.0, as in FL Studio): pattern clips, recordings and audio files can be layered
freely and they all play; the one added or moved last is drawn on top and is the one a click picks (every clip has an outline,
so the edges of the ones underneath stay visible). Clips on different tracks play together too.

**Track names** (0.33.0): clicking a track's **name** chooses the track that **Record** writes to (marked with a red dot);
**double-click** it to rename the track, **right-click** it for *Rename...*, **Colour** (twelve colours, or none: the colour marks
the name and tints the track) and *Default name and colour*. Names and colours are saved with the project and undone with Ctrl+Z.

### Audio clips

Audio clips are recordings and audio files placed on the playlist. They are drawn in teal with their waveform.

| Action | How |
| --- | --- |
| Add an audio or video file | Drag it from your file manager onto the Playlist (it lands on the track and beat under the mouse, which is highlighted), or use *File > Import audio or video into the playlist...*. Several files go on consecutive tracks. |
| Move | Drag the clip (it snaps to the grid chosen in **Grid** and can change track). |
| Shorten / lengthen | Drag its right or left edge: at most the length of the audio. |
| Delete | Right-click it > Delete, or select it and press Delete. |

Supported files: `.wav`, `.aiff` / `.aif`, `.flac`, `.ogg`, `.oga` and `.mp3` are read by the program itself. `.aac` /
`.m4a` and the **sound track of a video** (`.mp4`, `.m4v`, `.mkv`, `.mov`, `.avi`, `.webm`, `.wmv`, `.flv`, `.mpg`, `.mpeg`,
`.ogv`, `.3gp`) need the **ffmpeg** program: Nobara normally has it; otherwise install it with `sudo dnf install ffmpeg` (on stock
Fedora that is the RPM Fusion package; the `ffmpeg-free` package may lack the MP3 encoder). The sound of a video is extracted into
`~/Music/Sun Loops Studio/Samples/From videos/<name>.wav` (never replacing a file) and that WAV is what the clip uses.
Decoding runs in the background; the status line says when the clips are in. A file longer than 5 minutes is cut at 5
minutes (with a message), and a clip longer than the free space is cut to fit.

An audio clip plays in **Song mode** (the program switches to it when you add the first clip) through the mixer track
with the same number as its playlist track: **Track 3** of the playlist plays through **mixer track 3**, so its fader,
mute, solo and effects apply, and it is part of every export. A clip lasts as many beats as the audio lasts at the
tempo it was added at; if you change the BPM later the sound still plays at its original speed, so the clip may end
before or after the audio does (the waveform shows where the sound really ends). If the file is later moved or deleted,
the project opens with a silent clip marked *(file not found)* that keeps the path.

### Stretch and pitch

Right-click an audio clip > **Stretch and pitch...** to change its length or its pitch, or both, **independently**:

| Field | Meaning |
| --- | --- |
| Length (%) | 100 = unchanged, 200 = twice as long (half the tempo), 50 = half as long. From 25 to 400. The pitch does not change. |
| Pitch (semitones) | -24 to +24; decimals work (0.5 = 50 cents). The length does not change. |
| Tempo of the audio (BPM) | Optional. If you know the tempo a loop was recorded at, type it: the length is set to *its tempo / project tempo*, so a 90 BPM loop in a 120 BPM project becomes 75 % as long. This overrides the Length field. |
| Pitch mode | *Normal*, or *Keep the timbre*, which limits how far the spectrum above 8 kHz is moved (a little better on voices and cymbals when shifting by several semitones). |

**Apply** renders the audio again, once (a few seconds for a long file) and the clip becomes longer or shorter by the same factor;
**Back to the original** returns to the file's own sound and length; the playlist's **Undo** works too. The file on disk is never changed.
A clip with a change shows it in its name: `[x1.50, +3 st]`. The result is always made from the original audio, so stretching twice by 150 % is
150 %, not 225 %. If the longer clip would pass the end of the playlist, nothing changes and you are told.

The project file stores only the numbers; the audio is processed again when the project is opened (clips with the same file and the same
numbers share one rendering). Splitting a stretched clip leaves both halves stretched.

How good is it? The method (Signalsmith Stretch) works best between 75 % and 150 % and for pitch shifts of a few semitones; at the
extremes (4x, an octave or more) sounds get smeared and a little metallic, especially drums and percussive
notes, because there is no separate treatment of transients. It was measured on test tones (the length, the pitch and the timing are
exact within a few milliseconds; a sharp attack is smeared by about 10 to 25 ms) and **not judged by ear on real music**.

### Sample editor

Right-click an **audio clip** (playlist) or a **sampler channel** (Channel Rack) > **Edit sample...** opens the editor. For a clip it shows exactly the part
of the audio that the clip plays. It works on a copy: nothing changes outside until you press the button at the bottom right.

- **Waveform.** Drag to select; Shift+click extends the selection; the wheel zooms, Shift+wheel scrolls, Home shows everything. **Select all** (Ctrl+A).
- **Preview.** *Play* (Space) plays the selection, or from the cursor to the end when nothing is selected; *Loop* repeats the selection. The sound goes through the audio
  engine (after the master chain, so it does not depend on the mixer settings). The **spectrum** under the waveform follows what is playing (4096-point FFT,
  logarithmic frequency axis).
- **Edits** apply to the selection (or to everything when nothing is selected) and each one can be undone with *Undo* (Ctrl+Z) and brought back with *Redo*
  (Ctrl+Shift+Z or Ctrl+Y): **Cut** (Delete; removes the selection and closes the gap), **Crop** (keeps only the selection), **Silence**, **Reverse**,
  **Invert** (phase), **Gain...** (dB), **Normalise...** (loudest point to a level, default -0.3 dB), **Remove DC** (centres the waveform on zero) and
  **Fade in / Fade out** with a shape: *Linear*, *Curve* or *S curve*. Cut and Crop need a selection and, because they change the length, clear the selection and the slice markers.
- **Reduce noise...** First select a stretch that holds **only the noise** (at least about 0.08 s at 48 kHz, for example the silence before a recording starts). The
  program measures the noise in every frequency band and lowers everything in the **whole sample** that does not rise clearly above it. *Reduction* (0 to
  60 dB, default 12) is how much the noise is lowered; *Sensitivity* (0 to 8, default 3) is how far above the noise a sound must be to be kept. Too
  much of either makes the sound "watery"; try it, listen, and use Undo.
- **Slicing**. *Find hits* puts a marker at the start of every hit, note or word it finds; the **Sensitivity slider** next to it
  (low = only the clearest hits) decides how many. *Equal parts...* puts 2 to 128 equally spaced markers. Double-click on the waveform adds a marker or
  removes the one near the click; *Clear markers* removes all. When you press **Save as new file and use** the clip is cut at the markers into separate clips of the
  playlist (one Undo step), which you can then move, repeat or delete. **Slices to sampler channel** turns them into a new sampler channel with one note
  per slice instead (see *Sampler and SoundFonts*); it also works on a sampler channel's sample.
- The button at the bottom right (**Save as new file and use**) writes the result as a **new 32-bit float WAV file in
  `Samples/Edited`** next to your other samples (a number is added if the name exists: an earlier edit is never replaced) and puts it on the clip or in the
  channel. The original file is never touched, and the project file just points to the new one. **Cancel** (or closing the window) throws the edit away.

Things to know: a clip that is **stretched or pitch-shifted** must first be taken back with *Stretch and pitch... > Back to the original*, edited, and
stretched again. After saving, a clip plays the whole edited audio from its start, as far as there is room on its track (it is shortened if a
neighbour is in the way, and the tempo is the current one). Only one editor can be open at a time. Undo keeps the last 40 steps (fewer for very long samples). A sampler channel's sample cannot be cut into clips (there are no clips), but its slices can become a new sampler channel.

Noise reduction and hit detection were measured on test signals (noise lowered by about 18 dB with a tone kept within 1.5 dB; five test hits found to
within 0.2 ms) and **not judged by ear on real recordings**: spectral gating always trades noise for some loss of the quiet detail of the sound.

### Pitch editor (0.38.0)

Right-click an audio clip > **Edit pitch...** shows a recording of **one voice or instrument at a time** (a singer, a bass, a lead
line) as **notes over its pitch curve**, as FL Studio's Newtone: each note at the pitch it was sung, labelled with the nearest key
and how many cents off it is (names as in the piano roll: C5 = MIDI 60).

| Action | How |
| --- | --- |
| Move a note | Drag it up or down: by semitones; hold **Alt** to move it freely. The thin line shows where it was sung |
| Tune a note | Double-click it: it goes to the key it is nearest to |
| Tune several | Select them (click, **Shift+click** to add) - or none for all - and press **Correct**: **Amount** 100 % brings them to their keys, 50 % half way; with a root and a scale they go to the nearest key **in the scale** |
| Undo the changes | **Reset** (the selected notes, or all) |
| Listen | **Play** plays the edited audio |
| Keep it | **Save as new file and use**: a new WAV in `Samples/Edited`, which the clip then plays (the original file is never touched; Ctrl+Z in the playlist brings the old audio back) |
| Pitch to MIDI | **Notes to pattern**: the notes become the **selected channel's** notes in the current pattern (its notes there are replaced; Ctrl+Z undoes), each on its nearest key, from the start of the pattern, the louder ones with a higher velocity |

How it works: the pitch is measured every 5 ms (YIN), notes are the stretches where it holds steady, and each moved note is
pitch-shifted on its own (same length, 10 ms crossfades at its edges); the audio between notes is not changed. What it cannot do:
chords or several voices at once, very noisy recordings, flattening vibrato or drifts inside a note (a note moves as a whole),
or changing a note's length. A stretched, pitched or reversed clip must be put back first (as for the sample editor).

### Stems: drums, bass and the rest (0.39.0)

Right-click an audio clip > **Split into stems...** splits a mix into three parts, **without AI** and on your computer:

- **Drums**: what is short and wide in frequency (hits);
- **Bass**: what is steady and low (below about 150 Hz);
- **Other**: what is steady and higher (voices, chords, leads).

A progress window shows how far it is (a few seconds per minute of audio; *Cancel* stops it). The three parts are saved as WAV
files in `Samples/Stems` and replace the clip: Drums on the clip's own track, Bass and Other on the next two (above it at the bottom
of the playlist), all at the same place. **They add up exactly to the original**, so playing the three sounds like the clip did;
**Ctrl+Z** brings the original clip back.

How it works: harmonic-percussive separation by median filtering of the spectrum (Fitzgerald, 2010), then a crossover for the
steady part. What it cannot do: tell a voice from a guitar (both go to Other), or take the attack of a plucked bass out of Drums.
It works best on clear electronic or acoustic mixes; a neural network does better, but it is not what this program uses.

### Recording

**Record** (toolbar) records the **audio input**: a microphone, a guitar into an audio interface, a line input - on one track or,
since 0.36.0, on **several tracks at once**, each from its own input.

1. In **Audio...** choose the input device; speak or play and watch the **Input level** bar. (Pressing Record switches on the
   inputs the armed tracks need, if they are off.)
2. **Arm** the tracks to record: every playlist track has a **record dot** after its name. Click the dot (or Ctrl+click the
   name) to arm or disarm the track: armed tracks have a red dot. One track always stays armed. With a single track armed, a
   click on another track's name moves the arm there; with several armed, it only selects the track.
3. Choose **each track's input**: right-click the track's name > *Recording input...* (or the arrow next to Record > *Input of
   each track*): one input (a one-channel file: a microphone, a guitar) or two neighbouring inputs (a stereo file). The names
   come from the audio device once an input is open; before that, inputs 1-8 are offered by number. A new track records
   input 1.
4. Press **Record**. The song plays in Song mode and the status line shows the time, the loudest input level and the tracks
   that clip. Press **Record** again or **Stop** to finish; each take appears as an audio clip on its track and is also kept
   as a 24-bit WAV in `~/Music/Sun Loops Studio/Samples/Recordings/` (with *T1*, *T2*... in the name when several tracks
   were recorded).

The arrow next to Record also has:

- **The recording becomes**: clips on the armed tracks, or *New sampler channels* (one per armed track, to play from the piano
  roll).
- **The takes start**: *At the start of the song* (the song is rewound), or *Where the song is (punch in)*: from the playlist's
  start marker, or from where the song is playing if it already is. The takes are placed where they started.
- **Count-in** (0.47.0): *None*, *1 bar* or *2 bars* of metronome (in the time signature, at the starting tempo) before the song
  and the takes start; the metronome need not be on. The status line says *Count-in...*; Record or Stop cancels it.
- **With a loop region in Song mode** (0.47.0):
  - *Keep recording: every pass is a take* (the default): the song goes round the region and the recording goes on. At the end
    the clip covers the region and plays the **last whole pass**; right-click it > **Take** chooses another (*Take 1*, *Take 2*...).
    A pass counts when the whole region was recorded in it (the first one only if the take started at or before the region).
  - *Stop at the end of the region (punch out)*: the take stops when the song jumps back, and its clip ends exactly at the end of
    the region.
- *Hear the input while recording* (use headphones: with speakers and a microphone it feeds back; this monitoring adds the
  device's latency, and it plays inputs 1 and 2 only).

All the takes start in the same audio block, so they line up with each other, and they are shifted by the round-trip latency
that the audio device reports so that they line up with what was playing. A take stops by itself at the end of the playlist
(with loop takes: after 5 minutes), or at the end of the loop region with *punch out*. An input the device does not have is
reported and nothing is recorded.

### MIDI keyboards and other devices

**Audio...** also lists the **MIDI inputs**. Tick the keyboards you want: they play the **selected channel** of the
Channel Rack (click a channel to select it) with the velocity of the keys, whether the song is playing or not. A keyboard
that is plugged in while the program runs is switched on automatically; your choice is remembered. The status line names
the active MIDI inputs and says which channel is playing. Only notes are used (no pitch bend, pedals or knobs), and Notes can
be recorded into the piano roll with **Rec notes** (below).

### Recording notes into the piano roll (Rec notes)

**Rec notes** (toolbar) writes what a MIDI keyboard plays into a pattern, so you can build patterns by playing:

1. Select the channel to record into (click it in the Channel Rack) and make sure the keyboard is ticked in **Audio...**.
2. Click **Rec notes** (it turns red) and press **Play**. Recording only runs while the song plays, in Pattern mode (the
   current pattern loops) or Song mode.
3. Play. Each note goes into the **current pattern** of the **selected channel** at the position you heard it (the output
   latency is subtracted), with the key's velocity. A note held across the end of the pattern is split and continues at its
   start; a note still held when you press Stop is ended there. The notes appear in the Piano Roll straight away and can
   be edited like any other.

Right-click **Rec notes** to choose the **quantize** (1/4, 1/8, **1/16** by default, 1/32 or off). Notes already in the
pattern at the same place with the same pitch are not duplicated, so recording over a loop several times adds only new notes.
The pitch of a guitar or a voice is **not** turned into notes: use *Record* (below) for audio.

**Score log (0.28.0).** Everything you play on a MIDI keyboard is remembered for **five minutes**, also when nothing is
recording and the song is stopped, so an idea you played while trying things out is not lost. Right-click **Rec notes**:
*Put the last phrase into the pattern* takes what you played after your last pause of 3 seconds or more (with no key held),
*Put all of it into the pattern* takes the whole log, and *Clear the score log* forgets it. The notes go into the **current
pattern**, each on the channel it was played on, starting at the pattern's beginning and keeping your timing at the current
tempo (starts are put on the quantize grid chosen in the same menu). Notes that start after the end of the pattern are left out
and counted in the status line: make the pattern longer, or take the last phrase only. Undo takes the whole insertion back.

The arrow next to **Record** has *The recording becomes*: **A clip on a playlist track** (the default) or **A new sampler
channel**. With the second, an audio take from a microphone or guitar becomes the sample of a new channel, ready to be played with
a MIDI keyboard or sequenced in the piano roll (with no free channel it is put on the playlist instead).

## Pattern mode and Song mode

The **Pattern / Song** button decides what Play does.

- **Pattern**: loops the current pattern. The playhead runs in the Piano Roll and the Rack.
- **Song**: plays the Playlist from the start (or from where you clicked the ruler) to the end of the last clip, then loops, or repeats the loop
  region if there is one. The playhead runs in the Playlist and stays visible when stopped (it shows where Play will start). An empty playlist is silent.

You can edit while playing in either mode; the change is heard immediately.

### Time signature and tempo changes (0.46.0)

The button next to the BPM shows the **time signature** (4/4 to start with). Click it:

- **Time signature**: 2/4, 3/4, 4/4, 5/4, 6/4, 7/4, 6/8 or 12/8. The bars of the Playlist, the Piano Roll and the positions
  (bar.beat.tick) follow it, the metronome accents the first beat of every bar of it, and the snap value **Bar** is one bar of
  it. In 6/8 and 12/8 a beat is an eighth. A pattern is at least one bar of 4/4, or one bar when the bar is shorter (2/4 allows 2
  quarters, 3/4 and 6/8 allow 3); the rack's label says how many bars of the time signature it is.
- **Tempo changes**: *Add a tempo change at the song position...* (where you clicked the ruler, on its beat; at bar 2 when the
  song position is at the start) asks for a BPM. From there on the song plays at that tempo, until the next change. Each change
  is an orange flag with its tempo in the ruler, and the seconds under the ruler follow the tempos. In the menu each change can
  be given another tempo, moved to the song position or deleted; *Delete every tempo change* goes back to one tempo. A `*` on
  the button means the song has tempo changes.

The changes are steps, as with FL Studio's tempo markers (no gradual ramps), and only **Song mode** plays them: a pattern
plays at the toolbar's BPM, which is also the tempo the song starts with. Undo and redo include them, they are saved with the
project, and exports take them into account. **Audio clips** are still drawn and stretched for the song's starting tempo:
after a tempo change, an audio clip does not follow it (keep audio before the first change, or use patterns there).

## Automation

Automation makes a control move by itself while the song plays. It works on:

- a channel's **volume** and **pan** (the two sliders in the Channel Rack);
- a track's **fader** and **balance** (Mixer);
- every **parameter of an effect** in a track slot or in a master slot (the sliders of the effects panel);
- every **parameter of a plugin** (VST3, LV2, LADSPA): of a plugin instrument in the Channel Rack and of a plugin effect in a track or
  master slot (since 0.35.0, see *Plugin parameters* below);
- since 0.47.0: a track's **sends** to Aux A and Aux B (the small A / B sliders), an aux bus's **fader** and **balance**, and a
  **VCA group's** fader.

While the song plays in Song mode, the controls that have automation **move with it** (0.47.0): the faders, balance and send
sliders of the Mixer and the volume and pan of the Channel Rack. When the song stops they show their own values again (the
automation never changes them). Touching a control while it moves sets its own value, as usual.

**Not automatable yet:** the pitch and swing knobs, the master volume, mute and solo, the parameters of effects on an aux bus,
the tempo (use tempo changes, 0.46.0) and anything else.

**Create a clip.** Right-click the control and choose *Create automation clip*. A clip one pattern long appears on one of the four
**automation lanes** (*Auto 1-4*, under the tracks of the Playlist) and its editor opens. The clip is a flat line at the
control's current value. It starts at the pattern under the playhead if the song is playing in Song mode, otherwise at the start of the song. If all four
lanes are already occupied there (or 64 clips exist), nothing is created and the status line says so; move or delete a clip and try again.

**In the lanes** the clips behave like the other clips of the Playlist: drag to move, drag the right edge to make it longer or shorter,
*Duplicate* (Ctrl+D) and *Delete* from the right-click menu or the keyboard, the Erase tool removes one, the Zoom tool zooms. The clip shows
its name (for example *Track 1 Reverb Mix*) and a small picture of its curve. Clips on one lane cannot overlap. Everything here is undone
with the Playlist's Undo.

**The point editor** (double-click a clip, or right click > *Edit the points...*):

| Action | How |
| --- | --- |
| Add a point | Click on empty space |
| Move a point | Drag it. The time snaps to a 16th note; hold **Alt** to move freely (points can sit at a fraction of a tick). The value is always free. A point cannot jump over its neighbours |
| Shape of the line after a point | Right-click the point: *linear*, *hold* (a step) or *smooth* (an S curve) |
| Delete a point | Right-click > *Delete point*, or select it and press Delete or Backspace (a clip keeps at least one point) |

The axis on the left shows the control's own unit (dB for volumes, % or a plain number, Hz, ms...). A yellow line marks the default value.

**How it plays.** Automation is heard in **Song mode only**; in Pattern mode the controls work as usual. For each parameter the clip that started
last (and has begun) decides; inside it the value follows the curve, after its end the last value stays, before the first clip the control's
own value is used. While the song plays with automation the **on-screen controls do not move** (they keep showing the value you set), and
touching one has no lasting effect on a parameter that has a clip under the playhead. The value is updated once per 64 samples
(1.3 ms at 48 kHz): fast for any musical use, but it is not a per-sample ramp. Clips are saved in the project.

### Plugin parameters

A plugin's controls are in its own window, where there is no right-click menu of ours, so plugin parameters are chosen from a list:

- a **plugin instrument**: right-click the channel in the Channel Rack > *Automate a plugin parameter...* (greyed out for the
  other instruments, and for a plugin that has no parameters or could not be found);
- a **plugin effect**: open the Mixer, select the slot that holds it, then *Automate a parameter...* under *Open plugin window*
  (not for aux buses, as for the other effects).

The list shows every parameter the plugin offers with its current value, written as the plugin writes it. Type in the box at the
top to filter it by name (several words: all must appear). **Move a control in the plugin's window first** and that parameter is
already chosen when the list opens - the line above the box names it (FL Studio's "last tweaked"). Double-click a row, press
Enter or click *Automate*. The clip starts at the parameter's current value; its editor shows the plugin's values (On / Off, dB,
Hz...) where the plugin gives them, otherwise 0-100 %.

Notes: many plugins also list technical parameters (VST3 plugins built with JUCE add *MIDI CC* entries and a *Bypass*); the filter
is the quickest way through them. A clip whose plugin is missing when the project opens, or whose parameter the plugin no longer
has, stays in the project and does nothing. The plugin's window may not follow the automation while the song plays, as the other
controls do not.

## Mixer

Press **F9** or the **Mixer F9** button. The Mixer opens large over the other panels; press F9 again to close it.
It has 8 vertical **tracks** to start with, a **master** strip on the right and the **effects panel** of the selected track.

**More tracks (0.29.0).** The **+** button after the last track adds one; right-click it to add 10 or 100 at once, or to remove
the last track (only when no channel plays through it, it has no effects and no automation controls it). A project can have up to
**500** tracks; scroll the mixer sideways to reach them (adding scrolls to the new one). Adding and removing can be undone, and
the project file keeps the number. Tracks with no sound and no effect cost almost no processing. Reverbs, delays, choruses and
Pitch Correctors need about 0.8 MB of memory each, which is made for them as they are added: the very first moment after adding
many at once, the newest may let the sound through dry until their memory is ready (a few tenths of a second).

Sound path: each channel goes to the mixer track chosen with its **Track N** display (several channels can share
one; or use *Assign free mixer track* in the channel's right-click menu), then through the track's ten effect slots (four before 0.45.0),
then the track fader and balance, then the sum of all tracks goes through the master's ten effect slots and the
master fader.

Each track, from top to bottom:

- its name (Track 1...) and the channels routed to it;
- **ten effect slots** (0.45.0; four before), one above the other (see below);
- **balance** (stereo) - double-click to centre. It is a balance control: at the centre the sound is untouched;
- **M** (mute) and **S** (solo) - these belong to the track and are independent of the channel buttons in the rack.
  If any track is soloed, only soloed tracks are heard;
- a vertical **fader** with its dB read-out (silence up to +3.5 dB, 1.0 = 0 dB; double-click returns to 0 dB);
- a **meter**: the wide bar is the RMS level (an average over about 300 ms), the narrow bar is the peak, with a hold
  line and a lamp at the top that lights if the signal reached or passed 0 dB. Click the meter to clear the lamp.
  The scale runs from -60 to +6 dB.

Click a track to select it; the effects panel then shows its slots.

### Effects

**Effect chains (0.45.0).** The **Chain...** button of the effects panel saves the selected track's slots as a preset
(*Save this chain as a preset...*, a `.slchain` file in `~/Music/Sun Loops Studio/Presets/Effect chains`), loads one onto any track,
aux bus or the master (it replaces every slot), or clears every slot. Presets keep the built-in effects with their settings and
bypass; plugin slots are left out (the status line says so).

Click a slot for the menu: the effects, grouped (**EQ and filter**, **Dynamics**, **Drive**, **Modulation**, **Space and time**,
**Stereo**), then **Plugin effects**, **Bypass** and **Remove effect**. The effects run in order, top to bottom, before the track
fader. The effects panel has a power button per slot (bypass), a slider per parameter of the selected slot (a choice such as
*Type* or *Mode* steps through its names; double-click a slider for its default) and, for most effects, **Presets...** with
ready-made settings.

| Effect | Parameters | Presets |
| --- | --- | --- |
| Equalizer | Low shelf (120 Hz), Mid (a bell with its own frequency, 200-5000 Hz), High shelf (6 kHz); each -15 to +15 dB | |
| Parametric EQ | Low shelf (frequency, gain), a bell (frequency, gain, Q), high shelf (frequency, gain), each -18 to +18 dB; output | Vocal presence, Kick punch, Warm master |
| Filter | Type (low-pass, high-pass, band-pass, notch), Cutoff, Resonance, Slope (12 or 24 dB), Drive, LFO rate and depth (sweeps the cutoff up to 3 octaves), Mix | Low-pass 24 dB, High-pass, Wobble, Telephone |
| Compressor | Threshold (-60 to 0 dB), Ratio (1-20:1), Attack, Release, Makeup gain, **Sidechain** (0.45.0: *Off*, or a mixer track whose sound - before its own effects and fader, so even a muted track works - drives the gain reduction: the classic pumping bass under the kick) | Gentle, Drum bus, Vocal (a preset keeps the sidechain) |
| Maximizer | A mastering limiter in three stages: Input gain, optional Soft clip, Glue (a gentle soft-knee compressor), then a look-ahead limiter that keeps every peak at or below the **Ceiling**; Release. It delays the sound by 1.5 ms (its look-ahead). | Transparent, Loud, Streaming (-1 dB) |
| Transient | Splits each sound into its attack and its sustain: Attack and Sustain gain (-24 to +24 dB), Speed (how long an attack lasts), Listen (both, transients only, sustain only), Mix, Output | Punchy drums, Tighten, More room, Transients only |
| Overdrive | Drive, Boost (x10), Color (darker / brighter before the drive), Tone (after it), Character (adds even harmonics), Mix, Output. Runs at 4x the sample rate with anti-aliasing. | Warm, Crunch, Fuzz, Parallel grit |
| Chorus | Rate, Depth, Delay, Feedback (high feedback and a short delay make a flanger), Voices (1-3), Width, Mix | Subtle, Wide, Ensemble, Flanger |
| Phaser | Rate, Depth, Center frequency, Feedback, Stages (4, 6, 8, 12), Width, Mix | Slow sweep, Fast, Deep 12 |
| Reverb | Size, Damping, Width, Mix, **Type** (Hall, Room, Plate), Pre-delay (0-250 ms) | Hall, Large hall, Room, Small room, Plate, Vocal plate |
| Delay | Time (10-1000 ms), Feedback, Tone, Mix, **Mode** (Stereo, Ping-pong: the repeats bounce between the sides, Tape: darker, saturated repeats), Spread (the right side up to 1.5 x later), Wobble (tape wow and flutter) | Stereo 1/8, Ping-pong 1/4, Tape echo, Slapback, Dub space |
| Stereo Imager | Width (0-200 %), Mid and Side gain, Mono bass (below this frequency the sound becomes mono), Balance | Wider, Mono bass, Mono |
| Time Shaper (0.25.0) | In the manner of Gross Beat, synced to the song. **Time**: None, Half speed, Reverse (each cycle plays the one before backwards), Tape stop, Slow down, Stutter 1/8 and 1/16, Repeat 1/4 (the cycle's first quarter four times), Scratch. **Volume**: None, Gate 1/8, 1/16, 1/32, Trance gate, Pump (like sidechain), Fade in, Fade out. **Cycle** (1 beat to 2 bars) is what the time pattern spans; **Depth** of the volume pattern; **Smooth** (crossfades the jumps and softens the gates); Mix. Up to four Time Shapers in a project play their time patterns (each needs a recording of the last 8.5 s); more only shape the volume, and since 0.41.0 the effects panel says so (in orange) when one of those is selected | Half-time bar, Reverse every bar, Tape stop, Stutter 1/16, Beat repeat, Scratch, Trance gate, Gate 1/16, Sidechain pump, Half-time gated |
| Pitch Corrector (0.26.0) | In the manner of Pitcher, for one voice or instrument at a time (not chords). **Key** and **Scale** (the piano roll's scales): each note is pulled to the nearest note of the scale. **Speed**: how fast (0 ms = at once, the hard "tuned" sound; 50-100 ms sounds natural and lets vibrato through). **Amount** of the correction. **Shift** moves everything up or down (up to an octave, in fractions of a semitone too). **Range** of the voice: Low (40-400 Hz), Voice (70-900 Hz), High (150-2000 Hz). Mix (with Shift and Mix at 50 %, a harmony). The corrected sound is 10-15 ms late (the time it needs to hear the pitch); formants move with the pitch, so big shifts sound like a different voice. Breath, consonants and noise pass unchanged. **Follow** (0.41.0): *Scale* (as above), or a **channel**: the voice is pulled to the note that channel is playing, in the octave nearest to the voice (as Pitcher with MIDI); while the channel plays nothing, the voice is left alone. Put the notes you want sung in a pattern of an instrument channel (turn its volume down if you do not want to hear it) | Natural (chromatic), Hard tune (chromatic), C major fast, A minor natural, A minor pentatonic hard, Bass chromatic, Octave up, Fifth harmony |

Projects made before 0.21.0 sound the same: their reverbs are Halls without pre-delay and their delays are Stereo.
Files with the new effects cannot be read by older versions (the slot comes back empty there).

These are Sun Loops Studio's own effects. The slot menu also has **Plugin effects** (the effect plugins found by the last
scan, see *Plugins*) and **Open plugin window** for a plugin slot; a plugin's parameters are in its own window, not in the effects panel.

### Master effects

The **master strip** (right of the tracks) has the same ten effect slots as a track. Click a slot to pick an effect
(all the effects above, a Maximizer at the end for mastering); click the strip or a slot to show it in the effects panel, which is titled
*Effects - Master*. The master chain processes the **sum of all tracks**, in order, **before the master fader** and
the final soft limiter, so a compressor or equalizer there shapes the whole mix, and the exported WAV includes it. Use
it for glue compression, a final EQ, or a touch of reverb on everything. Slots can be bypassed or removed like those
on a track, and the chain is saved in the project.

The master volume is the toolbar slider and the master fader is the same control. The master strip has a meter.

### Sends, aux buses and VCA groups

Under its effect slots every track has:

- **A** and **B**: how much of the track is sent to **Aux A** and **Aux B** (all the way left: nothing; 1.5 at the far right). Double-click
  a slider to switch the send off;
- **Pre**: when on, the sends take the sound **before** the fader, the balance and the VCA (so the send level does not follow the
  fader). Off (the default), they are **post-fader**. A muted track, or a track silenced by another track's solo, sends nothing either way;
- **V1** and **V2**: put the track in VCA group 1 and/or 2.

**Aux buses** (the two strips after the tracks, with a green bar) sum what the tracks send them, run it through their own ten effect slots
(click a slot as on a track), then their fader and balance, and add the result to the mix **before the master effects**. Typical use: one reverb
on Aux A, a delay on Aux B (set the effect's Mix to 100 %), and each track sends as much as it needs. An aux bus has mute and balance but no
solo, and its name is fixed (Aux A, Aux B). Its effect parameters cannot be automated yet.

**VCA groups** (the two strips with a blue bar) have a fader and a mute. The fader multiplies the fader of every member track
(0 to +3.5 dB); mute silences them. The VCA does not move the member faders, it scales them: a post-fader send follows it, a
pre-fader send does not, and a track in two groups is scaled by both. The strip shows how many tracks are members.

There is no separate routing-matrix window: each track strip is one row of the matrix (its two sends and its two VCA switches), the
strips of the buses and groups are the columns. A bus cannot be sent to another bus (no feedback loops) and there is no sidechain. Sends, buses
and VCA groups are saved in the project.

The mixer is wider with the buses: if the panel is too narrow the strips scroll sideways (there is a scroll bar under them); double-click
the panel's title bar to make it as large as the window.

## Meters

The **Meters** button (second row of the toolbar) opens a small window that stays above the others (close it with its x or the button
again). It measures what the program **sends to the speakers**: after the master volume, with the start-up sound and the
metronome included, and it is not the same as the exported file (the export is rendered separately and is not metered).

**Loudness (LUFS).** Following ITU-R BS.1770-4 and EBU R128:

- *Momentary* (the last 400 ms), *Short-term* (the last 3 s);
- *Integrated*: the whole programme since the last **Reset** (or since the program started), ignoring silence (under -70 LUFS) and
  everything more than 10 LU below the average. It turns **green** within 1 LU of the EBU broadcast target of -23 LUFS;
- *Loudness range* (LRA) in LU, as EBU Tech 3342: how much the short-term loudness varies (10th to 95th percentile, silence and passages 20 LU
  below the average left out). A steady tone reads 0.
- *True peak* in dBTP: the highest level of the signal as a converter would reconstruct it (four times oversampling), which can exceed the
  highest sample. It turns red above -1 dBTP.

The loudness readings are checked in the automated tests against the EBU reference signals (not against a hardware meter); the true-peak
filter is accurate to about 0.05 dB on a faded tone and a start with no fade-in can read up to 0.4 dB high, because a click really does
overshoot.

**Spectrum.** A 4096-point FFT (Hann window) of the left+right mix, 20 Hz to 20 kHz on a logarithmic axis and 0 to -90 dB, updated 30 times a second; the
fill falls 36 dB per second, the thin line is the peak hold (the *Peak hold* button turns it off, *Reset* clears it).

**Goniometer and correlation.** The last 1024 samples as dots: a mono sound is a vertical line, a sound on the left leans left, a wide sound fills the
square. The bar shows the correlation of the two channels (+1 mono, 0 unrelated, -1 opposite phase: a red bar warns that the sound can disappear on a mono speaker).

## Metronome

The **Metronome** button turns a click on or off. Right-click it for a small panel with the **tempo** (the same BPM as
the toolbar), the **volume** and a **Tap tempo** button: tap it in time with a song (four or more taps) and the BPM
follows. The click is a higher, louder one on the first beat of every bar and a lower one on the other beats (the bar of the time signature, 0.46.0). While the
song plays it follows the song; while it is stopped it keeps time by itself at the BPM, so you can practise or record.
The metronome is never part of an export. It is remembered between runs.

## Export

**File > Export audio** writes the current mode: the pattern (one bar unless you changed it) or the song (up to the end of the last clip, audio
clips included), plus one second of tail so releases and drum tails are not cut. It is rendered stereo, at the
BPM set in the toolbar, by the same engine that plays, so it sounds like what you hear. Choose the format in the submenu; then
**Export options** (0.45.0, remembered) ask for:

- **Sample rate**: 44100, 48000 (the default), 88200 or 96000 Hz.
- **Bit depth** (WAV, AIFF, FLAC): 16-bit (with TPDF dither), 24-bit, or 32-bit floating point (WAV). **Quality** (MP3, AAC,
  Ogg Vorbis): 96 to 320 kbit/s.
- **Title** and **Artist**, written into WAV (INFO list), Ogg Vorbis (comments), MP3 and AAC (tags, through ffmpeg); FLAC and AIFF
  get none.
- **One file per mixer track (stems)**: instead of the mix, one file for each mixer track that plays something, named
  *<name> - <track>*, each rendered with only that track soloed (its sends and the master effects included, as in the mix);
  silent tracks are skipped. Together they add up to the mix (as long as the master effects are not compressors or limiters).

The table gives the defaults:

| Format | Kind | Notes |
| --- | --- | --- |
| WAV `.wav` | uncompressed | 24 bit |
| AIFF `.aiff` | uncompressed | 24 bit |
| FLAC `.flac` | lossless | 24 bit |
| MP3 `.mp3` | lossy | 192 kbit/s, needs `ffmpeg` |
| Ogg Vorbis `.ogg` | lossy | about 192 kbit/s |
| Ogg audio `.oga` | lossy | the same Vorbis audio under the `.oga` name |
| AAC `.aac` | lossy | 192 kbit/s, a plain AAC stream, needs `ffmpeg` |

Entries that need `ffmpeg` say so in the menu when it is not installed. The file is written under a temporary name and
moved into place at the end, so a failed export never damages a file that was already there.

## Audio setup

**Audio...** opens the device selector. On Linux, JUCE talks to ALSA or JACK; on Nobara the PipeWire compatibility
layers make both work. If the status line says "No audio device", pick an output in **Audio...** and check that your
system has an output available. Latency is what the device reports; it is not measured by the program.

### Buffer size (crackling)

The audio buffer is how much sound the program prepares ahead. **A bigger buffer is safer** (no crackling or
dropouts) but adds delay between pressing a note and hearing it; a smaller one reacts faster but needs a quicker, less
busy computer. The program starts with **1024 samples** (about 21-23 ms), a safe choice for most computers.

In **Audio...**, under *Buffer size*:

- **The menu** sets the size you want (64 to 8192 samples, with the delay in milliseconds next to each). It applies
  at once; playback continues. If you hear crackling, choose the next bigger size. If notes feel late when you play
  the piano roll, choose a smaller one.
- **Auto: set for this computer** is for when you do not want to guess. It plays your current song (starting it if it is
  stopped) at 256, 512, 1024, ... samples, for about two and a half seconds each, with extra load added to every
  block so that a computer short of headroom shows it. For each size it watches the audio system's count of dropped
  blocks and the longest time one block took. It stops at the first size that is clean *and* whose next bigger size is
  clean too, and uses that bigger one: one measured step of safety margin. The dialog lists what was tested
  (for example `256 ok  512 ok`). If even the biggest size drops blocks it says so (the computer is very busy: close
  other programs). **Cancel** (or closing the window) puts the previous size back.
- **The read-out** shows the computer's load now, the *busiest block* (how much of its time the slowest block used;
  1 % means very comfortable, over 50 % means too tight) and the number of **dropped blocks** since the buffer was
  last changed. It is a live way to see whether a size works: play the song and watch it.

The device and buffer size are remembered for the next start. With JACK the buffer is set in the JACK server, so the
menu and Auto are unavailable there. Auto is a measurement on your computer at that moment: a very busy desktop later,
a heavier song or a plugin-heavy mixer can still need a bigger size.

## Limits

- Up to 32 channels, up to 500 mixer tracks (8 to start with) with 10 effect slots each, 2 aux buses (10 slots each), 2 VCA groups, 64 patterns, 1 to 128 playlist tracks, a timeline of
  at least 32 patterns and 64 bars, 4 automation lanes, 64 automation clips of up to 256 points each, 32 simultaneous voices.
- The effects use about 15 MB of memory at 48 kHz to start with, plus about 0.8 MB for every reverb, delay, chorus and Pitch Corrector beyond
  the sixteenth (more at higher sample rates). Memory made for them stays until the audio device restarts.
- Patterns are 4 to 64 quarters long (the same for all patterns; 2 or 3 in a short time signature); one time signature for the whole song; up to 256 tempo changes, in steps, played in Song mode only; audio clips do not follow them.
- Samples are mono or stereo, at most 30 seconds. The sampler has one envelope and one filter per channel (no filter envelope, LFO or modulation);
  at most four zones sound per note. Audio clips: up to 5 minutes per file, 64 clips.
- The synth: one LFO and one filter per channel, two oscillators, no unison, no additive engine yet, no modulation of the envelopes by
  velocity; the LFO is restarted with every note. The FM engine has four operators, sine waves only, no filter, and its matrix is evaluated
  with a delay of one sample (as in most matrix FM synths).
- Plugins: VST3, LV2 and LADSPA (no VST2, no Audio Units); stereo in and out (extra outputs and side-chain inputs are not used); their parameters
  can be automated (0.35.0); edits made in a plugin's window are saved with the project but are not in the undo history.
- Not available yet: time-stretching of samples in the Sampler (only audio clips on the playlist can be stretched), automation of the pitch, swing,
  master volume, mute and solo, per-clip effects. See [roadmap.md](roadmap.md).
