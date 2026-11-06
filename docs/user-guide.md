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

*(New in 0.9.0: **Samples > Cyber-Hub** and **Drum Kits > Cyber-Hub Kit** hold the built-in system sounds by Hubert, and the program plays a start-up sound when it opens: switch it off in **File > Play the start-up sound**. See [cyber-hub.md](cyber-hub.md).)*

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
| Legato / slide | Shift + click a note: normal -> **legato** (blue, "L") -> **slide** (amber, "S") -> normal. |
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

**Legato and slide.** Two notes of the same channel can be joined: with **legato** the sound that is playing goes on and only its pitch
jumps to the new note (no new attack, as on a monosynth); with **slide** the pitch glides to the new note in at most an eighth note
(or the note's length, if shorter). The line drawn from the old note to the new one shows the join. For it to work the note before must
still be sounding when the linked note starts, so the two should touch or overlap (turning legato on lengthens a note that stops up to
a 16th early; a note that stops earlier leaves a gap and the linked note then just plays normally, with its own attack). It works on synth
and sampler channels and is saved in the project.

Notes snap to 16th notes. A pattern is two bars long. A synth note ends at its length or at the end of the pattern,
whichever comes first. When you open a channel the view scrolls to its notes.

## Patterns

A **pattern** holds the notes of every channel. The pattern box in the toolbar selects the current pattern: the
Channel Rack and the Piano Roll show and edit that pattern. The **+** button next to it offers **New empty pattern**
and **Clone current pattern**. New patterns get the next name automatically (Pattern 3, Pattern 4, ...). The demo project starts
with "Pattern 1" (the full groove) and "Drums" (the drums only), played with the Cyber-Hub sounds (see
[cyber-hub.md](cyber-hub.md)).

The **pattern box** at the top of the Playlist has three buttons: **New** (empty or cloned, like the **+**), **Rename**
(a name of up to 40 characters) and **Delete**. Delete removes the selected pattern, its notes and **every playlist clip
that plays it**; the last remaining pattern cannot be deleted. Choosing a pattern in that box also opens it in the Rack and
the Piano Roll.

Channel settings (volume, pan, mute, solo, pitch, swing, instrument, sample, mixer track) belong to the channel, not
to the pattern: they are the same in every pattern. Mixer tracks and their effects belong to neither.

## Undo and redo of notes

**Ctrl+Z** undoes and **Ctrl+Y** (or Ctrl+Shift+Z) redoes the last edit of notes - in the Piano Roll, the step sequencer, *Clear notes*, or a
whole take of **Rec notes** (one step). With the mouse over the Playlist the same keys undo the Playlist's own edits instead. The File menu
has the two commands too. The last 200 edits are kept. Not covered: volume, pan, pitch and the other channel settings, the mixer, adding
or deleting patterns and channels; opening a project or deleting a pattern forgets the history.

## Importing a MIDI file

*File > Import a MIDI file into patterns...* (or drop a `.mid` / `.midi` file on the Playlist) turns a Standard MIDI File into **new synth
channels** (one per track and MIDI channel that has notes, named after the track) and **new patterns** of two bars each, placed one after the
other on playlist track 1 after the existing clips. Long notes that cross the end of a pattern are cut there and sound again at the start of
the next pattern. The first tempo of the file sets the BPM. The status line reports what was done and what was left out.

Left out: General MIDI **drum** tracks (MIDI channel 10), tempo changes after the first, controllers, pitch bend, program changes; tracks that
do not fit in the free channels (there are 8 in all); files over 8 MB or with more than 200 000 notes; files timed in SMPTE frames. The
imported channels are plain synths: give them a sampler or another sound afterwards.

## Playlist

The Playlist arranges patterns in time. It has 8 tracks and a timeline of 32 pattern lengths. The ruler shows bar
numbers (a pattern is two bars, so the numbers go 1, 3, 5, ...). The orange vertical line marks the end of the song:
in Song mode playback loops there.

| Action | How |
| --- | --- |
| Place a clip (Select tool) | Click an empty cell. The clip holds the pattern selected in the pattern box and is one pattern long. Drag right straight away to make it longer. |
| Move a clip | Drag it (it snaps to pattern boundaries and can change track). |
| Repeat / shorten | Drag the right edge of the clip: the pattern repeats once per pattern length. |
| Delete | Right-click the clip > Delete, or select it and press Delete (or use the Erase tool). |
| Select its pattern | Double-click the clip. |
| Scroll time | Mouse wheel. |
| Zoom | Ctrl + wheel. |

### Tools, grid, zoom and undo

The toolbar under the pattern box has five tools (keys **1** - **5** while the mouse is over the timeline):

| Tool | What it does |
| --- | --- |
| **Select** (1) | The normal tool described above and below: click a clip to select it, drag to move it, drag its **left or right edge** to trim or lengthen it (an audio clip keeps its sound in place: only more or less of it is heard), right-click for a menu (*Split here, Duplicate, Copy, Delete*, plus *Position, length and gain...* on an audio clip or *Open this pattern* on a pattern clip), double-click a pattern clip to open that pattern. |
| **Split** (2) | Click a clip to cut it in two at that point (snapped to the grid). Key **S** splits the selected clip at the mouse. |
| **Cut** (3) | Drag across a clip to remove that stretch of it: the sound after the gap stays where it was. |
| **Erase** (4) | Click a clip to delete it, or drag across several. |
| **Zoom** (5) | Click zooms in, right-click or Shift+click zooms out, drag a stretch to zoom to it. |

**Grid** (the box next to the tools) is what audio clips snap to when moved, trimmed, split or cut: from a bar down to a
single tick (1 beat = 480 ticks); hold **Alt** while dragging to ignore it. Pattern clips always sit on whole patterns.
**-**, **+** and **Fit** zoom out, in and to the whole song; **Ctrl+wheel** zooms around the mouse and the wheel and the
scroll bar move in time. The ruler shows **minutes:seconds.milliseconds** and the bar numbers (only as many as fit at the
current zoom), and the two lines under the timeline read out, for the position under the mouse, *time - bar:beat:tick*, and
for the selected clip its name, start, length and end (each as bar:beat:tick and as a time), for an audio clip also the length and
sample rate of the file, where in the file the clip starts (in seconds and in samples) and its gain. On an audio clip, *Position,
length and gain...* (right-click) lets you type exact values.

**Undo / Redo** (buttons, **Ctrl+Z** and **Ctrl+Y**) cover everything done to clips in the Playlist. **Ctrl+C / X / V** copy,
cut and paste the selected clip at the mouse and **Ctrl+D** duplicates it; **Delete** removes it. The undo history is
forgotten when something outside the Playlist changes the arrangement (opening a project, a new recording, deleting a pattern).
Split and Cut also work on pattern clips, but only on whole pattern lengths. A clip made by splitting an audio clip remembers where it
starts in the sound **in seconds, at the BPM it was cut at**: if you change the BPM afterwards the sound still plays at its
original speed, so the cut points no longer fall on the beat they were cut on.

Clips cannot overlap on the same track; a move or resize that would overlap is simply not applied. Clips on
different tracks play together. Clicking a track's **name** chooses the track that **Record** writes to (marked with a
red dot).

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

