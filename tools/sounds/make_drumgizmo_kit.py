#!/usr/bin/env python3
"""Builds a sampled drum kit of Sun Loops Studio from a DrumGizmo kit (multi-microphone recordings, CC BY 4.0).

Sources:
  studio: DrumGizmo MuldjordKit by Lars Muldjord, https://github.com/sfzinstruments/DrumGizmo.MuldjordKit
  rock:   DrumGizmo DRSKit by the DrumGizmo team (Bent Bisballe Nyeng, Lars Muldjord and others),
          https://github.com/sfzinstruments/DrumGizmo.DRSKit

Every hit of a DrumGizmo kit is recorded by all the microphones at once (one mono file per microphone). For every piece (snare,
closed hi-hat, ...) this script:
  * mixes the microphones of a hit to stereo the way the kits' own SFZ mapping does by default: the piece's own close
    microphone(s) at full level, panned where that mapping puts them; the overheads and the room microphones hard left and right;
    the close microphones of the other drums (bleed) and the trigger channel left out; constant-power panning; the snare's bottom
    microphone with its polarity flipped (it faces the top one: unflipped, the two cancel part of each other's sound);
  * measures every hit and keeps four: the loudest, and the ones closest to 6, 12 and 20 dB below it (the velocity layers);
  * cuts the silence before the hit (from 1 ms before the first sample above -50 dB of the loudest hit) and the tail below
    -60 dB (at most the piece's length, with a fade);
  * applies one gain for the whole kit and a gain per layer that keeps one third of the level difference to the piece's loudest
    hit in dB;
  * encodes Ogg Vorbis, quality 4, 44.1 kHz stereo.
It writes kits/<kit>/*.ogg and kits/<kit>.json (pads in the General MIDI order where the kit has the sound).
Usage: make_drumgizmo_kit.py studio|rock <kit folder (the one holding Samples/)> <library folder>
"""
import glob, os, re, sys
import numpy as np
from common import read, trim, encode, write_json, layer_gain

def pan_gains(p):
    """p in [0, 1], 0.5 = centre: constant-power gains (left, right)."""
    a = p * np.pi / 2
    return np.cos(a), np.sin(a)

KITS = {
    "studio": {
        "name": "Acoustic Studio",
        "credit": "DrumGizmo MuldjordKit by Lars Muldjord, CC BY 4.0 (mixed to stereo, trimmed, levelled, Ogg Vorbis)",
        "room": {"OHL": 0.0, "OHR": 1.0, "AmbL": 0.0, "AmbR": 1.0},
        # pad: (name, sample folder, {close mic: pan}, seconds, choke group)
        "pads": [
            ("Kick", "KdrumL", {"KdrumL": 0.5}, 1.2, 0),
            ("Snare Rest", "SnareRest", {"Snare_top": 0.5, "Snare_bottom": 0.5}, 1.0, 0),
            ("Snare", "Snare", {"Snare_top": 0.5, "Snare_bottom": 0.5}, 1.5, 0),
            ("Kick 2", "KdrumR", {"KdrumR": 0.5}, 1.2, 0),
            ("China", "China", {}, 4.0, 0),
            ("Floor Tom", "Tom4", {"Tom4": 0.8}, 2.2, 0),
            ("Closed Hat", "HihatClosed", {"Hihat": 0.3}, 0.8, 1),
            ("Crash 2", "CrashR", {}, 4.0, 0),
            ("Ride Bell", "RideRBell", {"RideR": 0.75}, 4.0, 0),
            ("Low Tom", "Tom3", {"Tom3": 0.6}, 2.0, 0),
            ("Open Hat", "HihatOpen", {"Hihat": 0.3}, 2.5, 1),
            ("Mid Tom", "Tom2", {"Tom2": 0.4}, 1.8, 0),
            ("High Tom", "Tom1", {"Tom1": 0.2}, 1.6, 0),
            ("Crash", "CrashL", {}, 4.0, 0),
            ("Ride 2", "RideL", {"RideL": 0.25}, 4.0, 0),
            ("Ride", "RideR", {"RideR": 0.75}, 4.0, 0),
        ],
    },
    "rock": {
        "name": "Acoustic Rock",
        "credit": "DrumGizmo DRSKit by the DrumGizmo team, CC BY 4.0 (mixed to stereo, trimmed, levelled, Ogg Vorbis)",
        "room": {"OHL": 0.0, "OHR": 1.0, "AmbL": 0.0, "AmbR": 1.0},
        "pads": [
            ("Kick", "Kdrum_without_contact", {"Kdrum_front": 0.5, "Kdrum_back": 0.5}, 1.2, 0),
            ("Snare Rest", "Snare_rest", {"Snare_top": 0.5, "Snare_bottom": 0.5}, 1.0, 0),
            ("Snare", "Snare", {"Snare_top": 0.5, "Snare_bottom": 0.5}, 1.5, 0),
            ("Rimshot", "Snare_rim", {"Snare_top": 0.5, "Snare_bottom": 0.5}, 1.5, 0),
            ("Kick Click", "Kdrum_with_contact", {"Kdrum_front": 0.5, "Kdrum_back": 0.5}, 1.2, 0),
            ("Floor Tom", "Tom3", {"Tom3": 0.75}, 2.2, 0),
            ("Closed Hat", "Hihat_closed", {"Hihat": 0.3}, 0.8, 1),
            ("Half-open Hat", "Hihat_semi_open", {"Hihat": 0.3}, 1.5, 1),
            ("Pedal Hat", "Hihat_foot", {"Hihat": 0.3}, 0.8, 1),
            ("Low Tom", "Tom2", {"Tom2": 0.5}, 2.0, 0),
            ("Open Hat", "Hihat_open", {"Hihat": 0.3}, 2.5, 1),
            ("Ride Bell", "Ride_tip_bell", {"Ride": 0.75}, 4.0, 0),
            ("High Tom", "Tom1", {"Tom1": 0.25}, 1.8, 0),
            ("Crash", "Crash_left_tip", {}, 4.0, 0),
            ("Crash 2", "Crash_right_tip", {}, 4.0, 0),
            ("Ride", "Ride_tip", {"Ride": 0.75}, 4.0, 0),
        ],
    },
}

