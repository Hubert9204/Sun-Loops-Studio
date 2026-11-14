# The demo projects

`resources/demos/*.slp` are the songs of the browser's **Demo Projects** folder (0.65.0). They are ordinary project files, built
into the program (`src/library/DemoProjects.cpp`). This folder makes them:

| File | What it is |
| --- | --- |
| `DemoTool.cpp` | A console program (`cmake -DSLS_BUILD_DEMO_TOOL=ON`, target `SunLoopsDemoTool`). `catalog <out.json>`: what a project can use - the SunBurst and HexForge factory presets as saved plugin states, their parameter names, every effect's parameter table, the synth patches, the kits and the sampler instruments. `sunrise <out.slp>`: the built-in demo song with the bundled plugins' states. `render <in.slp> <out.wav> [--stems <dir>]`: the song as the export renders it, with its integrated loudness, true peak, band balance and level per four bars. `length <id>...`: how long built-in sounds are. |
| `slp.py` | Writes a project file from Python: channels (samplers, library instruments, synths, plugins with a preset), patterns and notes, the playlist, mixer tracks and buses, aux buses, VCA groups, master effects, automation clips, audio clips and markers. |
| `white_line.py` | **White Line** - melodic techno, 129 BPM, C minor, 96 bars. |
| `ryo.py` | **Ryo** - lo-fi hip hop, 92 BPM, C minor, 64 bars. |
| `make_demos.sh` | Builds the tool and writes the three files again (then renders them; `RENDER=0` skips that). |

White Line and Ryo started as two sketches (an FL Studio session imported into the program: loose patterns on a playlist, synth
channels with default sounds, an empty mixer). Their ideas were kept - the melodies, bass lines, grooves and chords they suggest - and
built into songs: a form with sections, new parts (pads, stabs, arpeggios, counter-lines, percussion, risers and impacts), the sounds
chosen from the program's own instruments, a mix (EQ, filters, compressors with sidechain, reverb and delay buses, a drum bus, a
mastering chain) and automation. Both were checked by rendering: each channel alone against the others in every section, the
spectrum, the loudness (White Line about -12.6 LUFS, Ryo about -14.4, Sunrise Drive -13.5) and the true peak (below -1 dBTP).
