#!/usr/bin/env python3
"""SunSymphony: the catalog of instruments its Library manager downloads (0.67.0).

    python3 tools/orchestra/catalog.py --cache DIR [--out resources/orchestra/catalog.json] [--report tools/orchestra/catalog.md]

The sounds come straight from the CC0 sources (VSCO 2 Community Edition and VCSL, Versilian Studios), file by file, from
raw.githubusercontent.com at a fixed commit, so nothing has to be hosted and every byte is checked against the SHA-256 written
here. The plugin reads this catalog (it is built into it), downloads the files of the instruments the user ticks and writes the
SFZ files below next to them.

--cache is a folder holding the source files as <source>/<path in the repository> (the script downloads what is missing). For each
file it records the size and SHA-256 and measures the pitch, so the SFZ maps every sample to the key it really sounds (VSCO names
most of its notes an octave low) and corrects its tuning by a few cents.

How a sample folder becomes an instrument:
  * pitched (strings, winds, brass, mallets, harp, piano): a zone per recorded note, spread halfway to its neighbours (at most 4
    semitones beyond the outermost notes); dynamics become velocity layers, takes become round robins;
  * unpitched percussion (and timpani, whose files are named by drum, not note): one key per kind of stroke from C2 upwards, each
    with its dynamics and takes, played at the recorded pitch (pitch_keytrack=0);
  * percussion, mallets and harp ring on after the key is released (loop_mode=one_shot).
"""
import argparse
import collections
import concurrent.futures
import hashlib
import json
import os
import re
import statistics
import subprocess
import sys
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import inventory as inv  # noqa: E402  (the rules, targets and filename parsing)

SOURCES = {
    "vsco2ce": {"name": "VSCO 2 Community Edition (Versilian Studios)", "license": "CC0 1.0",
                "repo": "sgossner/VSCO-2-CE", "commit": "440300901dfe9275fd84e0b7763af1f8443ae62e"},
    "vcsl": {"name": "Versilian Community Sample Library (Versilian Studios)", "license": "CC0 1.0",
             "repo": "sgossner/VCSL", "commit": "c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e"},
}

# Instruments offered for download, in the order the Library page lists them (families follow the plan).
OFFERED = ["violins", "violas", "cellos", "violin-solo", "bass-solo",
           "piccolo", "flute", "oboe", "clarinet", "bassoon",
           "trumpet", "horn", "trombone", "tuba",
           "timpani", "bass-drum", "snare", "cymbals", "suspended-cymbal", "tam-tam", "triangle", "tambourine",
           "glockenspiel", "xylophone", "marimba", "vibraphone", "tubular-bells",
           "harp", "piano"]
# A first set for someone who wants "an orchestra" without choosing: the plan's "Essential" package.
ESSENTIAL = {"violins", "violas", "cellos", "bass-solo", "flute", "oboe", "clarinet", "bassoon", "trumpet", "horn", "trombone",
             "tuba", "timpani", "cymbals", "harp"}
UNPITCHED = {"timpani", "bass-drum", "snare", "cymbals", "suspended-cymbal", "tam-tam", "triangle", "tambourine"}
RINGING = UNPITCHED | {"glockenspiel", "xylophone", "marimba", "vibraphone", "tubular-bells", "harp"}
BELLS = {"glockenspiel", "xylophone", "marimba", "vibraphone", "tubular-bells"}  # no fine tuning from the pitch tracker on these
# Where the tracker cannot decide the octave (bell partials): checked by ear. Glockenspiel files sound an octave above their names
# (G5-C8, the instrument's real range); tubular bells are left as named.
OCTAVE_OVERRIDE = {"glockenspiel": 12, "tubular-bells": 0}
# Files left out of an articulation (piano: the pedal-down takes; the release samples need release triggers, a later milestone).
EXCLUDE = [r"Grand Piano, Steinway B/Sus/", r"/Rel/"]

DYN_ORDER = {"ppp": 0, "pp": 1, "p": 2, "mp": 3, "mf": 4, "f": 5, "ff": 6, "fff": 7,
             "soft": 2, "quiet": 2, "medium": 4, "loud": 6, "hard": 6}
