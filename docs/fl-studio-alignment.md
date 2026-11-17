# FL Studio ↔ Sun Loops Studio: review and gap analysis (0.74.0)

Written on 2026-11-12 from Image-Line's online manual (*Basics > Workflow*, *Toolbar panels*, *Channel Rack*, *Piano Roll*,
*Playlist*, *Mixer*, *Plugin picker*, *Browser*, and the pages of the native instruments and effects) and from the code of this
repository. Every module is compared control by control with its FL Studio counterpart.

Status: **✔** present and equivalent · **◐** present but narrower than FL Studio's · **✘** missing. "0.60" ... "0.74"
mark what those releases added. Section 12 lists what 0.62.0 implemented (the whole "still open" list of 0.61.0), section 13
what 0.70.0 implemented, section 14 what 0.71.0 - 0.74.0 implemented (the whole list still open after 0.70.0: nothing in the
tables is ✘ any more), section 15 the rows that remain narrower than FL Studio's.

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
| Link a control to an automation clip / controller | Automation clips for volumes, pans, effect and plugin parameters; Peak (0.62), Envelope and Formula (0.70) Controllers for effect and plugin parameters, each driving two targets (0.71) | ◐ two targets per controller |

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
| CPU / memory / polyphony | CPU % (0.60), RAM (0.62), polyphony (0.70) | ✔ |
| Shortcut panel | Same six windows, same order (0.60) | ✔ |
| Recording panel: metronome, count-in, loop record, typing keyboard, step editing | Metronome, Typing keys, Record options, *Step edit* (0.62); *Blend* (overdub) or replace (0.71) | ✔ |
| Undo history window | Undo / redo (one list per editor) | ◐ |

## 3. Channel Rack

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Row: mute, pan, volume, mixer track, channel button, steps | Same order (0.60); plus solo, pitch, swing, meter | ✔ |
| Channel options menu | ≡ menu (0.60) | ✔ |
| Step menu: fill each 2 / 4 / 8 steps, clear | *Steps* submenu (0.61) | ✔ |
| Step sequencer ↔ piano roll preview | Same | ✔ |
| Pattern length, swing | Length in the bar; swing per channel and a global swing (0.70) | ✔ |
| Graph editor (per-step velocity, pan, pitch…) | *Graph*: velocity, pan, release, fine pitch (0.62), shift, mod X, mod Y (0.71) per step | ✔ |
| Channel filter groups, channel colours and icons | Filter groups and colours (0.62), 16 icons (0.72) | ✔ |
| Rotate / shift steps, randomize steps | *Steps > Rotate left / right*, *Randomize* (0.70) | ✔ |
| Clone, rename, delete, move | Same | ✔ |
| Layer channel, Audio clip channel, Automation clip channel | Layer channel (0.62); audio and automation clips live on the Playlist | ◐ |

## 4. Channel settings (Sampler channel)

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Sample load, root note | Load sample, Root key | ✔ |
| Reverse, SMP START, IN / OUT fades | Reverse, Start, Fade in, Fade out (0.61) | ✔ |
| Normalize, remove DC, reverse polarity, swap stereo (precomputed) | In the sample editor (Normalise, Remove DC, Invert) | ◐ not as channel switches |
| Loop: sustain loop points, ping-pong, crossfade | Forward and ping-pong loop with start / end, crossfade (0.62) | ✔ |
| Volume envelope (delay, attack, hold, decay, sustain, release) | Delay, hold (0.70) and ADSR | ✔ |
| Envelopes and LFOs for pan, cutoff, resonance, pitch | Two LFOs (0.62, 0.70: to pitch, cutoff, resonance, pan or volume), a pitch envelope, the volume envelope to the cutoff; an ADSR envelope with an amount for each of pan, cutoff, resonance and pitch (0.71) | ✔ |
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
| FPC, Fruit Kick, Drumaxx | Drum machine (16 pads, kits, choke groups); the **Drum synth** engine (0.74: kick, snare, tom, clap, hi-hat, cymbal, playable on the keys) | ✔ |
| Fruity Slicer, Slicex | Slicer | ◐ |
| FLEX, FL Keys, BooBass | Factory presets, Grand Piano, Electric Bass | ◐ |
| Wavetable / supersaw synths (Sawer, Poizone, Toxic…) | SunBurst plugin (wavetable, unison, arpeggiator) | ◐ |
| FL Slayer (electric guitar) | SunRiff plugin (0.68): the same string types, coils, playing modes, glissando, amp, cabinets and effects unit | ✔ |
| Orchestral libraries (FLEX packs, DirectWave) | SunSymphony plugin (0.66 - 0.67): 16 parts, 29 free orchestral instruments downloaded from inside it | ◐ no loops on sustains, no dynamic crossfade yet |
| Speech synth, Granulizer, Wave Traveller | Speech synthesizer in the sampler window, the sampler's *Granular* and *Travel curve* play modes (0.74) | ✔ |

