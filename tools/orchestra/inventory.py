#!/usr/bin/env python3
"""SunSymphony, Milestone 0: what the free orchestral sources hold, instrument by instrument and articulation by articulation.

    python3 tools/orchestra/inventory.py [--work DIR] [--measure N] [--out tools/orchestra/coverage]

For each source it fetches only what it needs:
  * VSCO 2 Community Edition and VCSL (Versilian Studios, CC0): a git clone without the audio ("blobless"), so the file list
    costs a few MB; --measure N then fetches N files per instrument and articulation to measure them.
  * No Budget Orchestra: the SFZ files and each instrument's license.txt (the samples live on bandshed.net).
  * University of Iowa MIS: the instrument pages, when the site can be reached from this machine.

Every file of an orchestral instrument is classified (instrument, articulation, written note, dynamic layer, round robin);
--measure adds the sample rate, channels, length, peak and the measured pitch, which tells how the source numbers its octaves
(VSCO writes some instruments an octave low). The result is coverage.json and coverage.md: the target instruments of the plan
(claude/orchestral-sampler-plan.md), each articulation with its source, notes, range against the instrument's real range, dynamic
layers and round robins, and the gaps.

Only the Python standard library and numpy are needed (numpy only for --measure).
"""
import argparse
import collections
import io
import json
import os
import re
import subprocess
import sys
import urllib.request
import wave

# --------------------------------------------------------------------------------------------------------------- sources
SOURCES = {
    "vsco2ce": {"name": "VSCO 2 Community Edition", "git": "https://github.com/sgossner/VSCO-2-CE", "license": "CC0-1.0",
                "tier": "A"},
    "vcsl": {"name": "Versilian Community Sample Library", "git": "https://github.com/sgossner/VCSL", "license": "CC0-1.0",
             "tier": "A"},
    "nbo": {"name": "No Budget Orchestra (SFZ)", "git": "https://github.com/ssj71/No-Budget-Orchestra",
            "license": "per instrument (license.txt)", "tier": "B",
            "samples": "http://www.bandshed.net/sounds/sfz/nbo_2.zip"},
    "iowa": {"name": "University of Iowa MIS", "web": "https://theremin.music.uiowa.edu/MIS.html",
             "license": "free for any project, without restrictions", "tier": "A"},
}

# --------------------------------------------------------------------------------------------------------------- targets
# The instruments of the plan: id -> (family, name, lowest, highest playable MIDI note, articulations wanted).
# Ranges are the instruments' usual orchestral ranges (written for the sounding pitch).
def m(name):
    names = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
    mt = re.fullmatch(r"([A-G])([#b]?)(-?\d)", name)
    return names[mt[1]] + (1 if mt[2] == "#" else -1 if mt[2] == "b" else 0) + 12 * (int(mt[3]) + 1)


