"""Ryo: lo-fi hip hop, 92 BPM, C minor - the demo project made from the Ryo sketch (resources/demos/ryo.slp).

The sketch's ideas are kept - the boom-bap kick and snare, the eighth hats with the pedal-hat ghosts, the hook in octaves
(C C Eb C | C C Bb C | Eb Eb C Eb | Bb C Eb F Eb), the bass rhythm, the plucked counter-line, the high bell arpeggio - and
given the chords they suggest (Cm9 - Ebmaj9 - Abmaj9 - Fm9, a bridge on Abmaj9 - Bb6/9 - Gm7 - Cm9) and a 64-bar form:

  bars  1-4   intro     electric piano behind a closed filter, tape hiss and vinyl crackle
  bars  5-12  beat in   the drums through a low-passed drum bus that opens up; the bass from bar 9
  bars 13-28  verse     everything; the hook (kalimba) from bar 17; a tape stop on the drums at the end
  bars 29-36  bridge    no kick: shaker, vibraphone arpeggios, a pad
  bars 37-52  verse 2   the hook, the plucked counter-line
  bars 53-64  outro     the drums leave, the keys close down, the last chord and a tape stop

Run from the repository: python3 tools/demos/ryo.py <catalog.json> resources/demos/ryo.slp
"""
import sys

from slp import BAR, Catalog, E, Q, S, Song, n

BPM = 92.0
DOTTED_EIGHTH_MS = round(60000.0 / BPM * 0.75, 1)  # 489.1 ms
QUARTER_MS = round(60000.0 / BPM, 1)

# (bass root, keys voicing, chord tones for arpeggios) - one bar each
VERSE = [
    (n("C2"), [n("Bb3"), n("D4"), n("Eb4"), n("G4")], [n("C4"), n("Eb4"), n("G4"), n("Bb4"), n("D5")]),     # Cm9
    (n("Eb2"), [n("Bb3"), n("D4"), n("F4"), n("G4")], [n("Eb4"), n("G4"), n("Bb4"), n("D5"), n("F5")]),     # Ebmaj9
    (n("Ab1"), [n("G3"), n("Bb3"), n("C4"), n("Eb4")], [n("Ab3"), n("C4"), n("Eb4"), n("G4"), n("Bb4")]),   # Abmaj9
    (n("F1"), [n("Ab3"), n("C4"), n("Eb4"), n("G4")], [n("F3"), n("Ab3"), n("C4"), n("Eb4"), n("G4")]),     # Fm9
]
BRIDGE = [
    (n("Ab1"), [n("G3"), n("Bb3"), n("C4"), n("Eb4")], [n("Ab3"), n("C4"), n("Eb4"), n("G4"), n("Bb4")]),   # Abmaj9
    (n("Bb1"), [n("G3"), n("C4"), n("D4"), n("F4")], [n("Bb3"), n("D4"), n("F4"), n("G4"), n("C5")]),       # Bb6/9
    (n("G1"), [n("F3"), n("Bb3"), n("D4"), n("G4")], [n("G3"), n("Bb3"), n("D4"), n("F4"), n("A4")]),       # Gm9
    (n("C2"), [n("Bb3"), n("D4"), n("Eb4"), n("G4")], [n("C4"), n("Eb4"), n("G4"), n("Bb4"), n("D5")]),     # Cm9
]
# The sketch's hook (pattern 5), one octave: (pitch, beat, beats) per bar.
HOOK = [
    [("C5", 0, 0.5), ("C5", 1, 0.5), ("Eb5", 1.5, 0.5), ("C5", 2, 0.5), ("C5", 2.75, 0.5), ("C5", 3.5, 0.5)],
    [("C5", 0, 0.5), ("C5", 1, 0.5), ("Bb4", 1.5, 0.5), ("C5", 2, 0.5), ("C5", 2.75, 0.5), ("C5", 3.5, 0.5)],
    [("Eb5", 0, 0.5), ("Eb5", 1, 0.5), ("C5", 1.5, 0.5), ("Eb5", 2, 0.5), ("Eb5", 2.75, 0.5), ("Eb5", 3.5, 0.5)],
    [("Bb4", 0, 0.5), ("C5", 1, 0.5), ("Eb5", 1.5, 0.5), ("F5", 2, 0.5), ("Eb5", 2.75, 0.5), ("Eb5", 3.5, 0.75)],
]