## 6. Piano Roll

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Target channel | Target channel box (0.60) | ✔ |
| Ghost channels | Ghost switch (0.60) | ✔ |
| Snap with "Main" | Same (0.60) | ✔ |
| Tools: Draw, Paint, Delete, Mute, Slip, Slice, Select, Zoom, Playback | Draw, Paint, Mute, Slice (0.62), Playback (0.71); right-click deletes, a rectangle selects | ◐ no Slip tool |
| Stamp (chords), scale highlighting, snap to scale | Chord box with inversions, Scale, Snap to scale | ✔ |
| Event lane: velocity | Velocity lane | ✔ |
| Event lanes: pan, release, fine pitch, mod X / Y per note | Pan, Release, Fine pitch (0.62), Mod X (cutoff) and Mod Y (resonance) (0.71) lanes, played by every instrument | ✔ |
| Note colours / MIDI channels | 16 note colours (0.62) | ◐ colours do not select a MIDI channel |
| Slide and portamento notes | Slide and legato notes | ✔ |
| Tools: quantize, legato / articulate, strum, arpeggiate, chop, flip, randomize, scale levels | Quantize, Humanize, Legato, Staccato, Strum, Arpeggiate, Chop, Flip, Velocity tools | ✔ |
| Tools: glue, flam, flip horizontally | Glue, Flam, Reverse (0.61) | ✔ |
| Riff machine, score generators | Generate menu (chords, riffs, bassline) | ◐ |
| Limit, LFO tool, claw machine, key-change, score logger | Limit, LFO on the lane shown (0.62); Claw machine (0.70); score log (*Dump the score log*) | ✔ |
| Time markers, loop region inside the roll | Loop region | ◐ |

## 7. Playlist

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Tools: Draw, Paint, Delete, Mute, Slip, Slice, Select, Zoom | Draw, Paint (0.61), Delete, Mute (0.60), Slip (0.60), Slice, Cut, Select, Zoom | ✔ |
| Playback tool (scrub) | Drag along the ruler; the *Play* tool (0.71) | ✔ |
| Clip types: pattern, audio, automation | Same three | ✔ |
| Track headers: name, colour, mute, solo | Same; M / S (0.60), record dot | ✔ |
| Track lock, group with the track above, resize, icons | Lock, group, fold a group (0.62); four row heights and an icon per track (0.72) | ✔ |
| Time markers (named), jump to marker, marker actions | Markers, ◂ ▸ (0.61); actions Loop, Skip, Pause, Start (0.62), Punch in / out (0.71) | ◐ no time-signature markers |
| Loop region, tempo and time-signature changes | Same | ✔ |
| Picker panel | The *Pattern* box and the browser | ◐ |
| Clip menu: make unique, consolidate / bounce, chop | Make unique, Consolidate to audio (clip or track) (0.62), Chop into pieces (0.70), mute, split, takes | ✔ |
| Audio clip stretch, pitch, fades, reverse | Stretch and pitch, sample editor, pitch editor, stems | ✔ |
| Performance (live) mode | Performance window: launch clips on the bar, scenes, stop (0.62); MIDI pads (0.70); audio clips, trigger modes Loop / One shot / Hold with position sync, a pad layout of your own (0.71) | ✔ |
| Snap with "Main" | Same (0.60) | ✔ |

## 8. Mixer

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Master + insert tracks | Master + up to 500 tracks | ✔ |
| Fader, pan, mute, solo, meter | Same (peak + RMS meter) | ✔ |
| Stereo separation, polarity, swap left / right | Separation slider, Ø, ⇄ (0.61) | ✔ |
| 10 effect slots with mix knob and on / off | 10 slots with bypass and a slot mix for every effect and plugin (0.70) | ✔ |
| Free routing of any track to any track, with send level | Routing button: to the master and / or up to eight tracks (0.70; four in 0.62), each with a level and pre- or post-fader (0.70); aux buses, VCA groups | ✔ a menu, not cables |
| Sidechain to any track | Compressor, Gate (0.61) and Vocoder (0.62) listen to any track | ✔ |
| Per-track EQ (3 bands) in the mixer | Equalizer / Parametric EQ as effects | ◐ |
| Track delay compensation, track latency | Automatic, from the Maximizer's look-ahead and the latency plugins report; exports trimmed (0.62); the aux buses too, and a track delay set by hand (0.70) | ✔ |
| Track recording (arm, input select) | Recording on Playlist tracks | ◐ |
| Track colours, icons, docking left / right, separators | Strip colours (0.70), icons, docking and separators (0.72) | ✔ |
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
| Pitch Shifter, Frequency Shifter | Pitch on audio clips; Frequency Shifter (0.70) | ✔ |
| Vocodex / Fruity Vocoder | Vocoder (0.62: 8 / 12 / 16 bands, the voice from another track) | ◐ no unvoiced / noise path, no band editor |
| Fruity Granulizer, Scratcher | Grains (0.70) on a mixer track, the sampler's *Granular* play mode (0.74), Scratcher (0.73) | ✔ |
| Peak Controller, Formula Controller, Envelope Controller | Peak Controller (0.62), Envelope and Formula Controllers (0.70), each to one parameter of an effect or plugin | ✔ |
| Spectroman, dB Meter, Wave Candy | Meters window | ✔ |
| Effector, Vintage Chorus / Phaser | Effector with its X / Y pad, Vintage Chorus, Vintage Phaser (0.70) | ✔ |
| Patcher, Hyper Chorus | Patcher (ten effects in series or in parallel in one slot) and Hyper Chorus (0.73) | ✔ |