STRINGS = ["sustain", "staccato", "spiccato", "pizzicato", "tremolo", "legato"]
WINDS = ["sustain", "staccato", "legato"]
TARGETS = {
    "violins":       ("Strings", "Violins (section)", m("G3"), m("A7"), STRINGS),
    "violas":        ("Strings", "Violas (section)", m("C3"), m("E6"), STRINGS),
    "cellos":        ("Strings", "Cellos (section)", m("C2"), m("A5"), STRINGS),
    "basses":        ("Strings", "Basses (section)", m("E1"), m("G4"), STRINGS),
    "violin-solo":   ("Strings", "Solo Violin", m("G3"), m("A7"), STRINGS),
    "viola-solo":    ("Strings", "Solo Viola", m("C3"), m("E6"), STRINGS),
    "cello-solo":    ("Strings", "Solo Cello", m("C2"), m("A5"), STRINGS),
    "bass-solo":     ("Strings", "Solo Bass", m("E1"), m("G4"), STRINGS),
    "piccolo":       ("Woodwinds", "Piccolo", m("D5"), m("C8"), WINDS),
    "flute":         ("Woodwinds", "Flute", m("C4"), m("D7"), WINDS),
    "oboe":          ("Woodwinds", "Oboe", m("Bb3"), m("A6"), WINDS),
    "cor-anglais":   ("Woodwinds", "Cor Anglais", m("E3"), m("C6"), WINDS),
    "clarinet":      ("Woodwinds", "Clarinet in Bb", m("D3"), m("Bb6"), WINDS),
    "bass-clarinet": ("Woodwinds", "Bass Clarinet", m("Bb1"), m("F5"), WINDS),
    "bassoon":       ("Woodwinds", "Bassoon", m("Bb1"), m("E5"), WINDS),
    "contrabassoon": ("Woodwinds", "Contrabassoon", m("Bb0"), m("F3"), WINDS),
    "trumpet":       ("Brass", "Trumpet", m("F#3"), m("D6"), WINDS + ["muted"]),
    "horn":          ("Brass", "French Horn", m("B1"), m("F5"), WINDS + ["muted"]),
    "trombone":      ("Brass", "Tenor Trombone", m("E2"), m("F5"), WINDS),
    "bass-trombone": ("Brass", "Bass Trombone", m("Bb0"), m("Bb4"), WINDS),
    "tuba":          ("Brass", "Tuba", m("D1"), m("F4"), WINDS),
    "timpani":       ("Percussion", "Timpani", m("D2"), m("C4"), ["hit", "roll"]),
    "bass-drum":     ("Percussion", "Concert Bass Drum", None, None, ["hit", "roll"]),
    "cymbals":       ("Percussion", "Clash Cymbals", None, None, ["hit"]),
    "suspended-cymbal": ("Percussion", "Suspended Cymbal", None, None, ["hit", "roll"]),
    "snare":         ("Percussion", "Snare Drum", None, None, ["hit", "roll"]),
    "tam-tam":       ("Percussion", "Tam-tam", None, None, ["hit"]),
    "triangle":      ("Percussion", "Triangle", None, None, ["hit", "roll"]),
    "tambourine":    ("Percussion", "Tambourine", None, None, ["hit", "roll"]),
    "glockenspiel":  ("Percussion", "Glockenspiel", m("G5"), m("C8"), ["hit"]),
    "xylophone":     ("Percussion", "Xylophone", m("F4"), m("C8"), ["hit", "roll"]),
    "marimba":       ("Percussion", "Marimba", m("C2"), m("C7"), ["hit", "roll"]),
    "vibraphone":    ("Percussion", "Vibraphone", m("F3"), m("F6"), ["hit"]),
    "crotales":      ("Percussion", "Crotales", m("C6"), m("C8"), ["hit"]),
    "tubular-bells": ("Percussion", "Tubular Bells", m("C4"), m("F5"), ["hit"]),
    "harp":          ("Keys & Harp", "Concert Harp", m("Cb1"), m("G#7"), ["pluck", "harmonics"]),
    "celesta":       ("Keys & Harp", "Celesta", m("C4"), m("C8"), ["hit"]),
    "piano":         ("Keys & Harp", "Concert Grand", m("A0"), m("C8"), ["sustain", "release"]),
}

