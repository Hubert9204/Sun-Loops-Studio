# TinySoundFont (MIT)

Single-header SoundFont 2 library by Bernhard Schelling, copied unchanged. Sun Loops Studio uses only its **file parser**
(`tsf_load_filename`, the presets and their regions) to turn a SoundFont preset into sampler zones (`src/library/SoundFont.cpp`);
the sound itself is made by Sun Loops Studio's own sampler voices, not by TinySoundFont's synthesizer.

| Library | Upstream | Revision | Licence |
| --- | --- | --- | --- |
| TinySoundFont `tsf.h` (v0.9) | https://github.com/schellingb/TinySoundFont | `853a0a171759f1ddba0de1442133a75912bbeffa` (19 Jul 2026) | MIT, `LICENSE-tinysoundfont.txt` |

The directory is added as a `SYSTEM` include path.
