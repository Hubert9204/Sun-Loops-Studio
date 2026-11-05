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

Everything in the song is saved in one `.slp` file (versions before 0.8 wrote `.slsproj`; those files still open): channels (instrument, sample, volume, pan, pitch, swing,
mixer track), all patterns and their notes, the playlist, the mixer (faders, balance, mute/solo, effect chains and
their settings), the audio clips of the playlist (as paths of their files), BPM, master volume and whether Song mode is on. The window title shows the project name, with a
`*` after it while there are changes that are not saved.

| Command | Key | What it does |
| --- | --- | --- |
| **New** | `Ctrl+N` | Starts an empty project (one synth channel, one empty pattern). |
| **Open...** | `Ctrl+O` | Chooses a `.slp` (or older `.slsproj`) file. |
| **Open recent** | | The last eight projects that still exist. |
| **Save** | `Ctrl+S` | Writes the project. |
| **Save as...** | `Ctrl+Shift+S` | Asks for a name (the `.slp` ending is added if you leave it out) and writes the project there. An old `.slsproj` is offered as `.slp`. |
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

**Damaged files.** A file that is not a Sun Loops Studio project, is damaged or was written by a newer version is
refused with a message and your current project is left untouched. Values that are out of range are clamped and notes
or clips that cannot be placed are dropped, with a message saying so.

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

## Browser

The sidebar on the left lists your sounds as a tree. Click the arrow of a folder to open it.

| Folder | Contents |
| --- | --- |
| Samples | The built-in sounds (kick, snare, hat), then the audio files (WAV, FLAC, OGG, AIFF) found in `~/Music/Sun Loops Studio/Samples`, with its sub-folders. |
| Drum Kits | A built-in kit, then one entry per sub-folder of `~/Music/Sun Loops Studio/Drum Kits`. |
| Instruments | *Synth* and *Sampler*. |
| Presets | Six factory channel presets, then your own `.slspreset` files from `~/Music/Sun Loops Studio/Presets`. |
| Plugin | Starts empty. *Scan for VST3 plugins...* searches the usual VST3 folders in the background and lists what it finds. **The plugins cannot be used yet** (hosting is not implemented); they are shown for information only. |

The three folders under `~/Music/Sun Loops Studio` are created when the program starts. **Folder** opens that
location in your file manager; **Refresh** looks for new files (the browser does not watch the disk).

- **Search**: type in the box at the top to filter every folder at once; matching folders open by themselves and the
  line under the tree counts the results. **Esc** clears the search.
- **Drag and drop**: drag a sound onto a channel of the Channel Rack (the row is outlined in green) to load it
  there; drop it on the empty space below the rows to create a new channel for it. A preset sets the channel's
  instrument, sample, volume, pan, pitch and swing; an instrument item switches the channel's instrument.
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
- **Track N** is the mixer track the channel plays through. Click it for a menu of the 8 tracks, or drag it up or
  down, or scroll over it. By default channel 1 goes to Track 1, channel 2 to Track 2, and so on.
- **Sampler channels** show 32 step buttons (two bars of 16th notes). Click a step to turn it on or off. The
  **Load** button picks a sample file.
- **Synth channels** show a small preview of their notes. Edit the notes in the Piano Roll.
- Click a row to select that channel (the Piano Roll and the Mixer follow the selection).
- **+ Add channel** under the last row adds a synth or sampler channel, up to 8 channels in total.

### Right-click menu on a channel

| Item | Effect |
| --- | --- |
| Piano roll | Selects the channel and opens the Piano Roll for it. |
| Instrument > Synth / Sampler | Chooses what makes the sound. Switching to Sampler on a channel without a sample asks for one. The sample is kept if you switch back to Synth. |
| Load sample... | Pick a WAV, FLAC, OGG or AIFF file (the first 30 seconds are used). The channel is renamed after the file. |
| Rename... | Up to 16 characters. |
| Assign free mixer track | Opens a window listing the 8 mixer tracks: free ones (no instrument on them) in green, the others with the instruments that are on them, and the current one marked. The first free track is selected for you; **Select first free track** picks it again, or choose any track (sharing one is allowed), then **Assign** (or Return; Escape cancels). The channel's **Track N** display and the mixer follow, and the mixer selects that track. |
| Save as preset... | Asks for a name and writes a `.slspreset` file (instrument, sample, volume, pan, pitch, swing; not the notes) into `~/Music/Sun Loops Studio/Presets`; it appears in the browser's Presets folder at once. |

Double-clicking a synth channel's note preview, or the name of any channel, also opens its Piano Roll. On sampler
channels double-clicking the steps is not special: every click toggles a step.

## Piano Roll

Edits the notes of the selected channel in the current pattern. The title shows the channel ("Piano Roll - Lead").
It is closed at start and opens from a channel, as above.

