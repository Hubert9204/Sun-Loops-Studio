# FL Studio ↔ Sun Loops Studio: review and gap analysis (0.62.0)

Written on 2026-11-12 from Image-Line's online manual (*Basics > Workflow*, *Toolbar panels*, *Channel Rack*, *Piano Roll*,
*Playlist*, *Mixer*, *Plugin picker*, *Browser*, and the pages of the native instruments and effects) and from the code of this
repository. Every module is compared control by control with its FL Studio counterpart.

Status: **✔** present and equivalent · **◐** present but narrower than FL Studio's · **✘** missing. "0.60" / "0.61" / "0.62"
mark what those releases added. Section 12 lists what 0.62.0 implemented (the whole "still open" list of 0.61.0), section 13
what is still narrower than FL Studio's or missing.

This is not full parity: FL Studio is two decades of work by a large team. The aim of 0.60 - 0.62 was that every window has
the same layout, tools and connections, that every *category* of native effect has a counterpart, and that the engine can do
what those tools need (per-note properties, free routing, delay compensation, internal controllers).

## 1. Workflow: how the windows connect

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

| Connection in FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Pattern / Song switch in the transport | Same button | ✔ |
| Pattern selector; the Channel Rack and Piano Roll show that pattern | Same; the Playlist's *Pattern* box is the same selector | ✔ |
| Channel → Piano Roll | Double-click the name button or click the step preview; the roll's target channel box (0.60) | ✔ |
| Channel → Mixer insert | *Track N* box; *Assign a free mixer track* | ✔ |
| Pattern → Playlist (Draw / Paint the selected pattern) | Draw, and Paint (0.61) | ✔ |
| Browser → Channel Rack / Playlist (drag) | Same; also from the Plugin picker (0.60) | ✔ |
| Plugin picker → Mixer slot | Double-click or *Add to the selected track* (0.60) | ◐ no drag onto a slot |
| Mixer track → mixer track (routing cables) | The routing button on every strip (0.62) | ✔ a menu, not cables |
| Global snap, editors follow with "Main" | Snap panel + *Main* in both editors (0.60) | ✔ |
| Link a control to an automation clip / controller | Automation clips for volumes, pans, effect and plugin parameters; Peak Controller for effect and plugin parameters (0.62) | ◐ no Formula / Envelope controller; a controller drives one parameter |

## 2. Toolbar

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Menu bar FILE … HELP | Same eight menus (0.60) | ✔ |
| Hint panel | Hint bar (0.60) | ✔ |
| Transport: Play, Stop, Record, Pattern / Song | Same; Record options, Rec notes | ✔ |
| Pattern panel (previous / next, selector) | Same (0.60) | ✔ |
| Tempo, tap tempo, time signature | BPM, tap, markers, time signature | ✔ |
| Time panel | bar.beat.tick + min:sec (0.60) | ✔ |
| Global snap | Same (0.60) | ✔ |
| Master volume | Same + meter | ✔ |
| Master pitch | *Pitch* in the toolbar, ±12 semitones (0.62) | ◐ whole semitones; drum channels are not moved |
| CPU / memory / polyphony | CPU % (0.60), RAM (0.62, Linux) | ◐ no polyphony count |
| Shortcut panel | Same six windows, same order (0.60) | ✔ |
| Recording panel: metronome, count-in, loop record, typing keyboard, step editing | Metronome, Typing keys, Record options, *Step edit* (0.62) | ◐ no blend / overdub switch |
| Undo history window | Undo / redo (one list per editor) | ◐ |

## 3. Channel Rack

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Row: mute, pan, volume, mixer track, channel button, steps | Same order (0.60); plus solo, pitch, swing, meter | ✔ |
| Channel options menu | ≡ menu (0.60) | ✔ |
| Step menu: fill each 2 / 4 / 8 steps, clear | *Steps* submenu (0.61) | ✔ |
| Step sequencer ↔ piano roll preview | Same | ✔ |
| Pattern length, swing | Length in the bar; swing per channel | ◐ no global swing |
| Graph editor (per-step velocity, pan, pitch…) | *Graph*: velocity, pan, release, fine pitch per step (0.62) | ◐ no shift / mod X / mod Y |
| Channel filter groups, channel colours and icons | Filter groups and colours (0.62) | ◐ no icons |
| Rotate / shift steps, randomize steps | – | ✘ |
| Clone, rename, delete, move | Same | ✔ |
| Layer channel, Audio clip channel, Automation clip channel | Layer channel (0.62); audio and automation clips live on the Playlist | ◐ |

