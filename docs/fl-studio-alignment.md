# FL Studio ↔ Sun Loops Studio: window-by-window alignment (0.60.0)

Written on 2026-11-12 from Image-Line's online manual (*Basics > Workflow*, *Toolbar panels*, *Channel Rack*, *Piano Roll*,
*Playlist*, *Mixer*, *Plugin picker*, *Browser*). Each table says what FL Studio has, what Sun Loops Studio has now, and what
still differs. "0.60.0" marks what this release added or moved.

## Workflow: how the windows connect

FL Studio's data flow, which Sun Loops Studio follows one for one:

```
Browser / Plugin picker ──drag──▶ Channel Rack (instruments, one row per channel)
                                       │  each channel ──▶ a Mixer insert track (the Track N box)
                                       │  steps / notes of the CURRENT PATTERN
                                       ▼
                                  Piano Roll (the notes of one channel in that pattern; target channel box)
                                       ▼
                                  Playlist (pattern clips, audio clips, automation clips on tracks) ── Song mode
                                       ▼
                                  Mixer (insert tracks → aux / VCA → Master) ──▶ output / export
```

| Connection in FL Studio | Sun Loops Studio |
| --- | --- |
| Pattern / Song switch in the transport | Same button (Pattern / Song) |
| Pattern selector in the toolbar; the Channel Rack and Piano Roll show that pattern | Same; the Playlist's *Pattern* box is the same selector |
| Channel → Piano Roll (double-click the channel button / the step preview) | Double-click the name button or click the step preview |
| Channel → Mixer insert (the mixer-track box on the channel) | *Track N* box (click for a menu, drag or wheel); *Assign a free mixer track* in the ≡ menu |
| Pattern → Playlist (draw the selected pattern with the Draw tool) | Same (Draw tool, the Playlist's *Pattern* box) |
| Browser → Channel Rack (drag an instrument / sample) | Same; also from the Plugin picker (0.60.0) |
| Plugin picker → Mixer slot (drag an effect) | Double-click or *Add to the selected track* (0.60.0); drag from the browser's Plugin node onto a channel |
| Global snap in the toolbar, editors follow with "Main" | 0.60.0: *Snap* panel in the toolbar; the Piano Roll's and the Playlist's snap boxes have *Main* |

## Toolbar panels

| FL Studio | Sun Loops Studio | Note |
| --- | --- | --- |
| Menu bar: FILE, EDIT, ADD, PATTERNS, VIEW, OPTIONS, TOOLS, HELP | Same eight menus (0.60.0) | TOOLS holds Helios, Loop Starter, Slicer, imports, FLP exchange, plugin scan |
| Hint panel | Hint bar next to the menus (0.60.0) | Shows the tooltip of the control under the mouse |
| Transport: Play, Stop, Record, Pattern / Song | Play, Stop (0.60.0, separate), Record + options, Rec notes, Pattern / Song | |
| Pattern panel: previous / next, selector | ◂ ▸ arrows (0.60.0), box, + | |
| Tempo panel | BPM slider + marker dot, time signature | |
| Time panel (bar:step:tick / min:sec) | bar.beat.tick and min:sec (0.60.0) | |
| Snap panel | Global snap box (0.60.0) | |
| Master volume / pitch | Master volume + meter | No master pitch |
| CPU / memory panel | CPU % (0.60.0) | No memory read-out |
| Shortcut panel: Playlist, Piano roll, Channel rack, Mixer, Browser, Plugin picker | Same six, same order (0.60.0) | F5 F7 F6 F9 F8 as in FL Studio |
| Recording panel: metronome, count-in, loop record, step edit, typing keyboard | Metronome, Typing keys, the Record options menu (count-in, punch, loop takes) | No step-edit mode |
| Online panel | – | Not planned |

## Channel Rack

| FL Studio (left to right) | Sun Loops Studio (0.60.0) |
| --- | --- |
| Channel options menu (top-left) | ≡ button: rename, clone, delete, move, assign track, piano roll, add channel |
| Pattern length / swing in the top bar | Pattern length with − / + (swing is per channel, a knob on the row) |
| Mute LED | M (and S) |
| Pan knob | Pan slider |
| Volume knob | Volume slider |
| Mixer track box | Track N box |
| Channel button (name; click selects, double-click opens, right-click menu) | Name button, lit when selected |
| 16 step buttons per bar, groups of 4 coloured | Step squares or Piano Roll preview, groups of 4 |
| Graph editor / keyboard editor | Velocity lane of the Piano Roll |
| Channel filter groups | – (not yet) |

## Piano Roll

| FL Studio | Sun Loops Studio |
| --- | --- |
| Target channel in the title bar | Target channel box at the left of the toolbar (0.60.0) |
| Tools: Draw, Paint, Delete, Mute, Slip, Slice, Select, Zoom, Playback | One tool with modifiers (click draws, drag moves, right-click deletes, edges resize, rectangle selects); no Mute / Slip / Slice / Playback tools for notes |
| Snap selector ("Main" follows the toolbar) | Snap box with *Main* (0.60.0) |
| Ghost channels | Ghost switch (0.60.0) |
| Stamp / chord tool, scale highlighting | Chord box, scale and snap-to-scale |
| Event editor lane (velocity, pan, release, pitch, mod) | Velocity lane (resizable) |
| Tools menu: quantize, arpeggiate, chop, strum, randomize, riff machine, legato | Tools and Generate menus (quantize, legato, strum, arpeggio, humanise, chords, riffs, bassline) |
| Slide / portamento notes | Slide and legato notes |

## Playlist

| FL Studio | Sun Loops Studio |
| --- | --- |
| Tools: Draw, Paint, Delete, Mute, Slip, Slice, Select, Zoom, Playback | Draw, Delete, Mute (0.60.0), Slip (0.60.0), Slice, Cut, Select, Zoom (keys 1-8); no Paint / Playback |
| Picker panel (patterns, audio, automation) | The *Pattern* box and the browser; audio by drag or import |
| Track headers: name, mute, solo, lock, colour | Name, M / S (0.60.0), record dot, colour, pattern-bound tracks |
| Time ruler with markers and loop region | Ruler with loop region and tempo / time-signature markers |
| Clip types: pattern, audio, automation | Same three |
| Snap ("Main") | Grid box with *Main* (0.60.0) |

## Mixer

| FL Studio | Sun Loops Studio |
| --- | --- |
| Master + 125 insert tracks, each with fader, pan, stereo separation, mute, record arm, routing | Master + tracks, aux buses, VCA groups; fader, pan, width, mute, solo, routing |
| 10 effect slots per track with mix knob and on/off | 10 slots with bypass, mix and the built-in / plugin effects |
| EQ panel per track | Parametric EQ as an effect |
| Sidechain sends | Compressor sidechain |

## Browser and Plugin picker

| FL Studio | Sun Loops Studio |
| --- | --- |
| Browser tree: samples, presets, plugins, project files; drag to rack / playlist | Browser: Samples, Drum Kits, Instruments, Presets, Plugin; drag to rack / playlist |
| Plugin picker (F8): instruments and effects side by side, search, drag or double-click | Plugin picker window (0.60.0): the same two lists, search, double-click / drag; *Scan plugins again* |

## Still different, worth a later block

- Piano Roll note tools as separate tools (Mute, Slip, Slice, Paint) — the roll works with modifiers instead.
- Playlist Paint tool (repeating clips while dragging) and Playback tool (audition).
- Channel filter groups in the Channel Rack.
- Memory read-out next to the CPU panel; master pitch.
- Step-edit recording mode.
