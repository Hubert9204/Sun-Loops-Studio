# FL Studio ↔ Sun Loops Studio: review and gap analysis (0.61.0)

Written on 2026-11-12 from Image-Line's online manual (*Basics > Workflow*, *Toolbar panels*, *Channel Rack*, *Piano Roll*,
*Playlist*, *Mixer*, *Plugin picker*, *Browser*, and the pages of the native instruments and effects) and from the code of this
repository. Every module is compared control by control with its FL Studio counterpart.

Status: **✔** present and equivalent · **◐** present but narrower than FL Studio's · **✘** missing. "0.60" / "0.61" mark what
those releases added. The last section lists what is still open, in the order it would be worth doing.

This is not full parity: FL Studio is two decades of work by a large team. The aim of 0.60 and 0.61 was that every window has
the same layout, tools and connections, and that every *category* of native effect has a counterpart.

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
| Global snap, editors follow with "Main" | Snap panel + *Main* in both editors (0.60) | ✔ |
| Link a control to an automation clip / controller | Automation clips for volumes, pans, effect and plugin parameters | ◐ no internal controllers (Peak, Formula, Envelope) |

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
| Master pitch | – | ✘ |
| CPU / memory / polyphony | CPU % (0.60) | ◐ |
| Shortcut panel | Same six windows, same order (0.60) | ✔ |
| Recording panel: metronome, count-in, loop record, typing keyboard | Metronome, Typing keys, Record options | ◐ no step-edit, no blend / overdub switch |
| Undo history window | Undo / redo (one list per editor) | ◐ |

## 3. Channel Rack

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Row: mute, pan, volume, mixer track, channel button, steps | Same order (0.60); plus solo, pitch, swing, meter | ✔ |
| Channel options menu | ≡ menu (0.60) | ✔ |
| Step menu: fill each 2 / 4 / 8 steps, clear | *Steps* submenu (0.61) | ✔ |
| Step sequencer ↔ piano roll preview | Same | ✔ |
| Pattern length, swing | Length in the bar; swing per channel | ◐ no global swing |
| Graph editor (per-step velocity, pan, pitch…) | Velocity in the Piano Roll's lane | ◐ |
| Channel filter groups, channel colours and icons | – | ✘ |
| Rotate / shift steps, randomize steps | – | ✘ |
| Clone, rename, delete, move | Same | ✔ |
| Layer channel, Audio clip channel, Automation clip channel | Audio and automation clips live on the Playlist | ◐ no Layer |

## 4. Channel settings (Sampler channel)

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Sample load, root note | Load sample, Root key | ✔ |
| Reverse, SMP START, IN / OUT fades | Reverse, Start, Fade in, Fade out (0.61) | ✔ |
| Normalize, remove DC, reverse polarity, swap stereo (precomputed) | In the sample editor (Normalise, Remove DC, Invert) | ◐ not as channel switches |
| Loop: sustain loop points, ping-pong, crossfade | Forward loop with start / end | ◐ |
| Volume envelope (delay, attack, hold, decay, sustain, release) | ADSR | ◐ no delay / hold |
| Envelopes and LFOs for pan, cutoff, resonance, pitch | Filter (LP / HP / BP) with fixed cutoff and resonance | ✘ |
| Time stretching on the channel (modes, pitch, mul, time) | On audio clips (stretch and pitch), not on sampler channels | ◐ |
| Polyphony: max voices, mono, portamento / slide time | Slide and legato notes in the Piano Roll | ◐ |
| Cut groups (cut / cut by) | Choke groups on drum pads only | ◐ |
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
| Speech synth, Vocodex, Granulizer, Wave Traveller | – | ✘ |

## 6. Piano Roll

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Target channel | Target channel box (0.60) | ✔ |
| Ghost channels | Ghost switch (0.60) | ✔ |
| Snap with "Main" | Same (0.60) | ✔ |
| Tools: Draw, Paint, Delete, Mute, Slip, Slice, Select, Zoom, Playback | One tool with modifiers: click draws, drag moves, edges resize, right-click deletes, rectangle selects | ◐ no separate Paint / Mute / Slice / Playback tools |
| Stamp (chords), scale highlighting, snap to scale | Chord box with inversions, Scale, Snap to scale | ✔ |
| Event lane: velocity | Velocity lane | ✔ |
| Event lanes: pan, release, fine pitch, mod X / Y per note | – | ✘ |
| Note colours / MIDI channels | – | ✘ |
| Slide and portamento notes | Slide and legato notes | ✔ |
| Tools: quantize, legato / articulate, strum, arpeggiate, chop, flip, randomize, scale levels | Quantize, Humanize, Legato, Staccato, Strum, Arpeggiate, Chop, Flip, Velocity tools | ✔ |
| Tools: glue, flam, flip horizontally | Glue, Flam, Reverse (0.61) | ✔ |
| Riff machine, score generators | Generate menu (chords, riffs, bassline) | ◐ |
| Limit, LFO tool, claw machine, key-change, score logger | – | ✘ |
| Time markers, loop region inside the roll | Loop region | ◐ |