TARGETS_DB = [-20.0, -12.0, -6.0, 0.0]   # softest layer first
MIN_VELOCITY = [1, 45, 80, 110]

def hits(folder):
    """{hit number: {mic: file}}"""
    out = {}
    for f in glob.glob(os.path.join(folder, "*.flac")):
        m = re.match(r"(\d+)-.*-([^-]+)\.flac$", os.path.basename(f))
        if m:
            out.setdefault(int(m.group(1)), {})[m.group(2)] = f
    return out

def mixdown(files, close, room):
    parts = []
    length = 0
    for mic, pan in list(close.items()) + list(room.items()):
        if mic not in files:
            raise SystemExit(f"missing microphone {mic} in {files}")
        d, sr = read(files[mic])
        assert sr == 44100, files[mic]
        x = d[:, 0]
        if mic == "Snare_bottom":
            x = -x  # the bottom microphone faces the top one: its polarity is flipped, as when mixing a real kit
        parts.append((x, pan))
        length = max(length, len(d))
    out = np.zeros((length, 2))
    for x, pan in parts:
        l, r = pan_gains(pan)
        out[:len(x), 0] += l * x
        out[:len(x), 1] += r * x
    return out

def main(kit_id, src, lib):
    kit = KITS[kit_id]
    pieces = []
    for name, folder, close, seconds, choke in kit["pads"]:
        hs = hits(os.path.join(src, "Samples", folder))
        mixed = {n: mixdown(files, close, kit["room"]) for n, files in sorted(hs.items())}
        peaks = {n: np.abs(x).max() for n, x in mixed.items()}
        top = max(peaks, key=peaks.get)
        chosen = []
        for target in TARGETS_DB:
            want = peaks[top] * 10 ** (target / 20)
            n = min(peaks, key=lambda k: abs(20 * np.log10(peaks[k] / want)))
            chosen.append(n)
        # four different hits, softest first
        chosen = sorted(set(chosen), key=lambda n: peaks[n])
        pieces.append((name, folder, seconds, choke, close, mixed, peaks, chosen))
        print(f"{name}: {len(hs)} hits, kept {[(n, round(20 * np.log10(peaks[n] / peaks[top]), 1)) for n in chosen]}")
    # gains: one for the kit, and one per layer of each piece that keeps a third of the level difference to its loudest hit
    loudest = max(peaks[n] * layer_gain(peaks[n] / peaks[chosen[-1]]) for *_, peaks, chosen in pieces for n in chosen)
    overall = 0.89 / loudest
    pads, total = [], 0
    for name, folder, seconds, choke, close, mixed, peaks, chosen in pieces:
        ref = peaks[chosen[-1]]
        layers = []
        for i, n in enumerate(chosen):
            idx = 4 - len(chosen) + i
            x = trim(mixed[n], 44100, ref, start_db=-50.0, end_db=-60.0, max_seconds=seconds, fade_seconds=min(0.3, seconds / 4), pre_seconds=0.001)
            x *= overall * layer_gain(peaks[n] / ref)
            sample = f"kits/{kit_id}/{folder.lower()}-{i + 1}"
            total += encode(x, 44100, os.path.join(lib, sample + ".ogg"), quality=4)
            layers.append({"sample": sample, "minVelocity": MIN_VELOCITY[idx] if i > 0 else 1})
        pads.append({"name": name, "choke": choke, "pan": 0.0, "layers": layers})  # the stereo image is in the sound itself
    write_json(os.path.join(lib, "kits", f"{kit_id}.json"), {"name": kit["name"], "credit": kit["credit"], "pads": pads})
    print(f"{sum(len(p['layers']) for p in pads)} files, {total / 1e6:.1f} MB")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