The arrow next to **Record** has *The recording becomes*: **A clip on a playlist track** (the default) or **A new sampler
channel**. With the second, an audio take from a microphone or guitar becomes the sample of a new channel, ready to be played with
a MIDI keyboard or sequenced in the piano roll (with no free channel it is put on the playlist instead).

## Pattern mode and Song mode

The **Pattern / Song** button decides what Play does.

- **Pattern**: loops the current pattern. The playhead runs in the Piano Roll and the Rack.
- **Song**: plays the Playlist from the start to the end of the last clip, then loops. The playhead runs in the
  Playlist. An empty playlist is silent.

You can edit while playing in either mode; the change is heard immediately.

## Automation

Automation makes a control move by itself while the song plays. It works on:

- a channel's **volume** and **pan** (the two sliders in the Channel Rack);
- a track's **fader** and **balance** (Mixer);
- every **parameter of an effect** in a track slot or in a master slot (the sliders of the effects panel).

**Not automatable yet:** the pitch and swing knobs, the master volume, mute and solo, the sends, the aux buses and VCA groups
(including the parameters of effects on an aux bus), and anything else.

**Create a clip.** Right-click the control and choose *Create automation clip*. A clip one pattern long appears on one of the four
**automation lanes** (*Auto 1-4*, under the eight tracks of the Playlist) and its editor opens. The clip is a flat line at the
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

### Sends, aux buses and VCA groups

Under its effect slots every track has:

- **A** and **B**: how much of the track is sent to **Aux A** and **Aux B** (all the way left: nothing; 1.5 at the far right). Double-click
  a slider to switch the send off;
- **Pre**: when on, the sends take the sound **before** the fader, the balance and the VCA (so the send level does not follow the
  fader). Off (the default), they are **post-fader**. A muted track, or a track silenced by another track's solo, sends nothing either way;
- **V1** and **V2**: put the track in VCA group 1 and/or 2.

**Aux buses** (the two strips after the tracks, with a green bar) sum what the tracks send them, run it through their own four effect slots
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

- Up to 8 channels, 8 mixer tracks with 4 effect slots each, 2 aux buses (4 slots each), 2 VCA groups, 64 patterns, 8 playlist tracks, a 32-pattern-long
  timeline, 4 automation lanes, 64 automation clips of up to 256 points each, 32 simultaneous voices.
- The effects use about 18 MB of memory at 48 kHz (more at higher sample rates).
- A pattern is always two bars long, 4/4, 16th-note grid.
- Samples are mono or stereo, at most 30 seconds, played one-shot (no looping, no envelope editing). Audio clips: up to
  5 minutes per file, 64 clips.
- Not available yet: playing notes from the computer keyboard, undo of channel settings and the mixer, embedding samples in the project, automatic saving
  and crash recovery, VST plugins as instruments or effects, automation of anything except volumes, pans and effect parameters, turning guitar or voice pitch into notes,
  punch-in and count-in for recording, fades and per-clip effects, seeking by clicking the ruler. See [roadmap.md](roadmap.md).
