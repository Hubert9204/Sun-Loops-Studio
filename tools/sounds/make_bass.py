#!/usr/bin/env python3
"""Builds the built-in Electric Bass of Sun Loops Studio from Growlybass (Karoryfer Lecolds, CC0 1.0): a Squier Jazz Bass, recorded direct.

Source: https://github.com/sfzinstruments/karoryfer.growlybass (44.1 kHz mono WAV). The key ranges (an octave lower, so that keys play their real
pitch) and velocity ranges are those of its "clean" mapping (sustained notes, 4 velocity layers); only the first of the four round robins is kept. Every file is changed only in
these ways: the silence before the note is cut, the tail below -60 dB of the loudest layer of its note is cut (at most 4 s, a 0.25 s fade), one gain
for all files times a gain per layer of each note that keeps one third of the level difference between its layers, Ogg Vorbis quality 4 (mono).
A note recorded more than 5 cents off its pitch (measured from its harmonics) is tuned in its zones (`tune`), not in the audio.
The staccato notes (one layer) become a second program, "Electric Bass Staccato".
Usage: make_bass.py <growlybass folder> <library folder>   (writes bass/*.ogg, programs/electric-bass.json and programs/electric-bass-staccato.json)
"""
import os, re, sys
import numpy as np
from common import read, trim, encode, write_json, layer_gain

DYN = ["pp", "p", "f", "ff"]

def groups(sfz):
    """(lokey, hikey, keycenter, note name) of every sustained group of the clean mapping, from its rr1 regions."""
    out = []
    for block in open(sfz).read().split("<group>")[1:]:
        m = re.search(r"sustain\\(\w+)_pp_rr1\.wav", block)
        if not m:
            continue
        def num(key, default=None):
            found = re.search(key + r"=(\d+)", block)
            return int(found.group(1)) if found else default
        out.append((num("lokey"), num("hikey"), num("pitch_keycenter", 60), m.group(1))) # SFZ: the key centre defaults to 60
    out.sort()
    return out

def measured_cents(x, sr, root):
    """How far (in cents) the recording is from the equal-tempered pitch of `root`: from its 2nd, 3rd and 4th harmonics, measured with
    a zero-padded FFT between 0.3 and 1.8 s (the fundamental of the low strings is too weak to measure on its own)."""
    y = x[int(0.3 * sr):int(1.8 * sr), 0]
    spectrum = np.abs(np.fft.rfft(y * np.hanning(len(y)), 32 * len(y)))
    freqs = np.fft.rfftfreq(32 * len(y), 1.0 / sr)
    f0 = 440.0 * 2 ** ((root - 69) / 12)
    estimates = []
    for h in (2, 3, 4):
        band = (freqs > h * f0 * 0.94) & (freqs < h * f0 * 1.06)
        estimates.append(freqs[band][np.argmax(spectrum[band])] / h)
    return 1200.0 * np.log2(np.median(estimates) / f0)

def velocity_ranges(sfz, name):
    block = [b for b in open(sfz).read().split("<group>") if f"sustain\\{name}_pp_rr1" in b][0]
    ranges = []
    for d in DYN:
        region = re.search(r"<region>\s*sample=sustain\\" + name + "_" + d + r"_rr1\.wav(.*?)(?=<region>|$)", block, re.S).group(1)
        lo = re.search(r"lovel=(\d+)", region); hi = re.search(r"hivel=(\d+)", region)
        ranges.append((int(lo.group(1)) if lo else 1, int(hi.group(1)) if hi else 127))
    return ranges

def main(src, lib):
    sfz = os.path.join(src, "growlybass_clean.sfz")
    # The mapping puts the open E string (41 Hz, E1) on key 40 (E2), the bass-clef habit of writing an octave up; here every key
    # plays its real pitch, so everything moves down an octave.
    gs = [(lo - 12, hi - 12, root - 12, n) for lo, hi, root, n in groups(sfz)]
    data = {(n, d): read(os.path.join(src, "sustain", f"{n}_{d}_rr1.wav"))[0] for *_, n in gs for d in DYN}
    loudest = {n: np.abs(data[(n, "ff")]).max() for *_, n in gs}
    gains = {(n, d): layer_gain(np.abs(data[(n, d)]).max() / loudest[n]) for *_, n in gs for d in DYN}
    overall = 0.89 / max(np.abs(data[(n, d)]).max() * gains[(n, d)] for *_, n in gs for d in DYN)
    zones, total = [], 0
    for lo, hi, root, n in gs:
        vr = velocity_ranges(sfz, n)
        # tune a note that was recorded off pitch (the open E string is about 30 cents sharp) in its zones, not in the audio
        cents = measured_cents(data[(n, "f")], 44100, root)
        tune = round(-cents / 100.0, 3) if abs(cents) > 5.0 else 0.0
        print(f"{n}: {cents:+.1f} cents")
        for i, d in enumerate(DYN):
            x = trim(data[(n, d)], 44100, loudest[n], end_db=-60.0, max_seconds=4.0, fade_seconds=0.25) * overall * gains[(n, d)]
            sample = f"bass/{n}-{i + 1}"
            total += encode(x, 44100, os.path.join(lib, sample + ".ogg"), quality=4)
            zones.append({"sample": sample, "root": root, "loKey": lo, "hiKey": hi, "loVel": vr[i][0], "hiVel": vr[i][1], "tune": tune})
    for z in zones[:4]:
        z["loKey"] = 0
    for z in zones[-4:]:
        z["hiKey"] = 127
    credit = "Growlybass by Karoryfer Lecolds, CC0 1.0 (trimmed, levelled, Ogg Vorbis)"
    write_json(os.path.join(lib, "programs", "electric-bass.json"), {
        "name": "Electric Bass", "category": "Bass", "credit": credit,
        "settings": {"envelope": True, "attack": 0.0, "decay": 1.0, "sustain": 1.0, "release": 0.12},
        "zones": zones})
    # Staccato: one layer per note.
    st = {n: read(os.path.join(src, "staccato", f"{n}_staccato_rr1.wav"))[0] for *_, n in gs}
    sgain = 0.89 / max(np.abs(x).max() for x in st.values())
    szones = []
    for lo, hi, root, n in gs:
        x = trim(st[n], 44100, np.abs(st[n]).max(), end_db=-60.0, fade_seconds=0.05) * sgain
        sample = f"bass/{n}-staccato"
        total += encode(x, 44100, os.path.join(lib, sample + ".ogg"), quality=4)
        cents = measured_cents(data[(n, "f")], 44100, root) # the same string and fret as the sustained note
        szones.append({"sample": sample, "root": root, "loKey": lo, "hiKey": hi, "loVel": 1, "hiVel": 127,
                       "tune": round(-cents / 100.0, 3) if abs(cents) > 5.0 else 0.0})
    szones[0]["loKey"] = 0
    szones[-1]["hiKey"] = 127
    write_json(os.path.join(lib, "programs", "electric-bass-staccato.json"), {"name": "Electric Bass Staccato", "category": "Bass", "credit": credit,
        "settings": {"envelope": True, "attack": 0.0, "decay": 1.0, "sustain": 1.0, "release": 0.05}, "zones": szones})
    print(f"{len(zones) + len(szones)} files, {total / 1e6:.1f} MB")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
