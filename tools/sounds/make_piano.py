#!/usr/bin/env python3
"""Builds the built-in Grand Piano of Sun Loops Studio from the Salamander Grand Piano V3 (Alexander Holm, CC BY 3.0).

Source: https://github.com/sfzinstruments/SalamanderGrandPiano (48 kHz FLAC, 30 notes a minor third apart, 16 velocity layers).
Four of the sixteen layers are kept (v2, v5, v11, v16: a level step of about 6 dB each), at 44.1 kHz, so that the whole
piano fits in about 170 MB of memory once decoded. Every file is changed only in these ways:
  * the silence before the hammer is cut (from 2 ms before the first sample above -60 dB of the loudest layer of its note);
  * it is cut where it has fallen 66 dB below the loudest layer of its note, at most 7 / 5.5 / 4 / 3 s (by pitch), with a 0.4 s fade;
  * one gain for all files, times a gain per layer of each note that keeps one third of the level difference between that note's
    layers in dB
    (the program plays the rest of the dynamics with the velocity);
  * it is encoded as Ogg Vorbis, quality 3.
Usage: make_piano.py <SalamanderGrandPiano folder> <library folder>   (writes piano/*.ogg and programs/grand-piano.json)
"""
import os, sys
import numpy as np
from scipy.signal import resample_poly
from common import read, trim, encode, write_json, layer_gain

LAYERS = ["v2", "v5", "v11", "v16"]  # about 17, 11, 6 and 0 dB below the loudest
VELOCITY = [(1, 43), (44, 80), (81, 112), (113, 127)]

def notes():
    return [(21 + 3 * i, {9: "A", 0: "C", 3: "D#", 6: "F#"}[(21 + 3 * i) % 12] + str((21 + 3 * i) // 12 - 1)) for i in range(30)]

def max_seconds(midi):
    return 7.0 if midi <= 36 else 5.5 if midi <= 60 else 4.0 if midi <= 84 else 3.0

def main(src, lib):
    data = {}
    for midi, name in notes():
        for layer in LAYERS:
            d, sr = read(os.path.join(src, "Samples", f"{name}{layer}.flac"))
            assert sr == 48000 and d.shape[1] == 2
            data[(midi, layer)] = d
    loudest = {m: np.abs(data[(m, "v16")]).max() for m, _ in notes()}
    gains = {(m, l): layer_gain(np.abs(data[(m, l)]).max() / loudest[m]) for m, _ in notes() for l in LAYERS}
    overall = 0.89 / max(np.abs(data[(m, l)]).max() * gains[(m, l)] for m, _ in notes() for l in LAYERS)
    zones, total = [], 0
    for i, (midi, name) in enumerate(notes()):
        lo = 0 if i == 0 else midi - 1
        hi = 127 if i == len(notes()) - 1 else midi + 1
        for index, layer in enumerate(LAYERS):
            x = trim(data[(midi, layer)], 48000, loudest[midi], max_seconds=max_seconds(midi), fade_seconds=0.4) * overall * gains[(midi, layer)]
            x = resample_poly(x, 147, 160, axis=0)  # 48 kHz -> 44.1 kHz
            sample = f"piano/n{midi:03d}-{index + 1}"
            total += encode(x, 44100, os.path.join(lib, sample + ".ogg"))
            zones.append({"sample": sample, "root": midi, "loKey": lo, "hiKey": hi, "loVel": VELOCITY[index][0], "hiVel": VELOCITY[index][1]})
    write_json(os.path.join(lib, "programs", "grand-piano.json"), {
        "name": "Grand Piano", "category": "Keys",
        "credit": "Salamander Grand Piano V3 by Alexander Holm, CC BY 3.0 (trimmed, levelled, Ogg Vorbis)",
        "settings": {"envelope": True, "attack": 0.0, "decay": 1.0, "sustain": 1.0, "release": 0.35},
        "zones": zones})
    print(f"{len(zones)} files, {total / 1e6:.1f} MB")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