## 4. Channel settings (Sampler channel)

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Sample load, root note | Load sample, Root key | ✔ |
| Reverse, SMP START, IN / OUT fades | Reverse, Start, Fade in, Fade out (0.61) | ✔ |
| Normalize, remove DC, reverse polarity, swap stereo (precomputed) | In the sample editor (Normalise, Remove DC, Invert) | ◐ not as channel switches |
| Loop: sustain loop points, ping-pong, crossfade | Forward and ping-pong loop with start / end, crossfade (0.62) | ✔ |
| Volume envelope (delay, attack, hold, decay, sustain, release) | ADSR | ◐ no delay / hold |
| Envelopes and LFOs for pan, cutoff, resonance, pitch | One LFO (to pitch, cutoff, pan or volume), a pitch envelope, the volume envelope to the cutoff (0.62) | ◐ one LFO and no envelope of its own per target; nothing on the resonance |
| Time stretching on the channel (modes, pitch, mul, time) | On audio clips (stretch and pitch), not on sampler channels | ◐ |
| Polyphony: max voices, mono, portamento / slide time | Maximum notes, Mono, Portamento in the channel's menu (0.62); slide and legato notes | ✔ |
| Cut groups (cut / cut by) | Cut group / Cut by group in the channel's menu (0.62); choke groups on drum pads | ✔ |
| Arpeggiator, echo delay / fat mode, time shift, swing mix | Arpeggiate tool in the Piano Roll (writes notes) | ◐ |
| Keyboard range, velocity → volume tracking | Zones of SoundFont / SFZ programs | ◐ |
| DirectWave-style multi-sample zones | SoundFont (.sf2) and SFZ programs, slices | ✔ |

## 5. Native instruments

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| 3x Osc | Synth, Subtractive engine (2 oscillators + noise, filter, 2 envelopes, LFO) | ◐ 2 oscillators, no phase / stereo detune per oscillator |
| Sytrus (FM) | FM engine; HexForge (6-operator FM plugin) | ◐ |
| Harmless / Harmor (additive) | Additive engine | ◐ no resynthesis, no image synthesis |
| Plucked!, Sakura (physical) | Physical engine | ◐ |
| FPC, Fruit Kick, Drumaxx | Drum machine (16 pads, kits, choke groups) | ◐ no drum synthesis |
| Fruity Slicer, Slicex | Slicer | ◐ |
| FLEX, FL Keys, BooBass | Factory presets, Grand Piano, Electric Bass | ◐ |
| Wavetable / supersaw synths (Sawer, Poizone, Toxic…) | SunBurst plugin (wavetable, unison, arpeggiator) | ◐ |
| Speech synth, Granulizer, Wave Traveller | – | ✘ |

## 6. Piano Roll

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Target channel | Target channel box (0.60) | ✔ |
| Ghost channels | Ghost switch (0.60) | ✔ |
| Snap with "Main" | Same (0.60) | ✔ |
| Tools: Draw, Paint, Delete, Mute, Slip, Slice, Select, Zoom, Playback | Draw, Paint, Mute, Slice (0.62); right-click deletes, a rectangle selects | ◐ no Slip / Playback tool |
| Stamp (chords), scale highlighting, snap to scale | Chord box with inversions, Scale, Snap to scale | ✔ |
| Event lane: velocity | Velocity lane | ✔ |
| Event lanes: pan, release, fine pitch, mod X / Y per note | Pan, Release, Fine pitch lanes, played by every instrument (0.62) | ◐ no mod X / Y |
| Note colours / MIDI channels | 16 note colours (0.62) | ◐ colours do not select a MIDI channel |
| Slide and portamento notes | Slide and legato notes | ✔ |
| Tools: quantize, legato / articulate, strum, arpeggiate, chop, flip, randomize, scale levels | Quantize, Humanize, Legato, Staccato, Strum, Arpeggiate, Chop, Flip, Velocity tools | ✔ |
| Tools: glue, flam, flip horizontally | Glue, Flam, Reverse (0.61) | ✔ |
| Riff machine, score generators | Generate menu (chords, riffs, bassline) | ◐ |
| Limit, LFO tool, claw machine, key-change, score logger | Limit, LFO on the lane shown (0.62); score log (*Dump the score log*) | ◐ no claw machine |
| Time markers, loop region inside the roll | Loop region | ◐ |