## 7. Playlist

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Tools: Draw, Paint, Delete, Mute, Slip, Slice, Select, Zoom | Draw, Paint (0.61), Delete, Mute (0.60), Slip (0.60), Slice, Cut, Select, Zoom | ✔ |
| Playback tool (scrub) | Drag along the ruler | ◐ |
| Clip types: pattern, audio, automation | Same three | ✔ |
| Track headers: name, colour, mute, solo | Same; M / S (0.60), record dot | ✔ |
| Track lock, group with the track above, resize, icons | – | ✘ |
| Time markers (named), jump to marker | Markers on the ruler, ◂ ▸ (0.61) | ◐ no marker actions (loop, skip, punch, song start) |
| Loop region, tempo and time-signature changes | Same | ✔ |
| Picker panel | The *Pattern* box and the browser | ◐ |
| Clip menu: make unique, consolidate / bounce, chop | Mute, split, takes, open pattern | ◐ |
| Audio clip stretch, pitch, fades, reverse | Stretch and pitch, sample editor, pitch editor, stems | ✔ |
| Performance (live) mode | – | ✘ |
| Snap with "Main" | Same (0.60) | ✔ |

## 8. Mixer

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Master + insert tracks | Master + up to 500 tracks | ✔ |
| Fader, pan, mute, solo, meter | Same (peak + RMS meter) | ✔ |
| Stereo separation, polarity, swap left / right | Separation slider, Ø, ⇄ (0.61) | ✔ |
| 10 effect slots with mix knob and on / off | 10 slots with bypass; mix inside each effect | ◐ no per-slot mix knob for every effect |
| Free routing of any track to any track, with send level | Two aux buses with sends (pre / post fader), VCA groups | ◐ |
| Sidechain to any track | Compressor and Gate (0.61) sidechain from any track | ✔ |
| Per-track EQ (3 bands) in the mixer | Equalizer / Parametric EQ as effects | ◐ |
| Track delay compensation, track latency | Maximizer look-ahead only | ✘ |
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
| Fruity WaveShaper, Distructor | – | ✘ |
| Fruity Squeeze | Squeeze (0.61) | ✔ |
| Fruity Chorus | Chorus | ✔ |
| Fruity Flanger, Flangus | Flanger (0.61) | ✔ |
| Fruity Phaser | Phaser | ✔ |
| Fruity Vibrator | Vibrato (0.61) | ✔ |
| Fruity Reeverb 2 | Reverb (Hall, Room, Plate, pre-delay) | ✔ |
| Fruity Convolver | – | ✘ |
| Fruity Delay 3, Delay Bank | Delay (stereo, ping-pong, tape) | ◐ |
| Gross Beat | Time Shaper | ✔ |
| Pitcher | Pitch Corrector | ✔ |
| NewTone, Edison | Pitch editor, Sample editor | ✔ |
| Fruity Stereo Enhancer, Stereo Shaper | Stereo Imager | ✔ |
| Fruity Balance, Panomatic | Panomatic (0.61) | ✔ |
| Pitch Shifter, Frequency Shifter | Pitch on audio clips | ◐ |
| Vocodex / Fruity Vocoder | – | ✘ |
| Fruity Granulizer, Scratcher | – | ✘ |
| Peak Controller, Formula Controller, Envelope Controller | – | ✘ |
| Spectroman, dB Meter, Wave Candy | Meters window | ✔ |
| Patcher, Effector, Hyper Chorus, Vintage Chorus / Phaser | – | ✘ |

## 10. Browser and Plugin picker

| FL Studio | Sun Loops Studio | |
| --- | --- | --- |
| Tree: packs, presets, plugins, current project | Samples, Drum Kits, Instruments, Presets, Plugin | ◐ no "current project" node |
| Search | Search box | ✔ |
| Preview on click | Preview on click | ✔ |
| Drag to Channel Rack / Playlist | Same | ✔ |
| Tabs, favourites, tags, coloured folders | – | ✘ |
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

## 12. Still open, in a suggested order

1. **Per-note pan, release and fine pitch** in the Piano Roll (new event lanes) and in the engine.
2. **Sampler envelopes and LFOs** for pan, cutoff, resonance and pitch; ping-pong and crossfade loops; channel cut groups; mono /
   portamento and maximum polyphony.
3. **Free mixer routing** (any track to any track with a level) and plugin delay compensation.
4. **Internal controllers** (Peak Controller first: an effect that drives another parameter).
5. **Convolution reverb**, **waveshaper**, **vocoder**.
6. Playlist: track grouping and locking, marker actions, *Make unique*, *Consolidate*, performance mode.
7. Channel Rack: filter groups, colours, graph editor, Layer channel.
8. Piano Roll: separate Paint / Mute / Slice tools, note colours, LFO and limit tools.
9. Browser: favourites, tabs, the current project's node.
10. Toolbar: master pitch, memory read-out, step-edit recording.