# --------------------------------------------------------------------------------------------------------------- rules
# (source, regular expression on the path, instrument, articulation). The first rule that matches wins; files that match no rule
# are not orchestral (or not wanted) and are only counted.
RULES = [
    # VSCO 2 CE - strings
    ("vsco2ce", r"^Strings/Violin Section/susVib/", "violins", "sustain"),
    ("vsco2ce", r"^Strings/Violin Section/Spic/", "violins", "spiccato"),
    ("vsco2ce", r"^Strings/Violin Section/Pizz/", "violins", "pizzicato"),
    ("vsco2ce", r"^Strings/Violin Section/Trem/", "violins", "tremolo"),
    ("vsco2ce", r"^Strings/Viola Section/susvib/", "violas", "sustain"),
    ("vsco2ce", r"^Strings/Viola Section/spic/", "violas", "spiccato"),
    ("vsco2ce", r"^Strings/Viola Section/pizz/", "violas", "pizzicato"),
    ("vsco2ce", r"^Strings/Viola Section/trem/", "violas", "tremolo"),
    ("vsco2ce", r"^Strings/Cello Section/susvib/", "cellos", "sustain"),
    ("vsco2ce", r"^Strings/Cello Section/spic/", "cellos", "spiccato"),
    ("vsco2ce", r"^Strings/Cello Section/pizzT/", "cellos", "pizzicato"),
    ("vsco2ce", r"^Strings/Cello Section/trem/", "cellos", "tremolo"),
    ("vsco2ce", r"^Strings/Solo Violin/Arco Vib/", "violin-solo", "sustain"),
    ("vsco2ce", r"^Strings/Solo Violin/spic/", "violin-solo", "spiccato"),
    ("vsco2ce", r"^Strings/Solo Violin/Pizz/", "violin-solo", "pizzicato"),
    ("vsco2ce", r"^Strings/Solo Violin/Trem/", "violin-solo", "tremolo"),
    ("vsco2ce", r"^Strings/Solo Contrabass/SusVib/", "bass-solo", "sustain"),
    ("vsco2ce", r"^Strings/Solo Contrabass/SusNV/", "bass-solo", "sustain (non vibrato)"),
    ("vsco2ce", r"^Strings/Solo Contrabass/Spic/", "bass-solo", "spiccato"),
    ("vsco2ce", r"^Strings/Solo Contrabass/Pizz/", "bass-solo", "pizzicato"),
    ("vsco2ce", r"^Strings/Solo Contrabass/Trem/", "bass-solo", "tremolo"),
    ("vsco2ce", r"^Strings/Harp/", "harp", "pluck"),
    # VSCO 2 CE - woodwinds
    ("vsco2ce", r"^Woodwinds/Piccolo/Sus/", "piccolo", "sustain"),
    ("vsco2ce", r"^Woodwinds/Piccolo/Stac/", "piccolo", "staccato"),
    ("vsco2ce", r"^Woodwinds/Flute/susvib/", "flute", "sustain"),
    ("vsco2ce", r"^Woodwinds/Flute/susNV/", "flute", "sustain (non vibrato)"),
    ("vsco2ce", r"^Woodwinds/Flute/expvib/", "flute", "sustain (expressive)"),
    ("vsco2ce", r"^Woodwinds/Flute/stac/", "flute", "staccato"),
    ("vsco2ce", r"^Woodwinds/Oboe/Vib/", "oboe", "sustain"),
    ("vsco2ce", r"^Woodwinds/Oboe/Sus/", "oboe", "sustain (non vibrato)"),
    ("vsco2ce", r"^Woodwinds/Oboe/Stacc/", "oboe", "staccato"),
    ("vsco2ce", r"^Woodwinds/Clarinet/susLong/", "clarinet", "sustain"),
    ("vsco2ce", r"^Woodwinds/Clarinet/stac/", "clarinet", "staccato"),
    ("vsco2ce", r"^Woodwinds/Bassoon/vib/", "bassoon", "sustain"),
    ("vsco2ce", r"^Woodwinds/Bassoon/sus/", "bassoon", "sustain (non vibrato)"),
    ("vsco2ce", r"^Woodwinds/Bassoon/stac/", "bassoon", "staccato"),
    # VSCO 2 CE - brass
    ("vsco2ce", r"^Brass/Trumpet/susvib/", "trumpet", "sustain"),
    ("vsco2ce", r"^Brass/Trumpet/sus/", "trumpet", "sustain (non vibrato)"),
    ("vsco2ce", r"^Brass/Trumpet/stac/", "trumpet", "staccato"),
    ("vsco2ce", r"^Brass/Trumpet/straightM-sus/", "trumpet", "muted"),
    ("vsco2ce", r"^Brass/Trumpet/harmonM-sus/", "trumpet", "muted (harmon)"),
    ("vsco2ce", r"^Brass/F Horn/sus/", "horn", "sustain"),
    ("vsco2ce", r"^Brass/F Horn/stac/", "horn", "staccato"),
    ("vsco2ce", r"^Brass/F Horn/mute/", "horn", "muted"),
    ("vsco2ce", r"^Brass/Tenor Trombone/sus/", "trombone", "sustain"),
    ("vsco2ce", r"^Brass/Tenor Trombone/vib/", "trombone", "sustain (vibrato)"),
    ("vsco2ce", r"^Brass/Tenor Trombone/stac/", "trombone", "staccato"),
    ("vsco2ce", r"^Brass/Tuba/sus/", "tuba", "sustain"),
    ("vsco2ce", r"^Brass/Tuba/stac/", "tuba", "staccato"),
    # VSCO 2 CE - percussion (the "temp" folder repeats files of the main one)
    ("vsco2ce", r"^Percussion/temp/", None, None),
    ("vsco2ce", r"^Percussion/Timpani/Rolls/", "timpani", "roll"),
    ("vsco2ce", r"^Percussion/Timpani/", "timpani", "hit"),
    ("vsco2ce", r"^Percussion/BDrumNewhit", "bass-drum", "hit"),
    ("vsco2ce", r"^Percussion/Snare2-roll", "snare", "roll"),
    ("vsco2ce", r"^Percussion/Snare2-(Hit|taps)", "snare", "hit"),
    ("vsco2ce", r"^Percussion/cymbal-crash", "cymbals", "hit"),
    ("vsco2ce", r"^Percussion/susCymb1-(hit|hitstick)", "suspended-cymbal", "hit"),
    ("vsco2ce", r"^Percussion/susCymb1-cresc", "suspended-cymbal", "roll"),
    ("vsco2ce", r"^Percussion/gongHit", "tam-tam", "hit"),
    ("vsco2ce", r"^Percussion/Triangle\d-Roll", "triangle", "roll"),
    ("vsco2ce", r"^Percussion/Triangle\d-Hit", "triangle", "hit"),
    ("vsco2ce", r"^Percussion/Tamb1-Roll", "tambourine", "roll"),
    ("vsco2ce", r"^Percussion/Tamb1-(Hit|Shake)", "tambourine", "hit"),
    ("vsco2ce", r"^Percussion/Glock/", "glockenspiel", "hit"),
    ("vsco2ce", r"^Percussion/Xylo/", "xylophone", "hit"),
    ("vsco2ce", r"^Percussion/Marimba/", "marimba", "hit"),
    ("vsco2ce", r"^Percussion/TB_hit", "tubular-bells", "hit"),
    # VCSL
    ("vcsl", r"^Chordophones/Composite Chordophones/Concert Harp/", "harp", "pluck"),
    ("vcsl", r"^Chordophones/Zithers/Grand Piano, Steinway B/(Sus|NoSus)/", "piano", "sustain"),
    ("vcsl", r"^Chordophones/Zithers/Grand Piano, Steinway B/Rel/", "piano", "release"),
    ("vcsl", r"^Idiophones/Struck Idiophones/Glockenspiel/", "glockenspiel", "hit"),
    ("vcsl", r"^Idiophones/Struck Idiophones/Tubular Bells [12]/", "tubular-bells", "hit"),
    ("vcsl", r"^Idiophones/Struck Idiophones/Xylophone/", "xylophone", "hit"),
    ("vcsl", r"^Idiophones/Struck Idiophones/Vibraphone/(Hard|Soft) Mallets/", "vibraphone", "hit"),
    ("vcsl", r"^Idiophones/Struck Idiophones/Marimba/", "marimba", "hit"),
    ("vcsl", r"^Idiophones/Struck Idiophones/Triangles/Legacy/", None, None),
    ("vcsl", r"^Idiophones/Struck Idiophones/Triangles/.*Roll", "triangle", "roll"),
    ("vcsl", r"^Idiophones/Struck Idiophones/Triangles/", "triangle", "hit"),
    ("vcsl", r"^Idiophones/Struck Idiophones/Clash Cymbals", "cymbals", "hit"),
    ("vcsl", r"^Idiophones/Struck Idiophones/Suspended Cymbal \d/.*(roll|cresc|swell)", "suspended-cymbal", "roll"),
    ("vcsl", r"^Idiophones/Struck Idiophones/Suspended Cymbal \d/", "suspended-cymbal", "hit"),
    ("vcsl", r"^Idiophones/Struck Idiophones/Gong \d/", "tam-tam", "hit"),
    ("vcsl", r"^Idiophones/Struck Idiophones/Tambourine [12]/.*[Rr]oll", "tambourine", "roll"),
    ("vcsl", r"^Idiophones/Struck Idiophones/Tambourine [12]/", "tambourine", "hit"),
    ("vcsl", r"^Membranophones/Struck Membranophones/Bass Drum [12]/.*(roll|cresc)", "bass-drum", "roll"),
    ("vcsl", r"^Membranophones/Struck Membranophones/Bass Drum [12]/", "bass-drum", "hit"),
    ("vcsl", r"^Membranophones/Struck Membranophones/Snare Drum, (Modern \d|Rope Tension)/.*[Rr]oll", "snare", "roll"),
    ("vcsl", r"^Membranophones/Struck Membranophones/Snare Drum, (Modern \d|Rope Tension)/", "snare", "hit"),
    ("vcsl", r"^Membranophones/Struck Membranophones/Timpani \d/Roll/", "timpani", "roll"),
    ("vcsl", r"^Membranophones/Struck Membranophones/Timpani \d/Hit/", "timpani", "hit"),
]
# No Budget Orchestra: one SFZ file per articulation; folder -> instrument.
NBO_FOLDERS = {
    "JgViolins": "violins", "ViolaSect": "violas", "CelloSect": "cellos", "Basses": "basses", "SoloViolin": "violin-solo",
    "SoloViolin2": "violin-solo", "SoloViola": "viola-solo", "SoloCello": "cello-solo", "SoloBass": "bass-solo",
    "FluteSect": "flute", "SoloFlute": "flute", "ClarinetSect": "clarinet", "SoloClarinet": "clarinet",
    "BassoonSect": "bassoon", "SoloBassoon": "bassoon", "TrumpetSect": "trumpet", "SoloTrumpet": "trumpet",
    "TrumpetMute": "trumpet", "TromboneSect": "trombone", "SoloTrombone": "trombone", "Tubas": "tuba", "SoloTuba": "tuba",
    "Harp": "harp", "Timpani": "timpani", "glock": "glockenspiel", "Chimes": "tubular-bells", "OrchBassDrum": "bass-drum",
    "OrchCym": "cymbals", "Triangle": "triangle", "Marimba": "marimba", "MslpVibes": "vibraphone",
}
NBO_ARTICULATIONS = [(r"pizz", "pizzicato"), (r"trem", "tremolo"), (r"stac|short", "staccato"), (r"leg", "legato"),
                     (r"mute", "muted"), (r"roll", "roll"), (r".", None)]