| Action | How |
| --- | --- |
| Play a note | Left button on a key of the keyboard (held = sounds). |
| Add a note | Click an empty cell; keep the button down and drag to the right to set its length. It sounds while you hold. |
| Move a note | Drag it. |
| Change its length | Drag its right edge. |
| Delete a note | Right-click it. |
| Scroll pitch | Mouse wheel. |
| Scroll time | Shift + wheel. |
| Zoom time | Ctrl + wheel. Until you zoom, the whole pattern fits the panel width. |

**Playing the notes.** The keyboard on the left and the notes you draw are playable:

- Press the left button on a **key** and the note sounds for as long as you hold the button; the key lights up.
  Keep the button down and move up or down to play the keys in turn (a glissando).
- When you **create** a note (click on an empty cell), **move** it or **resize** it, it sounds while the button is down.
  Moving it to another row changes the note you hear. Right-click does not play anything.
- The sound is that of the selected channel: its instrument or sample, volume, pan, **Pitch**, mixer track and effects. It
  works whether the transport is playing or stopped. On a sampler channel each press plays the sample once, at the
  pitch of the key.
- If the sound seems missing, check the channel's **M** button, the track's **M** and the master volume.

Notes snap to 16th notes. A pattern is two bars long. A synth note ends at its length or at the end of the pattern,
whichever comes first. When you open a channel the view scrolls to its notes.

## Patterns

A **pattern** holds the notes of every channel. The pattern box in the toolbar selects the current pattern: the
Channel Rack and the Piano Roll show and edit that pattern. The **+** button next to it offers **New empty pattern**
and **Clone current pattern**. The demo project starts with "Pattern 1" (the full groove) and "Drums" (the drums only).

Channel settings (volume, pan, mute, solo, pitch, swing, instrument, sample, mixer track) belong to the channel, not
to the pattern: they are the same in every pattern. Mixer tracks and their effects belong to neither.

## Playlist

The Playlist arranges patterns in time. It has 8 tracks and a timeline of 32 pattern lengths. The ruler shows bar
numbers (a pattern is two bars, so the numbers go 1, 3, 5, ...). The orange vertical line marks the end of the song:
in Song mode playback loops there.

| Action | How |
| --- | --- |
| Place a clip | Click an empty cell. The clip holds the pattern selected in the pattern box and is one pattern long. Drag right straight away to make it longer. |
| Move a clip | Drag it (it snaps to pattern boundaries and can change track). |
| Repeat / shorten | Drag the right edge of the clip: the pattern repeats once per pattern length. |
| Delete | Right-click the clip. |
| Select its pattern | Double-click the clip. |
| Scroll time | Mouse wheel. |
| Zoom | Ctrl + wheel. |

Clips cannot overlap on the same track; a move or resize that would overlap is simply not applied. Clips on
different tracks play together. Clicking a track's **name** chooses the track that **Record** writes to (marked with a
red dot).

### Audio clips

Audio clips are recordings and audio files placed on the playlist. They are drawn in teal with their waveform.

| Action | How |
| --- | --- |
| Add an audio or video file | Drag it from your file manager onto the Playlist (it lands on the track and beat under the mouse, which is highlighted), or use *File > Import audio or video into the playlist...*. Several files go on consecutive tracks. |
| Move | Drag the clip (it snaps to the beat and can change track). |
| Shorten / lengthen | Drag its right edge: at least one beat, at most the length of the audio. |
| Delete | Right-click it. |

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

### Recording

**Record** (toolbar) records the **audio input**: a microphone, a guitar into an audio interface, a line input.