## 7. Playlist

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Tools: Draw, Paint, Delete, Mute, Slip, Slice, Select, Zoom | Draw, Paint (0.61), Delete, Mute (0.60), Slip (0.60), Slice, Cut, Select, Zoom | ✔ |
| Playback tool (scrub) | Drag along the ruler | ◐ |
| Clip types: pattern, audio, automation | Same three | ✔ |
| Track headers: name, colour, mute, solo | Same; M / S (0.60), record dot | ✔ |
| Track lock, group with the track above, resize, icons | Lock, group, fold a group (0.62) | ◐ one row height for all tracks, no icons |
| Time markers (named), jump to marker, marker actions | Markers, ◂ ▸ (0.61); actions Loop, Skip, Pause, Start (0.62) | ◐ no punch in / out or time-signature markers as actions |
| Loop region, tempo and time-signature changes | Same | ✔ |
| Picker panel | The *Pattern* box and the browser | ◐ |
| Clip menu: make unique, consolidate / bounce, chop | Make unique, Consolidate to audio (clip or track) (0.62), mute, split, takes | ◐ no chop |
| Audio clip stretch, pitch, fades, reverse | Stretch and pitch, sample editor, pitch editor, stems | ✔ |
| Performance (live) mode | Performance window: launch pattern clips on the bar, scenes, stop (0.62) | ◐ mouse only (no MIDI controller mapping), pattern clips only, no per-clip trigger modes |
| Snap with "Main" | Same (0.60) | ✔ |

## 8. Mixer

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Master + insert tracks | Master + up to 500 tracks | ✔ |
| Fader, pan, mute, solo, meter | Same (peak + RMS meter) | ✔ |
| Stereo separation, polarity, swap left / right | Separation slider, Ø, ⇄ (0.61) | ✔ |
| 10 effect slots with mix knob and on / off | 10 slots with bypass; mix inside each effect | ◐ no per-slot mix knob for every effect |
| Free routing of any track to any track, with send level | Routing button: to the master and / or up to four tracks, each with a level (0.62); aux buses, VCA groups | ◐ four sends per track; sends are post-fader |
| Sidechain to any track | Compressor, Gate (0.61) and Vocoder (0.62) listen to any track | ✔ |
| Per-track EQ (3 bands) in the mixer | Equalizer / Parametric EQ as effects | ◐ |
| Track delay compensation, track latency | Automatic, from the Maximizer's look-ahead and the latency plugins report; exports trimmed (0.62) | ◐ no manual track delay; the aux buses' own latency is not compensated |
| Track recording (arm, input select) | Recording on Playlist tracks | ◐ |
| Track colours, icons, docking left / right, separators | – | ✘ |
| Effect chain presets (mixer track states) | Chain presets (.slchain) | ✔ |

## 9. Native effects

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Fruity Parametric EQ 2 (7 bands) | Parametric EQ 2 (0.61: 7 bands) | ◐ fixed band types, no spectrum display |
| Fruity Parametric EQ, EQUO | Equalizer, Parametric EQ | ✔ |
| Fruity Filter, Fruity Love Philter | Filter (4 types, 12 / 24 dB, drive, LFO) | ◐ |
| Fruity Bass Boost | Bass Boost (0.61) | ✔ |
| Fruity Compressor | Compressor (with sidechain) | ✔ |
| Fruity Limiter (limiter + compressor + gate) | Maximizer, Compressor, Gate (0.61) | ✔ |
| Fruity Multiband Compressor, Maximus | Multiband Compressor (0.61: 3 bands) | ◐ no per-band envelopes / saturation |
| Soundgoodizer | Maximizer presets | ◐ |
| Transient Processor | Transient | ✔ |
| Fruity Fast Dist, Blood Overdrive, Soft Clipper | Overdrive (4x oversampled), Maximizer soft clip | ✔ |
| Fruity WaveShaper, Distructor | WaveShaper (0.62: six curves, bias, 4x oversampled) | ◐ fixed curves, no drawn curve |
| Fruity Squeeze | Squeeze (0.61) | ✔ |
| Fruity Chorus | Chorus | ✔ |
| Fruity Flanger, Flangus | Flanger (0.61) | ✔ |
| Fruity Phaser | Phaser | ✔ |
| Fruity Vibrator | Vibrato (0.61) | ✔ |
| Fruity Reeverb 2 | Reverb (Hall, Room, Plate, pre-delay) | ✔ |
| Fruity Convolver | Convolver (0.62: five built-in spaces or an audio file, no latency) | ◐ responses up to 4 s, no response editing |
| Fruity Delay 3, Delay Bank | Delay (stereo, ping-pong, tape) | ◐ |
| Gross Beat | Time Shaper | ✔ |
| Pitcher | Pitch Corrector | ✔ |
| NewTone, Edison | Pitch editor, Sample editor | ✔ |
| Fruity Stereo Enhancer, Stereo Shaper | Stereo Imager | ✔ |
| Fruity Balance, Panomatic | Panomatic (0.61) | ✔ |
| Pitch Shifter, Frequency Shifter | Pitch on audio clips | ◐ |
| Vocodex / Fruity Vocoder | Vocoder (0.62: 8 / 12 / 16 bands, the voice from another track) | ◐ no unvoiced / noise path, no band editor |
| Fruity Granulizer, Scratcher | – | ✘ |
| Peak Controller, Formula Controller, Envelope Controller | Peak Controller (0.62: level + LFO to one parameter of an effect or plugin) | ◐ no Formula / Envelope controller |
| Spectroman, dB Meter, Wave Candy | Meters window | ✔ |
| Patcher, Effector, Hyper Chorus, Vintage Chorus / Phaser | – | ✘ |