def build(cat):
    s = Song(cat, BPM, seed=92)

    # --- channels -----------------------------------------------------------------------------------------------------------
    kick = s.sampler("Kick", "lib:kits/studio/kdruml-4", volume=1.0)
    snare = s.sampler("Snare", "lib:kits/studio/snare-4", volume=0.8)
    rim = s.sampler("Rim", "gen:boom/rim/3", volume=0.6, pan=0.15)
    hat = s.sampler("Hat", "lib:kits/studio/hihatclosed-4", volume=1.5, pan=0.2)
    ped = s.sampler("Pedal Hat", "gen:boom/pedal-hat/4", volume=0.62, pan=-0.2)
    ohat = s.sampler("Open Hat", "lib:kits/studio/hihatopen-4", volume=0.7, pan=0.2)
    shaker = s.sampler("Shaker", "gen:percussion/shaker/4", volume=0.63, pan=-0.3)
    keys = s.plugin("Keys", "SunBurst", "Electric Piano", volume=0.24)
    bass = s.program("Bass", "electric-bass", volume=1.0)
    hook = s.plugin("Hook", "SunBurst", "Kalimba", volume=0.6, pan=0.1)
    pluck = s.plugin("Pluck", "HexForge", "Liquid Pluck", volume=0.35, pan=-0.25)
    vibes = s.plugin("Vibes", "SunBurst", "Vibraphone", volume=0.45, pan=0.25)
    pad = s.plugin("Pad", "SunBurst", "Warm Pad", volume=0.25)
    hiss = s.synth("Tape Hiss", "Soft Pad", volume=0.3,
                   oscillators=[{"wave": "saw", "level": 0.0, "octave": 0, "semitones": 0, "fine": 0.0, "pulseWidth": 0.5}] * 2,
                   noise=1.0, cutoff=0.75, envAmount=0.0)
    crackle = s.sampler("Crackle", "lib:kits/studio/hihatclosed-1", volume=1.2)
    for ch in (hat, ped, ohat, shaker):
        s.channels[ch]["swing"] = 0.35  # the off-beat sixteenths lag: the lazy feel
    drums = [kick, snare, rim, hat, ped, ohat, shaker]

    P = {}

    def pat(name, bars=4):
        P[name] = s.pattern(name, bars)
        return P[name]

    def late(ticks):  # a little behind the beat, never ahead
        return ticks + s.rng.randint(0, 14)

    # --- drums ----------------------------------------------------------------------------------------------------------------
    kick_bars = [[0, 0.75, 2.5, 3.5], [0, 0.75, 1.5, 2.25, 2.5, 3.5]]  # the sketch's boom-bap kick (pattern 2)
    p = pat("Beat")
    for b in range(4):
        for beat in kick_bars[b % 2]:
            s.note(p, kick, 60, b * BAR + int(beat * Q), S, 0.95 if beat == 0 else 0.8, humanize=0.06)
        for beat in (1, 3):
            s.note(p, snare, 60, late(b * BAR + beat * Q), S, 0.9, humanize=0.05)
        s.note(p, snare, 60, b * BAR + 3 * Q + 3 * S, S, 0.25)  # ghost
    p = pat("Beat end")  # bar 4: the kick stops, a rim and a snare fill
    for b in range(4):
        for beat in kick_bars[b % 2]:
            if b == 3 and beat >= 2:
                continue
            s.note(p, kick, 60, b * BAR + int(beat * Q), S, 0.95 if beat == 0 else 0.8, humanize=0.06)
        for beat in (1, 3):
            s.note(p, snare, 60, late(b * BAR + beat * Q), S, 0.9, humanize=0.05)
    for k, st in enumerate((10, 13, 14, 15)):
        s.note(p, snare, 60, 3 * BAR + st * S, S, 0.35 + 0.12 * k)

    p = pat("Hats")
    for b in range(4):
        for e in range(8):
            s.note(p, hat, 60, b * BAR + e * E, S, 0.7 if e % 2 == 0 else 0.45, humanize=0.15)
        for st in (3, 7, 11, 13, 15):  # the sketch's pedal hat between them
            s.note(p, ped, 60, b * BAR + st * S, S, 0.5, humanize=0.2)
        if b == 3:
            s.note(p, ohat, 60, b * BAR + 3 * Q + E, E, 0.6)
    p = pat("Rim")
    for b in range(4):
        for st in ((6, 14) if b % 2 == 0 else (6, 11, 14)):
            s.note(p, rim, 60, late(b * BAR + st * S), S, 0.5, humanize=0.2)
    p = pat("Shaker")
    for b in range(4):
        for st in range(16):
            s.note(p, shaker, 60, b * BAR + st * S, S, 0.6 if st % 4 == 2 else 0.3, humanize=0.2)
    p = pat("Bridge beat")  # no kick: a rim on 2 and 4, a soft snare on 4
    for b in range(4):
        s.note(p, rim, 60, late(b * BAR + Q), S, 0.7)
        s.note(p, snare, 60, late(b * BAR + 3 * Q), S, 0.55)

    # Texture: tape hiss (one long note) and vinyl crackle (a few random ticks).
    p = pat("Texture", 8)
    s.note(p, hiss, n("C5"), 0, 8 * BAR, 0.6)
    for _ in range(90):
        st = s.rng.randrange(0, 8 * 32)
        s.note(p, crackle, s.rng.choice([60, 67, 72, 79]), st * S // 2, S // 2, s.rng.uniform(0.15, 0.5))

    # --- harmony -------------------------------------------------------------------------------------------------------------
    def keys_part(name, chords, last_held=False):
        p = pat(name)
        for b, (_, voicing, _) in enumerate(chords):
            at = b * BAR
            if last_held and b == 3:
                s.chord(p, keys, voicing, late(at), BAR - 20, 0.68, humanize=0.06, strum=14)
                continue
            s.chord(p, keys, voicing, late(at), int(1.5 * Q), 0.7, humanize=0.08, strum=12)
            s.chord(p, keys, voicing, late(at + 2 * Q + E), int(1.25 * Q), 0.55, humanize=0.08, strum=10)
            if b % 2 == 1:  # a lone top note answers
                s.note(p, keys, voicing[-1] + 12 if voicing[-1] < n("A4") else voicing[-1], late(at + 3 * Q + E), E, 0.4)
        return p

    keys_part("Keys", VERSE)
    keys_part("Keys bridge", BRIDGE)
    p = pat("Keys end", 4)  # the last chord, held
    s.chord(p, keys, [n("C3")] + VERSE[0][1] + [n("C5")], 0, 3 * BAR, 0.65, strum=25)

    def bass_part(name, chords):
        p = pat(name)
        for b, (root, _, _) in enumerate(chords):
            at = b * BAR
            nxt = chords[(b + 1) % 4][0]
            approach = nxt - 1 if (nxt - root) % 12 not in (1, 11) else nxt + 2
            s.note(p, bass, root, at, int(0.7 * Q), 0.9, humanize=0.05)
            s.note(p, bass, root, at + int(0.75 * Q), int(0.5 * Q), 0.7, humanize=0.05)
            s.note(p, bass, root + 12 if b % 2 else root + 7, at + int(1.5 * Q), int(0.45 * Q), 0.6, humanize=0.05)
            s.note(p, bass, root, at + int(2.5 * Q), int(0.9 * Q), 0.85, humanize=0.05)
            s.note(p, bass, approach, at + int(3.5 * Q), int(0.45 * Q), 0.65, humanize=0.05)
        return p

    bass_part("Bass", VERSE)
    bass_part("Bass bridge", BRIDGE)

    p = pat("Hook")
    for b, bar in enumerate(HOOK):
        for pitch, beat, beats in bar:
            s.note(p, hook, n(pitch), late(b * BAR + int(beat * Q)), int(beats * Q) - 10, 0.85 if beat in (0, 2) else 0.7, humanize=0.08)
    p = pat("Hook octaves")  # the sketch's octaves, the lower one softer
    for b, bar in enumerate(HOOK):
        for pitch, beat, beats in bar:
            at = late(b * BAR + int(beat * Q))
            s.note(p, hook, n(pitch), at, int(beats * Q) - 10, 0.85 if beat in (0, 2) else 0.7, humanize=0.08)
            s.note(p, hook, n(pitch) - 12, at, int(beats * Q) - 10, 0.5, humanize=0.08)

    # The counter-line (the sketch's pattern 8): a low and a high note around each beat, on the chord's root and fifth.
    p = pat("Pluck")
    for b, (root, _, tones) in enumerate(VERSE):
        low, high = tones[0], tones[2]
        for beat in range(4):
            s.note(p, pluck, low if beat % 2 == 0 else tones[1], late(b * BAR + beat * Q + S), S, 0.55, humanize=0.1)
            s.note(p, pluck, high + (12 if beat == 3 else 0), late(b * BAR + beat * Q + 3 * S), E, 0.6, humanize=0.1)

    # Vibraphone arpeggios (the sketch's bells, pattern 10, an octave lower): up the chord in eighths, then down.
    def vibes_part(name, chords):
        p = pat(name)
        for b, (_, _, tones) in enumerate(chords):
            figure = [0, 1, 2, 3, 4, 3, 2, 1]
            for e, k in enumerate(figure):
                s.note(p, vibes, tones[k] + 12, late(b * BAR + e * E), E + 60, 0.65 if e % 4 == 0 else 0.5, humanize=0.1)
        return p

    vibes_part("Vibes bridge", BRIDGE)
    vibes_part("Vibes", VERSE)

    def pad_part(name, chords):
        p = pat(name)
        for b, (_, voicing, _) in enumerate(chords):
            s.chord(p, pad, [x + 12 for x in voicing[1:]], b * BAR, BAR - 10, 0.6)
        return p

    pad_part("Pad", VERSE)
    pad_part("Pad bridge", BRIDGE)

    # --- arrangement --------------------------------------------------------------------------------------------------------
    pl = s.place
    for start in range(0, 64, 8):
        pl(P["Texture"], start, 8)
    pl(P["Keys"], 0, 4); pl(P["Pad"], 0, 4)
    # beat in 4-11
    pl(P["Keys"], 4, 8)
    pl(P["Beat"], 4, 4); pl(P["Beat end"], 8, 4)
    pl(P["Hats"], 4, 8)
    pl(P["Bass"], 8, 4)
    # verse 12-27
    pl(P["Keys"], 12, 16); pl(P["Bass"], 12, 16); pl(P["Hats"], 12, 16)
    pl(P["Beat"], 12, 12); pl(P["Beat end"], 24, 4)
    pl(P["Rim"], 16, 12)
    pl(P["Hook"], 16, 8); pl(P["Hook octaves"], 24, 4)
    # bridge 28-35
    pl(P["Keys bridge"], 28, 8); pl(P["Bass bridge"], 28, 8); pl(P["Vibes bridge"], 28, 8); pl(P["Pad bridge"], 28, 8)
    pl(P["Bridge beat"], 28, 8); pl(P["Shaker"], 28, 8)
    # verse 2 36-51
    pl(P["Keys"], 36, 16); pl(P["Bass"], 36, 16); pl(P["Hats"], 36, 16); pl(P["Rim"], 36, 16)
    pl(P["Beat"], 36, 12); pl(P["Beat end"], 48, 4)
    pl(P["Hook octaves"], 36, 8); pl(P["Hook"], 44, 8)
    pl(P["Pluck"], 40, 12)
    pl(P["Shaker"], 44, 8)
    # outro 52-63
    pl(P["Keys"], 52, 8); pl(P["Bass"], 52, 4)
    pl(P["Hats"], 52, 4); pl(P["Beat end"], 52, 4)
    pl(P["Hook"], 52, 4)
    pl(P["Vibes"], 56, 4); pl(P["Pad"], 56, 4)
    pl(P["Keys end"], 60, 4)

    # --- mixer --------------------------------------------------------------------------------------------------------------
    eff = cat.effect
    t = s.tracks
    t[0]["name"] = "Audio FX"
    kick_track = s.track_number(kick)

    # The drum bus: every drum goes through it (low-passed in the intro, crushed a little, glued, a tape stop at bar 28).
    drum_bus = s.bus("Drum Bus")
    for ch in drums:
        s.route(s.channels[ch]["mixerTrack"], drum_bus)
    s.set_effects(t[drum_bus]["effects"],
                  eff("Filter", Type=0, Cutoff=20000.0, Resonance=0.15),
                  eff("Squeeze", Bits=12.0, Downsample=2.0, Puncher=0.15, Filter=11000.0, Mix=0.6),
                  eff("Compressor", Threshold=-24.0, Ratio=4.0, Attack=8.0, Release=120.0, Makeup=5.0),
                  eff("Time Shaper", Time=3.0, Volume=0.0, Cycle=1.0, Mix=0.0))
    t[drum_bus]["sends"] = [0.06, 0.0]
    t[drum_bus]["vca"] = 0b10
    s.set_effects(s.mixer_of(kick)["effects"],
                  eff("Parametric EQ", Low_freq=60.0, Low_gain=3.0, Mid_freq=400.0, Mid_gain=-4.0, Mid_Q=1.0, High_freq=5000.0, High_gain=-2.0))
    s.set_effects(s.mixer_of(snare)["effects"],
                  eff("Filter", Type=1, Cutoff=150.0),
                  eff("Parametric EQ", Mid_freq=220.0, Mid_gain=2.0, High_freq=8000.0, High_gain=-3.0))
    s.mixer_of(snare)["sends"] = [0.2, 0.0]
    for ch in (hat, ped, ohat, shaker, rim):
        s.set_effects(s.mixer_of(ch)["effects"], eff("Filter", Type=1, Cutoff=350.0), eff("Filter", Type=0, Cutoff=9000.0))
    s.mixer_of(hat)["volume"] = 1.5
    s.mixer_of(rim)["sends"] = [0.2, 0.1]

    s.set_effects(s.mixer_of(keys)["effects"],
                  eff("Filter", Type=0, Cutoff=6000.0, Resonance=0.1),
                  eff("Parametric EQ", Low_freq=150.0, Low_gain=-4.0, Mid_freq=800.0, Mid_gain=1.5, Mid_Q=0.7, High_freq=7000.0, High_gain=-2.0),
                  eff("Chorus", Rate=0.5, Depth=3.0, Mix=0.3),
                  eff("Vibrato", Rate=0.7, Depth=9.0, Stereo=0.3),
                  eff("Squeeze", Bits=12.0, Downsample=1.0, Filter=12000.0, Mix=0.5),
                  eff("Compressor", Threshold=-20.0, Ratio=2.5, Attack=15.0, Release=200.0, Makeup=2.0))
    s.mixer_of(keys)["sends"] = [0.28, 0.12]
    s.set_effects(s.mixer_of(bass)["effects"],
                  eff("Filter", Type=0, Cutoff=1800.0, Resonance=0.05),
                  eff("Parametric EQ", Low_freq=80.0, Low_gain=2.0, Mid_freq=300.0, Mid_gain=-2.0, Mid_Q=1.0),
                  eff("Compressor", Threshold=-20.0, Ratio=4.0, Attack=5.0, Release=150.0, Makeup=2.0),
                  eff("Compressor", Threshold=-24.0, Ratio=3.0, Attack=1.0, Release=120.0, Sidechain=float(kick_track)))
    s.set_effects(s.mixer_of(hook)["effects"],
                  eff("Filter", Type=1, Cutoff=250.0),
                  eff("Delay", Time=DOTTED_EIGHTH_MS, Feedback=0.35, Tone=0.4, Mix=0.25, Mode=2.0, Wobble=0.35),
                  eff("Vibrato", Rate=0.6, Depth=7.0))
    s.mixer_of(hook)["sends"] = [0.3, 0.0]
    s.set_effects(s.mixer_of(pluck)["effects"],
                  eff("Filter", Type=0, Cutoff=3500.0, Resonance=0.2),
                  eff("Filter", Type=1, Cutoff=200.0))
    s.mixer_of(pluck)["sends"] = [0.2, 0.3]
    s.set_effects(s.mixer_of(vibes)["effects"], eff("Filter", Type=1, Cutoff=300.0), eff("Chorus", Rate=0.3, Depth=2.0, Mix=0.25))
    s.mixer_of(vibes)["sends"] = [0.35, 0.2]
    s.set_effects(s.mixer_of(pad)["effects"], eff("Filter", Type=1, Cutoff=300.0), eff("Filter", Type=0, Cutoff=5000.0),
                  eff("Stereo Imager", Width=150.0))
    s.mixer_of(pad)["sends"] = [0.4, 0.0]
    s.set_effects(s.mixer_of(hiss)["effects"], eff("Filter", Type=1, Cutoff=2500.0), eff("Filter", Type=0, Cutoff=9000.0))
    s.set_effects(s.mixer_of(crackle)["effects"], eff("Filter", Type=1, Cutoff=2000.0),
                  eff("Squeeze", Bits=12.0, Downsample=3.0, Filter=8000.0, Mix=1.0))
    for ch in (keys, hook, pluck, vibes, pad):
        s.mixer_of(ch)["vca"] = 0b01

    s.aux[0]["name"] = "Room"
    s.set_effects(s.aux[0]["effects"],
                  eff("Reverb", Size=0.6, Damping=0.65, Width=1.0, Mix=1.0, Type=1.0, Pre_delay=15.0),
                  eff("Parametric EQ", Low_freq=250.0, Low_gain=-10.0, High_freq=6000.0, High_gain=-4.0))
    s.aux[0]["volume"] = 0.8
    s.aux[1]["name"] = "Tape Delay"
    s.set_effects(s.aux[1]["effects"],
                  eff("Delay", Time=QUARTER_MS, Feedback=0.4, Tone=0.35, Mix=1.0, Mode=2.0, Wobble=0.4),
                  eff("Filter", Type=1, Cutoff=300.0))
    s.aux[1]["volume"] = 0.55

    s.set_effects(s.master,
                  eff("Parametric EQ", Low_freq=45.0, Low_gain=-1.5, Mid_freq=3000.0, Mid_gain=-1.0, Mid_Q=0.7, High_freq=10000.0, High_gain=-2.0),
                  eff("Compressor", Threshold=-14.0, Ratio=2.0, Attack=30.0, Release=200.0, Makeup=1.0),
                  eff("Time Shaper", Time=3.0, Volume=0.0, Cycle=2.0, Mix=0.0),
                  eff("Maximizer", Input=8.0, Ceiling=-2.0, Release=120.0, Glue=0.5, Soft_clip=0.3))

    # --- automation ---------------------------------------------------------------------------------------------------------
    cutoff = cat.param_index("Filter", "Cutoff")
    mix = cat.param_index("Time Shaper", "Mix")
    keys_track = s.channels[keys]["mixerTrack"]
    # Lane 1: the drum bus opens up (bars 5-12); the keys close down in the outro.
    s.automate("trackEffect", 0, 4, 8, [(0, 600.0, "smooth"), (7 * BAR, 6000.0, "smooth"), (8 * BAR, 20000.0, "smooth")],
               index=drum_bus, slot=0, param=cutoff)
    s.automate("trackEffect", 0, 56, 8, [(0, 6000.0, "smooth"), (8 * BAR, 700.0, "smooth")], index=keys_track, slot=0, param=cutoff)
    # Lane 2: the keys come out of the filter in the intro.
    s.automate("trackEffect", 1, 0, 4, [(0, 500.0, "smooth"), (4 * BAR, 6000.0, "smooth")], index=keys_track, slot=0, param=cutoff)
    # Lane 3: a tape stop on the drums in the last two beats of the verse; on everything in the last bar.
    s.automate("trackEffect", 2, 27, 1, [(0, 0.0, "hold"), (2 * Q, 100.0, "hold"), (BAR - 1, 0.0, "hold")],
               index=drum_bus, slot=3, param=mix)
    s.automate("masterEffect", 2, 63, 1, [(0, 0.0, "hold"), (2 * Q, 100.0, "hold"), (BAR - 1, 100.0, "hold")], slot=2, param=mix)
    # Lane 4: the keys' room grows in the bridge.
    s.automate("trackSend", 3, 28, 8, [(0, 0.28, "smooth"), (4 * BAR, 0.5, "smooth"), (8 * BAR, 0.28, "smooth")], index=keys_track, slot=0)

    for bar, name in ((0, "Intro"), (12, "Verse"), (28, "Bridge"), (36, "Verse 2"), (52, "Outro")):
        s.marker(bar, name)
    s.current_pattern = P["Hook"]
    return s


if __name__ == "__main__":
    build(Catalog(sys.argv[1])).write(sys.argv[2])