LAYER_SPLITS = {1: [(1, 127)], 2: [(1, 79), (80, 127)], 3: [(1, 52), (53, 95), (96, 127)],
                4: [(1, 40), (41, 72), (73, 100), (101, 127)]}


def splits(n):
    if n in LAYER_SPLITS:
        return LAYER_SPLITS[n]
    edges = [round(1 + i * 127 / n) for i in range(n + 1)]
    return [(edges[i], edges[i + 1] - 1 if i + 1 < n else 127) for i in range(n)]


def dyn_rank(token):
    if token is None:
        return 4
    token = token.lower()
    if token in DYN_ORDER:
        return DYN_ORDER[token]
    mt = re.fullmatch(r"vl?(\d+)", token)
    return int(mt[1]) if mt else 4


def variant_of(name):
    """A percussion file's kind of stroke: its name without dynamics, take numbers, microphone and extension."""
    base = os.path.splitext(name)[0]
    base = re.sub(r"[_\-](Sum|Mid|Close|Far|far|rel|nloop)$", "", base)
    base = re.sub(r"[_\-]rr\d+", "", base, flags=re.I)
    base = re.sub(r"[_\-](ppp|pp|p|mp|mf|f|ff|fff|v\d+|vl\d+)\d*(?=[_\-]|$)", "", base)
    base = re.sub(r"[_\-]\d+(\.\d+)?s?$", "", base)
    return base.strip("_- ")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def url_of(src, path):
    s = SOURCES[src]
    return f"https://raw.githubusercontent.com/{s['repo']}/{s['commit']}/" + urllib.parse.quote(path)


def fetch(cache, src, path):
    dest = os.path.join(cache, src, path)
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return dest
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    subprocess.run(["curl", "-sf", "--retry", "3", "-o", dest + ".part", url_of(src, path)], check=True)
    os.replace(dest + ".part", dest)
    return dest


def file_list(work, src):
    """The source's files at its fixed commit (a blobless clone: a few MB)."""
    folder = os.path.join(work, src)
    if not os.path.isdir(folder):
        subprocess.run(["git", "clone", "-q", "--filter=blob:none", "--no-checkout",
                        f"https://github.com/{SOURCES[src]['repo']}", folder], check=True)
    subprocess.run(["git", "fetch", "-q", "--filter=blob:none", "origin", SOURCES[src]["commit"]], cwd=folder, check=False)
    out = subprocess.run(["git", "ls-tree", "-r", "--name-only", SOURCES[src]["commit"]], cwd=folder, check=True,
                         capture_output=True, text=True).stdout
    return out.splitlines()


