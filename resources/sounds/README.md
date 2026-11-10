# Sounds that come with Sun Loops Studio

Everything here is built into the program (nothing is read from disk at run time).

* `intro.wav` - the start-up sound by Hubert (5 s, 48 kHz, 24 bit stereo), played when the program opens. Used unchanged.
* `library/` - the sound library (0.22.0): sampled instruments and drum kits from free libraries, made by the scripts in
  [`tools/sounds`](../../tools/sounds/README.md), which say exactly what was changed (trimming, levels, mixing the microphones of a
  drum kit to stereo, Ogg Vorbis encoding). The build packs the folder into one zip archive linked into the program.

| Folder | What | From | License |
|---|---|---|---|
| `library/piano` | Grand Piano: 30 notes a minor third apart, 4 velocity layers | Salamander Grand Piano V3 by Alexander Holm (a Yamaha C5) | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) |
| `library/bass` | Electric Bass (4 velocity layers) and Electric Bass Staccato, 14 notes | Growlybass by Karoryfer Lecolds (a Squier Jazz Bass) | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `library/kits/studio` | Acoustic Studio kit: 16 pieces, 4 velocity layers | DrumGizmo MuldjordKit by Lars Muldjord | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| `library/kits/rock` | Acoustic Rock kit: 16 pieces, 4 velocity layers | DrumGizmo DRSKit by the DrumGizmo team | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| `library/programs`, `library/kits/*.json` | How the sounds are mapped to keys, velocities and pads | written for Sun Loops Studio | AGPL-3.0 like the program (GPL-3.0 before 0.34.0) |

The recordings keep their own licenses (above); they are not covered by the program's AGPL. Under CC BY the sounds can be used in
music of any kind, also commercially, as long as the authors are credited when the sounds themselves are passed on. The electronic
kits and the hand percussion are not recordings: they are made by the program's code (`src/audio/DrumSynth.cpp`, AGPL-3.0).

The Cyber-Hub sound theme that earlier versions carried was removed in 0.22.0.
