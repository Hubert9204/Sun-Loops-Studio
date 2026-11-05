# User guide

*[Italiano](user-guide.it.md)*

This guide describes what Sun Loops Studio 0.5 does today. Limits and missing features are listed at the end.

## The window

A **toolbar** on two rows, a status line, and below it the **browser** (left sidebar) and the **workspace** where
the panels float.

Toolbar, first row: **Play / Stop**, **Pattern / Song** switch, the **pattern box** with **+**, **BPM** (40-240),
**Master** volume with its meter, **Audio...** (device settings) and **Export WAV...**.

Toolbar, second row: buttons that show or hide the browser (**Browser F8**) and the panels (**Rack F6**,
**Playlist F5**, **Piano F7**, **Mixer F9**),
**Reset** (default panel layout) and **Clear notes** (empties the selected channel in the current pattern).

The status line shows the audio device (sample rate, buffer size, reported latency) and the last message.

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
different tracks play together.

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
one), then through the track's four effect slots, then the track fader and balance, then the master.

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

The master volume is the toolbar slider and the master fader is the same control. The master strip has a meter and
no effects.

## Export

**Export WAV...** writes the current mode: the pattern (two bars) or the song (up to the end of the last clip), plus
one second of tail so releases and drum tails are not cut. Format: 24-bit, 48 kHz stereo WAV, at the BPM set in the
toolbar. The file is produced by the same engine that plays, so it sounds like what you hear.

## Audio setup

**Audio...** opens the device selector. On Linux, JUCE talks to ALSA or JACK; on Nobara the PipeWire compatibility
layers make both work. If the status line says "No audio device", pick an output in **Audio...** and check that your
system has an output available. Latency is what the device reports; it is not measured by the program.

## Limits

- Up to 8 channels, 8 mixer tracks with 4 effect slots each, 64 patterns, 8 playlist tracks, a 32-pattern-long
  timeline, 32 simultaneous voices.
- The effects use about 16 MB of memory at 48 kHz (more at higher sample rates).
- A pattern is always two bars long, 4/4, 16th-note grid.
- Samples are mono or stereo, at most 30 seconds, played one-shot (no looping, no envelope editing).
- Not available yet: playing notes from the computer keyboard or a MIDI keyboard, saving and opening projects (mixer settings and effects are lost on exit like everything
  else), undo, VST plugins as instruments or effects, sends and master effects, automation, MIDI input and
  recording, deleting or renaming patterns, seeking by clicking the ruler, dropping files from the desktop onto
  the program. See [roadmap.md](roadmap.md).
