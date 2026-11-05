# User guide

*[Italiano](user-guide.it.md)*

This guide describes what Sun Loops Studio 0.4 does today. Limits and missing features are listed at the end.

## The window

A **toolbar** on two rows, a status line, and below it the **workspace** where the panels float.

Toolbar, first row: **Play / Stop**, **Pattern / Song** switch, the **pattern box** with **+**, **BPM** (40-240),
**Master** volume with its meter, **Audio...** (device settings) and **Export WAV...**.

Toolbar, second row: buttons that show or hide the panels (**Rack F6**, **Playlist F5**, **Piano F7**, **Mixer F9**),
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

Default layout: Channel Rack and Playlist open at the top, Piano Roll and Mixer closed.

## Keyboard

| Key | Action |
| --- | --- |
| Space | Play / Stop |
| F5 | Playlist |
| F6 | Channel Rack |
| F7 | Piano Roll |
| F9 | Mixer |

The keys work when the main window is active. Typing into a text field (for example renaming a channel) takes the
keyboard for that field.

## Channel Rack

One row per channel: name, **M** (mute), **S** (solo), volume, pan, level meter and, on the right, the step area.

- **Volume** goes from 0 to 1.5 (default 0.8) and **pan** from left to right; double-click a slider to reset it.
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

Double-clicking a synth channel's note preview, or the name of any channel, also opens its Piano Roll. On sampler
channels double-clicking the steps is not special: every click toggles a step.

## Piano Roll

Edits the notes of the selected channel in the current pattern. The title shows the channel ("Piano Roll - Lead").
It is closed at start and opens from a channel, as above.

| Action | How |
| --- | --- |
| Add a note | Click an empty cell; keep the button down and drag to the right to set its length. |
| Move a note | Drag it. |
| Change its length | Drag its right edge. |
| Delete a note | Right-click it. |
| Scroll pitch | Mouse wheel. |
| Scroll time | Shift + wheel. |
| Zoom time | Ctrl + wheel. Until you zoom, the whole pattern fits the panel width. |

Notes snap to 16th notes. A pattern is two bars long. A synth note ends at its length or at the end of the pattern,
whichever comes first. When you open a channel the view scrolls to its notes.

## Patterns

A **pattern** holds the notes of every channel. The pattern box in the toolbar selects the current pattern: the
Channel Rack and the Piano Roll show and edit that pattern. The **+** button next to it offers **New empty pattern**
and **Clone current pattern**. The demo project starts with "Pattern 1" (the full groove) and "Drums" (the drums only).

Mixer settings (volume, pan, mute, solo, instrument, sample) belong to the channel, not to the pattern: they are the
same in every pattern.

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

One vertical strip per channel: name, pan, mute / solo, a fader with a dB read-out below it (from silence up to
+3.5 dB; the default 0.8 is about -1.9 dB; double-click resets it), and a level meter. The strips are linked to the rack rows: change one, the other follows. Click a
strip to select the channel. The master volume and its meter are in the toolbar.

## Export

**Export WAV...** writes the current mode: the pattern (two bars) or the song (up to the end of the last clip), plus
one second of tail so releases and drum tails are not cut. Format: 24-bit, 48 kHz stereo WAV, at the BPM set in the
toolbar. The file is produced by the same engine that plays, so it sounds like what you hear.

## Audio setup

**Audio...** opens the device selector. On Linux, JUCE talks to ALSA or JACK; on Nobara the PipeWire compatibility
layers make both work. If the status line says "No audio device", pick an output in **Audio...** and check that your
system has an output available. Latency is what the device reports; it is not measured by the program.

## Limits

- Up to 8 channels, 64 patterns, 8 playlist tracks, a 32-pattern-long timeline, 32 simultaneous voices.
- A pattern is always two bars long, 4/4, 16th-note grid.
- Samples are mono or stereo, at most 30 seconds, played one-shot (no looping, no envelope editing).
- Not available yet: saving and opening projects, undo, VST plugins as instruments or effects, effects of any
  kind, automation, MIDI input and recording, deleting or renaming patterns, seeking by clicking the ruler.
  See [roadmap.md](roadmap.md).