1. In **Audio...** choose the input device and switch on its input channels; speak or play and watch the **Input level**
   bar. (If you skip this, pressing Record switches on the device's first input channels for you.)
2. In the Playlist click the **name of an empty track**: it gets a red dot. The arrow next to Record offers the same
   choice, plus the input type: **Mono** (the first input, written as a one-channel file: use this for a microphone or
   a guitar) or **Stereo** (inputs 1 and 2), and *Hear the input while recording* (use headphones: with speakers and a
   microphone it feeds back; this monitoring adds the device's latency).
3. Press **Record**. The song plays from the start, in Song mode, and the status line shows the time and the input
   level (it warns about clipping). Press **Record** again or **Stop** to finish; the take appears as an audio clip at
   the start of the track and is also kept as a 24-bit WAV in `~/Music/Sun Loops Studio/Samples/Recordings/`.

The recording is shifted by the round-trip latency that the audio device reports so that it lines up with what was
playing. The track must be free at the start of the playlist (otherwise Record says so); a take stops by itself at the
end of the playlist or after 5 minutes. Takes always start at the beginning of the song: there is no punch-in or
count-in yet (switch the **Metronome** on to play in time).

### MIDI keyboards and other devices

**Audio...** also lists the **MIDI inputs**. Tick the keyboards you want: they play the **selected channel** of the
Channel Rack (click a channel to select it) with the velocity of the keys, whether the song is playing or not. A keyboard
that is plugged in while the program runs is switched on automatically; your choice is remembered. The status line names
the active MIDI inputs and says which channel is playing. Only notes are used (no pitch bend, pedals or knobs), and MIDI is
for playing: it does not record into the piano roll.

## Pattern mode and Song mode

The **Pattern / Song** button decides what Play does.

- **Pattern**: loops the current pattern. The playhead runs in the Piano Roll and the Rack.
- **Song**: plays the Playlist from the start to the end of the last clip, then loops. The playhead runs in the
  Playlist. An empty playlist is silent.

You can edit while playing in either mode; the change is heard immediately.

## Mixer

Press **F9** or the **Mixer F9** button. The Mixer opens large over the other panels; press F9 again to close it.
It has 8 vertical **tracks**, a **master** strip on the right and the **effects panel** of the selected track.

Sound path: each channel goes to the mixer track chosen with its **Track N** display (several channels can share
one; or use *Assign free mixer track* in the channel's right-click menu), then through the track's four effect slots,
then the track fader and balance, then the sum of all tracks goes through the master's four effect slots and the
master fader.

Each track, from top to bottom:

- its name (Track 1...) and the channels routed to it;
- **four effect slots**, one above the other (see below);
- **balance** (stereo) - double-click to centre. It is a balance control: at the centre the sound is untouched;
- **M** (mute) and **S** (solo) - these belong to the track and are independent of the channel buttons in the rack.
  If any track is soloed, only soloed tracks are heard;
- a vertical **fader** with its dB read-out (silence up to +3.5 dB, 1.0 = 0 dB; double-click returns to 0 dB);
- a **meter**: the wide bar is the RMS level (an average over about 300 ms), the narrow bar is the peak, with a hold
  line and a lamp at the top that lights if the signal reached or passed 0 dB. Click the meter to clear the lamp.
  The scale runs from -60 to +6 dB.

Click a track to select it; the effects panel then shows its slots.

### Effects

Click an empty slot (or right-click any slot) for the menu: **Equalizer**, **Reverb**, **Delay**, **Compressor**,
**Bypass**, **Remove effect**. The effects run in order, top to bottom, before the track fader. The effects panel has
a power button per slot (bypass) and a slider per parameter of the selected slot.

| Effect | Parameters |
| --- | --- |
| Equalizer | Low shelf (120 Hz), Mid (a bell with its own frequency, 200-5000 Hz), High shelf (6 kHz); each -15 to +15 dB |
| Reverb | Size, Damping, Width, Mix |
| Delay | Time (10-1000 ms), Feedback, Tone, Mix |
| Compressor | Threshold (-60 to 0 dB), Ratio (1-20:1), Attack, Release, Makeup gain |

These are Sun Loops Studio's own effects. **VST3 plugins cannot be loaded into the slots yet**: the slot menu shows
a greyed-out "VST3 plugin..." entry as a reminder.

### Master effects

The **master strip** (right of the tracks) has the same four effect slots as a track. Click a slot to pick an effect
(Equalizer, Reverb, Delay, Compressor); click the strip or a slot to show it in the effects panel, which is titled
*Effects - Master*. The master chain processes the **sum of all tracks**, in order, **before the master fader** and
the final soft limiter, so a compressor or equalizer there shapes the whole mix, and the exported WAV includes it. Use
it for glue compression, a final EQ, or a touch of reverb on everything. Slots can be bypassed or removed like those
on a track, and the chain is saved in the project.

The master volume is the toolbar slider and the master fader is the same control. The master strip has a meter.

## Metronome

The **Metronome** button turns a click on or off. Right-click it for a small panel with the **tempo** (the same BPM as
the toolbar), the **volume** and a **Tap tempo** button: tap it in time with a song (four or more taps) and the BPM
follows. The click is a higher, louder one on the first beat of every bar and a lower one on the other three. While the
song plays it follows the song; while it is stopped it keeps time by itself at the BPM, so you can practise or record.
The metronome is never part of an export. It is remembered between runs.

## Export

**File > Export audio** writes the current mode: the pattern (two bars) or the song (up to the end of the last clip, audio
clips included), plus one second of tail so releases and drum tails are not cut. It is rendered at 48 kHz stereo, at the
BPM set in the toolbar, by the same engine that plays, so it sounds like what you hear. Choose the format in the submenu:

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

- Up to 8 channels, 8 mixer tracks with 4 effect slots each, 64 patterns, 8 playlist tracks, a 32-pattern-long
  timeline, 32 simultaneous voices.
- The effects use about 18 MB of memory at 48 kHz (more at higher sample rates).
- A pattern is always two bars long, 4/4, 16th-note grid.
- Samples are mono or stereo, at most 30 seconds, played one-shot (no looping, no envelope editing). Audio clips: up to
  5 minutes per file, 64 clips.
- Not available yet: playing notes from the computer keyboard, undo, embedding samples in the project, automatic saving
  and crash recovery, VST plugins as instruments or effects, sends, automation, recording MIDI notes, punch-in and
  count-in for recording, trimming or splitting audio clips, deleting or renaming patterns, seeking by clicking the
  ruler. See [roadmap.md](roadmap.md).
