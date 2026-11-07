# Making the built-in sounds

The scripts in this folder turn free sample libraries into the sound library of Sun Loops Studio (`resources/sounds/library`). The
library is committed to the repository, so these scripts are needed only to make it again or to change it. Each script's first
lines say exactly what it does to the recordings.

Requirements: Python 3 with `numpy`, `scipy` and `soundfile`, and `ffmpeg` with `libvorbis`.

| Script | Source (clone it first) | Writes |
|---|---|---|
| `make_piano.py` | https://github.com/sfzinstruments/SalamanderGrandPiano | `piano/*.ogg`, `programs/grand-piano.json` |
| `make_bass.py` | https://github.com/sfzinstruments/karoryfer.growlybass | `bass/*.ogg`, `programs/electric-bass*.json` |
| `make_drumgizmo_kit.py studio` | https://github.com/sfzinstruments/DrumGizmo.MuldjordKit (the `DrumGizmo/MuldjordKit` folder) | `kits/studio/*.ogg`, `kits/studio.json` |
| `make_drumgizmo_kit.py rock` | https://github.com/sfzinstruments/DrumGizmo.DRSKit (the `DrumGizmo/DRSKit` folder) | `kits/rock/*.ogg`, `kits/rock.json` |

Example: `python3 make_piano.py ~/src/SalamanderGrandPiano ../../resources/sounds/library`

The program reads `programs/<id>.json` (a multi-sample instrument: zones with `sample`, `root`, `loKey`, `hiKey`, `loVel`, `hiVel`,
optional `gain`, `pan`, `tune`, and `settings` for the sampler's envelope) and `kits/<id>.json` (16 pads with `name`, `choke`,
`pan`, `volume` and up to four `layers` of `sample` and `minVelocity`). `sample` is a path in the library without `.ogg`.