# --------------------------------------------------------------------------------------------------------------- parsing
NOTE_RE = re.compile(r"(?<![A-Za-z])([A-G])(#|b)?(-?\d)(?![0-9])")
DYN_RE = re.compile(r"(?<![a-z])(ppp|pp|p|mp|mf|f|ff|fff|v\d+|vl\d+|loud|medium|quiet|soft|hard)(?=\d*(?:[_\-. ]|$))", re.I)
RR_RE = re.compile(r"(?:rr|RR)(\d+)|_(\d)(?=\.wav$|_[A-Za-z]+\.wav$)")


def note_of(name):
    found = NOTE_RE.findall(name)
    if not found:
        return None
    letter, accidental, octave = found[-1]
    return m(letter + (accidental or "") + octave)


def dyn_of(name):
    found = DYN_RE.findall(os.path.splitext(name)[0])
    return found[-1].lower() if found else None


def rr_of(name):
    mt = RR_RE.search(name)
    return int(mt[1] or mt[2]) if mt else 1


def note_name(n):
    return ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"][n % 12] + str(n // 12 - 1) if n is not None else "-"


# --------------------------------------------------------------------------------------------------------------- fetching
def run(*cmd, cwd=None):
    return subprocess.run(cmd, cwd=cwd, check=True, capture_output=True, text=True).stdout