def build(args):
    groups = collections.defaultdict(list)  # (instrument, articulation, source) -> paths
    for src in SOURCES:
        rules = [(re.compile(rx), i, a) for s, rx, i, a in inv.RULES if s == src]
        for path in file_list(args.work, src):
            if not path.lower().endswith(".wav") or any(re.search(x, path) for x in EXCLUDE):
                continue
            for rx, inst, art in rules:
                if rx.search(path):
                    if inst in OFFERED:
                        groups[(inst, art, src)].append(path)
                    break
    # One source per articulation: the one with more files (more notes, layers or takes).
    chosen = {}
    for (inst, art, src), paths in groups.items():
        if (inst, art) not in chosen or len(paths) > len(groups[(inst, art, chosen[(inst, art)])]):
            chosen[(inst, art)] = src

    jobs = [(src, p) for (inst, art), src in chosen.items() for p in groups[(inst, art, src)]]
    print(f"{len(jobs)} files", file=sys.stderr)

    def describe(job):
        src, path = job
        local = fetch(args.cache, src, path)
        with open(local, "rb") as f:
            data = f.read()
        info = inv.measure(data)
        return job, {"size": len(data), "sha256": hashlib.sha256(data).hexdigest(), **info}

    facts = {}
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        for i, (job, info) in enumerate(ex.map(describe, jobs)):
            facts[job] = info
            if i % 200 == 0:
                print(f"  {i}/{len(jobs)}", file=sys.stderr)

    catalog = {"format": 1, "sources": {k: {"name": v["name"], "license": v["license"],
                                            "base": f"https://raw.githubusercontent.com/{v['repo']}/{v['commit']}/"}
                                        for k, v in SOURCES.items()},
               "instruments": []}
    report = ["# SunSymphony - downloadable instruments", "",
              "Generated by `tools/orchestra/catalog.py` (the catalog built into the plugin: `resources/orchestra/catalog.json`).", "",
              "| Family | Instrument | Articulations | Files | MB | Range | Octave fix | Notes |", "|---|---|---|---|---|---|---|---|"]
    for inst in OFFERED:
        family, title, lo_real, hi_real, _ = inv.TARGETS[inst]
        arts = sorted([a for (i, a) in chosen if i == inst], key=lambda a: (a.split(" ")[0] not in ("sustain",), a))
        if not arts:
            continue
        entry = {"id": inst, "name": title, "family": family, "essential": inst in ESSENTIAL, "articulations": [], "files": []}
        notes_out, fixes, lo_all, hi_all = [], [], 127, 0
        used = {}
        for art in arts:
            src = chosen[(inst, art)]
            paths = sorted(groups[(inst, art, src)])
            fname = re.sub(r"[^a-z0-9]+", "-", art.lower()).strip("-") + ".sfz"
            recs = []
            for p in paths:
                name = os.path.basename(p)
                f = facts[(src, p)]
                dest = f"samples/{src}/{name}"
                if dest in used and used[dest] != (src, p):
                    dest = f"samples/{src}/{hashlib.sha1(p.encode()).hexdigest()[:6]}-{name}"
                used[dest] = (src, p)
                recs.append({"src": src, "path": p, "dest": dest, "name": name, "note": inv.note_of(name),
                             "dyn": inv.dyn_of(name), "rr": inv.rr_of(name), **f})
            ringing = inst in RINGING
            lines = [f"// {title} - {art}. Generated by Sun Loops Studio's tools/orchestra/catalog.py.",
                     f"// Samples: {SOURCES[src]['name']}, {SOURCES[src]['license']}.", "",
                     "<control>", "", "<global>"]
            peak = max((r["peak_db"] for r in recs if r.get("peak_db") is not None), default=-6.0)
            gain = round(min(12.0, -6.0 - peak), 1)  # the loudest take peaks near -6 dBFS
            g = [f"volume={gain}"]
            if ringing:
                g.append("loop_mode=one_shot ampeg_release=1.5")
            elif art.split(" ")[0] in ("staccato", "spiccato", "pizzicato"):
                g.append("ampeg_release=0.25")
            else:
                g.append("ampeg_release=0.4")
            lines.append(" ".join(g))
            if inst not in UNPITCHED and any(r["note"] is not None for r in recs):
                recs = [r for r in recs if r["note"] is not None]  # (a pitched instrument's odd file without a note is left out)
            if inst in UNPITCHED or all(r["note"] is None for r in recs):
                # Kit-style: one key per kind of stroke.
                by_var = collections.defaultdict(list)
                for r in recs:
                    by_var[variant_of(r["name"])].append(r)
                key = 36
                legend = []
                for var in sorted(by_var):
                    vr = by_var[var]
                    lines += ["", f"// {inv.note_name(key)}: {var}", f"<group> key={key} pitch_keytrack=0"]
                    lines += layered(vr)
                    legend.append(f"{inv.note_name(key)} {var}")
                    lo_all, hi_all = min(lo_all, key), max(hi_all, key)
                    key += 1
                    if key > 96:
                        break
                notes_out.append(f"{art}: " + ", ".join(legend))
            else:
                # The octave convention, from the measured pitches (the bells' pitch is not trusted).
                diffs = [r["pitch"] - r["note"] for r in recs if r.get("pitch") is not None]
                offset = 0
                if inst in OCTAVE_OVERRIDE:
                    offset = OCTAVE_OVERRIDE[inst]
                elif diffs:
                    octs = [round(d / 12) * 12 for d in diffs]
                    common = statistics.mode(octs)
                    if octs.count(common) >= (0.6 if inst in BELLS else 0.7) * len(octs):
                        offset = common
                if offset:
                    fixes.append(f"{art} {offset:+d}")
                for r in recs:
                    r["root"] = r["note"] + offset
                    r["tune"] = 0
                    if r.get("pitch") is not None and inst not in BELLS:
                        cents = (r["pitch"] - r["root"]) * 100
                        if abs(cents) <= 40:
                            r["tune"] = int(round(-cents))
                lines += zones(recs, lo_real, hi_real)
                roots = [r["root"] for r in recs]
                lo_all, hi_all = min(lo_all, min(roots)), max(hi_all, max(roots))
            entry["articulations"].append({"name": art, "file": fname, "sfz": "\n".join(lines) + "\n"})
            for r in recs:
                entry["files"].append({"source": r["src"], "path": r["path"], "dest": r["dest"], "size": r["size"],
                                       "sha256": r["sha256"]})
        entry["bytes"] = sum(f["size"] for f in entry["files"])
        catalog["instruments"].append(entry)
        rng = f"{inv.note_name(lo_all)}-{inv.note_name(hi_all)}" if hi_all >= lo_all else "-"
        report.append(f"| {family} | {title}{' (essential)' if inst in ESSENTIAL else ''} | {', '.join(arts)} | {len(entry['files'])} | "
                      f"{entry['bytes'] / 1e6:.0f} | {rng} | {', '.join(fixes) or '-'} | {'; '.join(notes_out) or ''} |")
    total = sum(i["bytes"] for i in catalog["instruments"])
    essential = sum(i["bytes"] for i in catalog["instruments"] if i["essential"])
    report += ["", f"All: {total / 1e9:.2f} GB; essential set: {essential / 1e9:.2f} GB."]
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(catalog, f, separators=(",", ":"))
    with open(args.report, "w") as f:
        f.write("\n".join(report) + "\n")
    print(f"written {args.out} ({os.path.getsize(args.out) // 1024} KB) and {args.report}", file=sys.stderr)