## 10. Browser and Plugin picker

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Tree: packs, presets, plugins, current project | Current project (0.62), Samples, Drum Kits, Instruments, Presets, Plugin | ✔ |
| Search | Search box | ✔ |
| Preview on click | Preview on click | ✔ |
| Drag to Channel Rack / Playlist | Same | ✔ |
| Tabs, favourites, tags, coloured folders | Tabs All / Project / Plugins / ★, favourites (0.62) | ◐ no tags or folder colours |
| Plugin picker | Plugin picker window (0.60) | ✔ |
| Plugin database with categories and favourites | Instruments / Effects lists from the scan | ◐ |

## 11. What 0.61.0 implemented

- Effects: Parametric EQ 2, Bass Boost, Multiband Compressor, Gate, Squeeze, Flanger, Vibrato, Panomatic.
- Mixer tracks: stereo separation, polarity, swap left / right.
- Sampler: reverse, start, fade in, fade out.
- Playlist: Paint tool, named time markers with previous / next.
- Piano Roll tools: Reverse, Glue, Flam.
- Channel Rack: fill each 2 / 4 / 8 steps, fill all, clear.
- Engine: up to 24 parameters per effect; a scrolling parameter pane; the gate's sidechain.

## 12. What 0.62.0 implemented

Every point of the "still open" list of 0.61.0, in its order:

1. **Per-note pan, release and fine pitch**: three more event lanes in the Piano Roll, saved with the notes and played by the
   synths, the samplers and the drum machine.
2. **Sampler**: an LFO (pitch, cutoff, pan or volume), a pitch envelope, envelope to cutoff, ping-pong and crossfade loops;
   **channels**: cut groups, mono, portamento, maximum polyphony.
3. **Free mixer routing** (a track to the master and / or to four other tracks, each with a level; tracks are processed in the
   order the routing asks for, loops are broken) and **plugin delay compensation** (per track, through buses too; exports are
   trimmed by the mix's latency).
4. **Peak Controller**: the level of its track and an LFO drive a parameter of any built-in effect or plugin on any track.
5. **Convolver**, **WaveShaper**, **Vocoder**.
6. **Playlist**: track locking, grouping and folding; marker actions (Loop, Skip, Pause, Start); *Make unique*; *Consolidate to
   audio*; **performance mode** (a window of launchable clips).
7. **Channel Rack**: filter groups, colours, the graph editor, the Layer channel.
8. **Piano Roll**: Paint, Mute and Slice tools, note colours, the LFO and Limit tools.
9. **Browser**: favourites, tabs, the Current project node.
10. **Toolbar**: master pitch, memory read-out, step editing.

## 13. Still narrower than FL Studio's, or missing

What the tables above mark ◐ or ✘ after 0.62.0, the larger ones first:

1. **Performance mode** is driven by the mouse: there is no mapping of a MIDI controller's pads to the cells, no per-clip trigger
   and position-sync modes, and audio clips cannot be launched.
2. **Delay compensation** has no manual per-track delay, and the latency of effects on the two aux buses is not compensated.
3. **Sampler modulation**: one LFO and no separate envelope per target; the resonance cannot be modulated.
4. **Internal controllers**: only the Peak Controller (no Formula or Envelope controller), one parameter each.
5. **Native instruments** without a counterpart: speech synth, Granulizer, Wave Traveller, drum synthesis (Fruit Kick, Drumaxx);
   **effects**: Patcher, Effector, the vintage chorus / phaser models, frequency shifter, Scratcher.
6. **Looks**: channel and track icons, per-track row heights, tags and coloured folders in the browser, mixer track colours
   and docking.
7. Piano Roll: mod X / Y lanes, claw machine, a Playback tool. Playlist: chop, punch markers. Mixer: a mix knob per slot,
   pre-fader track sends, more than four sends per track.