## 10. Browser and Plugin picker

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Tree: packs, presets, plugins, current project | Current project (0.62), Samples, Drum Kits, Instruments, Presets, Plugin | ✔ |
| Search | Search box | ✔ |
| Preview on click | Preview on click | ✔ |
| Drag to Channel Rack / Playlist | Same | ✔ |
| Tabs, favourites, tags, coloured folders | Tabs All / Project / Plugins / ★, favourites (0.62), tags (search with #tag) and folder colours (0.72) | ✔ |
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

## 13. What 0.70.0 implemented

From the "still open" list of 0.62.0, in its order:

1. **Performance mode**: MIDI pads - the notes from C1 up launch the cells, eight to a row, the eighth stops the row.
2. **Delay compensation**: a track delay set by hand (0 - 200 ms) and the aux buses' own latency.
3. **Sampler modulation**: a second LFO with its own target, the resonance as an LFO target, the envelope's delay and hold.
4. **Internal controllers**: the Envelope Controller and the Formula Controller (a formula language of 20 functions, compiled
   once and run on the audio thread without allocating).
5. **Effects**: Frequency Shifter, Vintage Chorus, Vintage Phaser, Effector (ten effects, an X / Y pad) and Grains (granular).
6. **Looks**: mixer strip colours.
7. Piano Roll: claw machine. Playlist: chop. Mixer: a mix knob per slot, pre-fader track sends, eight sends per track.
   Channel Rack: rotate and randomize steps, a global swing. Toolbar: the polyphony count.

## 14. What 0.71.0 - 0.74.0 implemented

The whole list still open after 0.70.0, in its order (released together as 0.74.0):

1. **Native instruments** (0.74): the **Drum synth** engine (Fruit Kick / Drumaxx: kick, snare, tom, clap, hi-hat and cymbal,
   pitch sweep, noise, click, drive, playable on the keys, nine ready-made drums); the **speech synthesizer** (a formant
   synthesizer: text read the Italian way becomes the channel's sample, five voices, pitch, speed, intonation, saved as its text);
   the sampler's **Granular** play mode (the Granulizer as a channel: grain length, density, position, speed, spread, pan) and
   **Travel curve** play mode (Wave Traveller: forward, backward, tape stop, spin back, scratch, stutter, wobble).
   **Effects** (0.73): **Hyper Chorus** (up to eight voices), **Scratcher** (by hand or four patterns), **Patcher** (up to ten
   effects in series or in parallel in one slot, in a window of its own, latency compensated).
2. **Performance mode** (0.71): audio clips in the cells, trigger modes per clip (Loop, One shot, Hold) with position sync, a pad
   layout of your own (first note, pads per row).
3. **Sampler** (0.71): an ADSR envelope with an amount for each of pan, cutoff, resonance and pitch.
4. **Controllers** (0.71): a second target for the Peak, Envelope and Formula Controllers (optionally upside down).
5. **Looks** (0.72): 16 icons for channels, Playlist tracks and mixer tracks; four row heights per Playlist track; mixer tracks
   docked left / centre / right with separators; tags (searched with #tag) and coloured folders in the browser.
6. Piano Roll (0.71): Mod X and Mod Y lanes (cutoff and resonance of the note), the Playback tool. Playlist: the Playback tool,
   Punch in / out markers. Recording: the *Blend* (overdub) switch. Graph editor: shift, mod X, mod Y.

## 15. Still narrower than FL Studio's

Every row of the tables is now present; these rows remain ◐, mostly because FL Studio's version is deeper (more bands, more
operators, more editing), not because a tool is missing:

1. **Instruments**: 3x Osc has two oscillators, the FM and additive engines have no resynthesis, the physical and wavetable
   instruments are narrower than Sytrus / Harmor / Sakura; SunSymphony has no sustain loops or dynamic crossfades yet.
2. **Effects**: fixed band types and curves (Parametric EQ 2, WaveShaper), no per-band envelopes in the multiband compressor,
   Convolver responses up to 4 s, Vocoder without its noise path.
3. **Sampler channel**: no time stretching on the channel (it is on audio clips), the arpeggiator is a Piano Roll tool.
4. **Editing**: no Slip tool, no time-signature markers as actions, note colours are not MIDI channels; controllers drive two
   targets each; drag from the plugin picker onto a slot.