def layered(recs):
    """Velocity layers by dynamic, round robins by take, for regions that share a key range (given by the enclosing group)."""
    out = []
    ranks = sorted({dyn_rank(r["dyn"]) for r in recs})
    for (lo, hi), rank in zip(splits(len(ranks)), ranks):
        takes = sorted([r for r in recs if dyn_rank(r["dyn"]) == rank], key=lambda r: (r["rr"], r["name"]))
        for i, r in enumerate(takes):
            seq = f" seq_length={len(takes)} seq_position={i + 1}" if len(takes) > 1 else ""
            out.append(f"<region> sample={r['dest']} lovel={lo} hivel={hi}{seq}")
    return out


def zones(recs, lo_real, hi_real):
    """Pitched: each dynamic layer gets its own key zones (the layers do not always have the same notes)."""
    out = []
    ranks = sorted({dyn_rank(r["dyn"]) for r in recs})
    for (vlo, vhi), rank in zip(splits(len(ranks)), ranks):
        layer = [r for r in recs if dyn_rank(r["dyn"]) == rank]
        roots = sorted({r["root"] for r in layer})
        out += ["", f"<group> lovel={vlo} hivel={vhi}"]
        for i, root in enumerate(roots):
            lo = (roots[i - 1] + root) // 2 + 1 if i > 0 else max(0, root - 4)
            hi = (root + roots[i + 1]) // 2 if i + 1 < len(roots) else min(127, root + 4)
            takes = sorted([r for r in layer if r["root"] == root], key=lambda r: (r["rr"], r["name"]))
            for k, r in enumerate(takes):
                seq = f" seq_length={len(takes)} seq_position={k + 1}" if len(takes) > 1 else ""
                tune = f" tune={r['tune']}" if r["tune"] else ""
                out.append(f"<region> sample={r['dest']} lokey={lo} hikey={hi} pitch_keycenter={root}{tune}{seq}")
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--work", default=os.path.expanduser("~/.cache/sunsymphony-sources"), help="the blobless clones")
    ap.add_argument("--cache", required=True, help="the sample files, as <source>/<path>")
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("--out", default=os.path.join(root, "resources", "orchestra", "catalog.json"))
    ap.add_argument("--report", default=os.path.join(root, "tools", "orchestra", "catalog.md"))
    ap.add_argument("--jobs", type=int, default=8)
    build(ap.parse_args())
