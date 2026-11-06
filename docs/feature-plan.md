# Plan for the next feature blocks (written 2026-11-06, updated for 0.15.0)

Each block is a release of its own, in the order chosen by the maintainer. "Done" means what the repository has now.
Effort is a rough size: S = a few days of work, M = about two weeks, L = a month or more, XL = a long project of its own.

## Already in the program
**0.15.0** (not a block: ruler seeking and loop region, 32 channels with delete / move / clone, undo of every edit, pattern length, autosave and recovery, export with samples, CI). **0.14.0: block G** (drum machine: 16 pads, velocity layers, choke groups, per-pad swing, kits as folders). **0.13.0: block E** (sample editor: destructive editing, slicing, noise reduction, live spectrum). **0.12.0: block D** (time-stretch and pitch-shift of audio clips with Signalsmith Stretch). **0.11.0: blocks A, B and C below** (automation, aux buses / sends / VCA groups, LUFS / true-peak / spectrum / goniometer meters; see the notes after the table).
MIDI file import into patterns; legato and slide between notes of one channel (0.10.0). Basic sampler, a synth, four effects, mixer with
tracks and master chain, audio and MIDI recording, playlist editing, metronome, export in seven formats.

## Blocks

| # | Block | What is in it | Open-source building blocks | Effort | Needs hardware to verify |
| --- | --- | --- | --- | --- | --- |
| A | **Automation** (done, 0.11.0) | Right-click on any knob / fader -> *Create automation clip*; automation clips as free graphic objects on the playlist; parameters of the native effects and instruments first | none (own code) | L | no |
| B | **Mixer console** (done, 0.11.0, without a separate routing-matrix window) | Fader / pan per track (exists), **auxiliary buses (sends)**, **VCA groups**, bus routing; routing matrix for audio | none | M | no |
| C | **Metering** (done, 0.11.0) | Peak, **LUFS (ITU-R BS.1770 / EBU R128 momentary, short-term, integrated, loudness range)**, true peak, spectrum analyser (FFT), goniometer / phase scope | none (the standards are public) | M | no |
| D | **Time-stretch and pitch-shift** (done, 0.12.0: audio clips only, offline, source tempo typed in, no transient mode or tempo detection) | Stretch a clip to the project tempo, pitch it without changing speed, transient-preserving mode | **Signalsmith Stretch (MIT)**. *zplane elastique is commercial and closed: it cannot be added to an MIT program. Rubber Band is GPL: using it would make the whole program GPL.* | M | no |
| E | **Sample editor** (done, 0.13.0: edits a copy and saves a new WAV; slicing for playlist clips only; spectral gating tested on synthetic signals only) | Destructive editing (cut, normalise, fade, reverse), **slicing** (by transients, by grid) to a pattern, **noise reduction** (spectral gating), real-time spectrum | own code | L | no |
| F | **Native synths** | A subtractive synth (oscillators, filter, envelopes, LFO), an **FM synth** (Sytrus-like operator matrix), an **additive synth** (Harmor-like partial editor and resynthesis from a sample) | own code. "Professional level" is a long road: each of the three is its own project | XL | no |
| G | **Drum machine** (done, 0.14.0: one drum channel with 16 pads, no per-pad routing, no round-robin) | Pad grid with per-pad sample, choke groups, velocity layers, swing; a kit format | own code | M | no |
| H | **Advanced sampler** | Multi-sample zones, velocity layers, loops, ADSR, filter, **SoundFont `.sf2`** loading | **TinySoundFont (MIT)** for `.sf2`; `.GUS` patches are simple and can be read directly; `.GIG` needs libgig, which is GPL: skipped unless the licence question is settled | L | no |
| I | **Plugin hosting** | VST3, LV2, LADSPA instruments and effects, scanned and loaded into channels and mixer slots; automation of their parameters | JUCE already hosts VST3, LV2 and LADSPA. *VST2: Steinberg no longer licenses the SDK, so it cannot be added legally. Audio Units exist only on macOS.* | L | partly (needs real plugins and a sound card) |
| J | **Modular patchbay** | A graph of generators, effects, mixers, MIDI processors with unlimited cables, saved as one preset | own code on top of F, G, H and I | XL | no |
| K | **Session view** | A grid of clips / loops launched live, quantised to the beat, with scenes; recording the performance to the playlist | own code | L | partly |
| L | **Controllers** | Scripted support for hardware: MIDI mapping exists; **Akai Fire** and **Novation FLkey** need their protocols (pads, lights, screens) | the manufacturers' documented MIDI / SysEx; community notes | M each | **yes: cannot be tested without the devices** |
| M | **Pitch correction** | Monophonic pitch detection (YIN), a note-by-note editor, corrected by shifting each note (the Melodyne-like part); then **vocal / guitar pitch to MIDI** | own code with the stretcher from block D. *Melodyne's polyphonic engine is proprietary and not reproducible; monophonic is realistic* | XL | needs real voices to judge |
| N | **Time effects** (Gross-Beat-like) | Real-time stutter, gater, reverse, scratch, tape stop, pitch-down, driven by a pattern | own code | M | no |

## Suggested order
1. **Split `MainComponent.cpp`** (it is 2 600 lines; see `audit.md`). Prerequisite for everything below, S.
2. **A Automation**, because it touches every parameter and is easier before the parameter list grows.
3. **B + C** (console and metering): small, useful, and the routing matrix is needed by J.
4. **D + E** (both done): the part most useful for working on recorded audio.
5. **G + H** (G done; next the sampler and SoundFont), then **F** (synths), then **I** (plugins), then **K**, **N**, **M**, **J**, **L**.

## Things that cannot be done as asked (and what is offered instead)
- *zplane elastique*: closed and commercial. Offered: Signalsmith Stretch (MIT), same kind of result for most material.
- *VST2*: the SDK can no longer be obtained legally. VST3 and LV2 are supported instead.
- *Audio Units, Windows, macOS*: the program is built and tested for Linux only; JUCE allows the ports, but they cannot be tested here.
- *Akai Fire, Novation FLkey*: written from documentation only, never run on the device, so they would be marked "untested".
- *Melodyne-level polyphonic correction*: not realistic; the monophonic editor is.
- *`.GIG`*: only through GPL code; to be decided.