def git_source(work, key):
    url = SOURCES[key]["git"]
    folder = os.path.join(work, key)
    if not os.path.isdir(folder):
        print(f"  cloning {url} (file list only)", file=sys.stderr)
        run("git", "clone", "-q", "--filter=blob:none", "--no-checkout", url, folder)
    commit = run("git", "rev-parse", "HEAD", cwd=folder).strip()
    files = run("git", "ls-tree", "-r", "--name-only", "HEAD", cwd=folder).splitlines()
    return folder, commit, files


def git_blob(folder, path):
    # (git fetches the one blob when the clone has no audio)
    return subprocess.run(["git", "cat-file", "-p", "HEAD:" + path], cwd=folder, check=True, capture_output=True).stdout


def reachable(url):
    try:
        urllib.request.urlopen(urllib.request.Request(url, method="HEAD"), timeout=15)
        return True
    except Exception:
        return False


# --------------------------------------------------------------------------------------------------------------- measuring
def measure(data):
    """Sample rate, channels, bits, seconds, peak dBFS and the pitch (MIDI, fractional) of a WAV file's steady part."""
    import numpy as np
    with wave.open(io.BytesIO(data)) as w:
        rate, channels, width, frames = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(frames)
    if width == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x) / float(1 << 23)
    elif width == 2:
        x = np.frombuffer(raw, dtype="<i2") / 32768.0
    elif width == 4:
        x = np.frombuffer(raw, dtype="<i4") / float(1 << 31)
    else:
        x = (np.frombuffer(raw, dtype=np.uint8) - 128) / 128.0
    x = x.reshape(-1, channels).mean(axis=1)
    peak = float(np.max(np.abs(x))) if len(x) else 0.0
    result = {"rate": rate, "channels": channels, "bits": width * 8, "seconds": round(frames / rate, 2),
              "peak_db": round(20 * np.log10(peak), 1) if peak > 0 else None, "pitch": None}
    # Pitch: YIN on 100 ms after the loudest point (the attack is over, the note is still loud).
    start = int(np.argmax(np.abs(x))) + int(0.05 * rate)
    seg = x[start:start + int(0.1 * rate)]
    if len(seg) < int(0.06 * rate):
        return result
    tau_max = min(len(seg) // 2, int(rate / 25))
    tau_min = int(rate / 4500)
    d = np.array([np.sum((seg[:-t] - seg[t:]) ** 2) for t in range(1, tau_max)])
    cmnd = d * np.arange(1, tau_max) / np.maximum(np.cumsum(d), 1e-12)
    for t in range(tau_min, len(cmnd)):
        if cmnd[t] < 0.12:
            while t + 1 < len(cmnd) and cmnd[t + 1] < cmnd[t]:
                t += 1
            tau = t + 1
            if 1 <= t < len(cmnd) - 1:  # parabolic refinement
                a, b, c = cmnd[t - 1], cmnd[t], cmnd[t + 1]
                denom = a - 2 * b + c
                if denom != 0:
                    tau += 0.5 * (a - c) / denom
            f0 = rate / tau
            result["pitch"] = round(69 + 12 * np.log2(f0 / 440.0), 2)
            break
    return result


# --------------------------------------------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--work", default=os.path.expanduser("~/.cache/sunsymphony-sources"))
    ap.add_argument("--measure", type=int, default=0, help="files to measure per instrument and articulation (0: none)")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "coverage"))
    args = ap.parse_args()
    os.makedirs(args.work, exist_ok=True)

    groups = collections.defaultdict(list)  # (instrument, articulation, source) -> [file records]
    sources_out = {}
    unmatched = collections.Counter()
    folders = {}

    for key in ("vsco2ce", "vcsl"):
        folder, commit, files = git_source(args.work, key)
        folders[key] = folder
        sources_out[key] = dict(SOURCES[key], commit=commit, files=len(files))
        rules = [(re.compile(rx), inst, art) for src, rx, inst, art in RULES if src == key]
        for path in files:
            if not path.lower().endswith(".wav"):
                continue
            for rx, inst, art in rules:
                if rx.search(path):
                    if inst is not None:
                        name = os.path.basename(path)
                        groups[(inst, art, key)].append({"path": path, "note": note_of(name), "dyn": dyn_of(name), "rr": rr_of(name)})
                    break
            else:
                unmatched[(key, "/".join(path.split("/")[:2]))] += 1

    # No Budget Orchestra: the SFZ files and their licenses (the samples are not in the repository).
    folder, commit, files = git_source(args.work, "nbo")
    sources_out["nbo"] = dict(SOURCES["nbo"], commit=commit, files=len(files))
    nbo_licenses = {}
    for path in files:
        top = path.split("/")[0]
        if path.endswith("license.txt"):
            text = git_blob(folder, path).decode("utf-8", "replace").strip()
            nbo_licenses[top] = text.splitlines()[0:4]
        if top in NBO_FOLDERS and path.endswith(".sfz"):
            base = os.path.basename(path).lower()
            art = next(a for rx, a in NBO_ARTICULATIONS if re.search(rx, base)) or "sustain"
            groups[(NBO_FOLDERS[top], art, "nbo")].append({"path": path, "note": None, "dyn": None, "rr": 1, "sfz": True})
    sources_out["nbo"]["licenses"] = {k: v for k, v in sorted(nbo_licenses.items()) if k in NBO_FOLDERS or k == ""}
    if "" not in nbo_licenses:
        top = run("git", "cat-file", "-p", "HEAD:license.txt", cwd=folder).strip().splitlines()[:6]
        sources_out["nbo"]["licenses"]["(top-level license.txt)"] = top
    sources_out["nbo"]["samples_reachable"] = reachable(SOURCES["nbo"]["samples"])
    sources_out["iowa"] = dict(SOURCES["iowa"], reachable=reachable(SOURCES["iowa"]["web"]))

    # Measuring: the lowest, highest and a few middle notes of each group, so the octave convention and the formats are known.
    measured = {}
    if args.measure:
        for (inst, art, src), recs in sorted(groups.items()):
            if src not in folders:
                continue
            pick = sorted([r for r in recs if r["note"] is not None], key=lambda r: r["note"]) or recs
            step = max(1, len(pick) // args.measure)
            chosen = (pick[::step] + [pick[-1]])[: args.measure + 1]
            for r in chosen:
                try:
                    r["measure"] = measure(git_blob(folders[src], r["path"]))
                except Exception as e:  # a file that cannot be read is reported, not fatal
                    r["measure"] = {"error": str(e)}
                print(f"  measured {src}:{r['path']} -> {r['measure']}", file=sys.stderr)
            measured[(inst, art, src)] = chosen

    # The report
    report = {"sources": sources_out, "instruments": {}, "unmatched": {f"{k[0]}:{k[1]}": v for k, v in unmatched.most_common()}}
    lines = ["# SunSymphony - coverage of the free sources (Milestone 0)", "",
             "Generated by `tools/orchestra/inventory.py`. Notes are as the sources name them; the column *octave* says how a",
             "source's names compare with the measured pitch (+12: the file sounds an octave above its name).", "",
             "| Source | Licence | Commit / state | Files |", "|---|---|---|---|"]
    for key, s in sources_out.items():
        state = s.get("commit", "")[:10] or ("reachable" if s.get("reachable") else "not reachable from this machine")
        if key == "nbo":
            state += ", samples " + ("reachable" if s["samples_reachable"] else "not reachable from this machine")
        lines.append(f"| {s['name']} | {s['license']} | {state} | {s.get('files', '-')} |")
    for fam in ("Strings", "Woodwinds", "Brass", "Percussion", "Keys & Harp"):
        lines += ["", f"## {fam}", "", "| Instrument | Articulation | Source | Samples | Notes | Range (named) | Real range | Layers | RR | Octave | Format | Status |",
                  "|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for inst, (family, title, lo, hi, wanted) in TARGETS.items():
            if family != fam:
                continue
            entry = report["instruments"].setdefault(inst, {"name": title, "family": family, "range": [lo, hi], "articulations": {}})
            arts = sorted({a for (i, a, s) in groups if i == inst} | set(wanted), key=lambda a: (a not in wanted, wanted.index(a) if a in wanted else 0, a))
            for art in arts:
                found = [(s, groups[(inst, art, s)]) for s in ("vsco2ce", "vcsl", "nbo") if (inst, art, s) in groups]
                if not found:
                    status = "✗ missing"
                    lines.append(f"| {title} | {art} | - | 0 | - | - | {note_name(lo)}-{note_name(hi)} | - | - | - | - | {status} |")
                    entry["articulations"].setdefault(art, [])
                    continue
                for src, recs in found:
                    notes = sorted({r["note"] for r in recs if r["note"] is not None})
                    layers = sorted({r["dyn"] for r in recs if r["dyn"]})
                    rr = max(r["rr"] for r in recs)
                    octave, fmt = "-", "-"
                    ms = [r for r in measured.get((inst, art, src), []) if r.get("measure") and r["measure"].get("rate")]
                    if ms:
                        offs = [round((r["measure"]["pitch"] - r["note"]) / 12) * 12 for r in ms if r["measure"].get("pitch") and r["note"] is not None]
                        if offs:
                            common = collections.Counter(offs).most_common(1)[0][0]
                            octave = f"{common:+d}" + ("" if offs.count(common) == len(offs) else f" ({offs.count(common)}/{len(offs)})")
                        f0 = ms[0]["measure"]
                        fmt = f"{f0['rate'] // 1000}k/{f0['bits']}/{'st' if f0['channels'] == 2 else 'mono'}"
                    if src == "nbo":
                        status = "SFZ only here (samples on bandshed.net); licence per folder"
                        rng = "-"
                    else:
                        if lo is not None and notes:
                            shift = int(octave.split()[0]) if octave not in ("-",) else 0
                            nlo, nhi = notes[0] + shift, notes[-1] + shift
                            covered = nlo <= lo + 2 and nhi >= hi - 4
                            status = "✓" if covered else "⚠ partial range"
                        else:
                            status = "✓"
                        if len(recs) < 4 and lo is not None:
                            status = "⚠ few samples"
                        rng = f"{note_name(notes[0])}-{note_name(notes[-1])}" if notes else "-"
                    entry["articulations"].setdefault(art, []).append({
                        "source": src, "samples": len(recs), "notes": len(notes), "named_range": [notes[0], notes[-1]] if notes else None,
                        "layers": layers, "round_robins": rr, "octave_offset": octave, "format": fmt, "status": status,
                        "measured": [{"path": r["path"], **r["measure"]} for r in ms]})
                    lines.append(f"| {title} | {art} | {src} | {len(recs)} | {len(notes) or '-'} | {rng} | "
                                 f"{note_name(lo) + '-' + note_name(hi) if lo is not None else 'unpitched'} | {', '.join(layers) or '-'} | {rr} | {octave} | {fmt} | {status} |")
    gaps = [(TARGETS[i][1], a) for i, e in report["instruments"].items() for a, v in e["articulations"].items() if not v and a in TARGETS[i][4]]
    lines += ["", "## Gaps (wanted, found in no source checked here)", ""] + [f"- {t}: {a}" for t, a in gaps]
    lines += ["", "## No Budget Orchestra licences (first lines of each instrument's license.txt)", ""]
    for k, v in sources_out["nbo"]["licenses"].items():
        lines.append(f"- **{k or 'top'}**: " + " / ".join(x.strip() for x in v if x.strip()))
    with open(args.out + ".json", "w") as f:
        json.dump(report, f, indent=1)
    with open(args.out + ".md", "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"written {args.out}.md and .json", file=sys.stderr)


if __name__ == "__main__":
    main()
