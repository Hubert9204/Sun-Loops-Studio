"""White Line: melodic techno, 129 BPM, C minor - the demo project made from the White Line sketch (resources/demos/white-line.slp).

The sketch's ideas are kept - the Csus4 pad, the rolling saw bass, the offbeat stabs, the HexForge melody (G F D Eb | D C D Bb G |
C D Eb D | F G D Eb D) in its long and its 3-3-2 versions, the sixteenth pulses - and built into a 96-bar song:

  bars  1-16  intro      kick, hats, the pad opening up (filter automation), a rim; clap, open hat and bass from bar 9
  bars 17-32  groove     the full beat over a C pedal, stabs from bar 25
  bars 33-48  theme      the melody (long notes) over Cm - Bb - Ab - Bb; ride
  bars 49-64  breakdown  no kick: pad, melody, the arp filtered in, then a clap roll, a riser and a high-pass sweep
  bars 65-88  drop       everything, the melody in 3-3-2, the arp in sixteenths
  bars 89-96  outro      the beat on the pedal, the synths fade away (VCA)

Run from the repository: python3 tools/demos/white_line.py <catalog.json> resources/demos/white-line.slp
"""
import sys

from slp import BAR, Catalog, E, Q, S, Song, n

BPM = 129.0
DOTTED_EIGHTH_MS = round(60000.0 / BPM * 0.75, 1)  # 348.8 ms
EIGHTH_MS = round(60000.0 / BPM * 0.5, 1)

# The progression of the theme and the drop: two bars each.
PROGRESSION = [  # (bass root, pad voicing, stab voicing, arp tones)
    (n("C2"), [n("C3"), n("G3"), n("Bb3"), n("Eb4")], [n("G3"), n("Bb3"), n("Eb4")], [n("C4"), n("Eb4"), n("G4"), n("Bb4")]),
    (n("Bb1"), [n("Bb2"), n("F3"), n("Bb3"), n("D4")], [n("F3"), n("Bb3"), n("D4")], [n("Bb3"), n("D4"), n("F4"), n("C5")]),
    (n("Ab1"), [n("Ab2"), n("Eb3"), n("G3"), n("C4")], [n("Eb3"), n("Ab3"), n("C4")], [n("Ab3"), n("C4"), n("Eb4"), n("G4")]),
    (n("Bb1"), [n("Bb2"), n("F3"), n("Bb3"), n("D4")], [n("F3"), n("Bb3"), n("D4")], [n("Bb3"), n("D4"), n("F4"), n("D5")]),
]
CSUS = [n("C3"), n("F3"), n("G3"), n("C4")]  # the sketch's pad chord
# The sketch's melody (pattern 11), two beats a note: (pitch, beat, beats).
MELODY = [("G4", 0, 2), ("F4", 2, 2), ("D4", 4, 2), ("Eb4", 6, 2), ("D4", 8, 2), ("C4", 10, 2), ("D4", 12, 2), ("Bb3", 14, 1.5),
          ("G3", 15.5, 0.5), ("C4", 16, 2), ("D4", 18, 2), ("Eb4", 20, 2), ("D4", 22, 2), ("F4", 24, 2), ("G4", 26, 2),
          ("D4", 28, 2), ("Eb4", 30, 1.5), ("D4", 31.5, 0.5)]


def build(cat):
    s = Song(cat, BPM, seed=129)

    # --- channels (each on its own mixer track, after track 1 for the audio clips) ---------------------------------------
    kick = s.sampler("Kick", "gen:punch/kick/4", volume=1.0)
    clap = s.sampler("Clap", "gen:punch/clap/4", volume=1.5)
    hat = s.sampler("Closed Hat", "gen:punch/closed-hat/4", volume=1.05, pan=0.12)
    ohat = s.sampler("Open Hat", "gen:modern/open-hat/4", volume=0.75, pan=-0.1)
    ride = s.sampler("Ride", "gen:punch/ride/4", volume=0.37, pan=0.25)
    rim = s.sampler("Rim", "gen:modern/rim/4", volume=1.0, pan=-0.3)
    crash = s.sampler("Crash", "gen:punch/crash/4", volume=0.4)
    bass = s.plugin("Rolling Bass", "SunBurst", "Deep House Bass", volume=0.53)
    pad = s.plugin("Pad", "SunBurst", "Dream Pad", volume=0.125)
    stab = s.plugin("Stab", "HexForge", "Neon Pluck", volume=0.85)
    lead = s.plugin("Lead", "HexForge", "Liquid Pluck", volume=0.95)
    arp = s.plugin("Arp", "SunBurst", "Bell Pluck", volume=0.33)
    riser = s.plugin("Riser", "SunBurst", "White Noise Riser", volume=0.5)
    impact = s.plugin("Impact", "SunBurst", "Impact Boom", volume=0.6)
    drums = [kick, clap, hat, ohat, ride, rim, crash]
    synths = [pad, stab, lead, arp]

    # --- patterns --------------------------------------------------------------------------------------------------------
    P = {}

    def pat(name, bars=4):
        P[name] = s.pattern(name, bars)
        return P[name]

    # Drums
    p = pat("Kick")
    for b in range(4):
        for beat in range(4):
            s.note(p, kick, 60, b * BAR + beat * Q, S, 0.95 if beat == 0 else 0.9)
    p = pat("Kick fill")  # the last beat left out: the bar before a change breathes
    for b in range(4):
        for beat in range(4 if b < 3 else 3):
            s.note(p, kick, 60, b * BAR + beat * Q, S, 0.95 if beat == 0 else 0.9)

    p = pat("Hats")
    for b in range(4):
        for st in range(16):
            accent = st % 4 == 2
            s.note(p, hat, 60, b * BAR + st * S, S, 0.75 if accent else (0.45 if st % 2 == 0 else 0.3), humanize=0.12)
    p = pat("Hats light")
    for b in range(4):
        for beat in range(4):
            s.note(p, hat, 60, b * BAR + beat * Q + E, S, 0.6, humanize=0.1)

    p = pat("Clap")
    for b in range(4):
        for beat in (1, 3):
            s.note(p, clap, 60, b * BAR + beat * Q, S, 0.85, humanize=0.05)
        if b == 3:  # a flam into the next phrase
            s.note(p, clap, 60, b * BAR + 3 * Q + 3 * S, S, 0.45)
    p = pat("Off hats")
    for b in range(4):
        for beat in range(4):
            s.note(p, ohat, 60, b * BAR + beat * Q + E, E, 0.7, humanize=0.08)
    p = pat("Ride")
    for b in range(4):
        for beat in range(4):
            s.note(p, ride, 60, b * BAR + beat * Q + E, E, 0.6, humanize=0.1)
    p = pat("Rim")
    for b in range(4):
        for st in (3, 6, 11, 14) if b % 2 == 0 else (3, 7, 10, 13):
            s.note(p, rim, 60, b * BAR + st * S, S, 0.55, humanize=0.15)

    p = pat("Clap roll")  # the build: quarters, eighths, sixteenths, louder and louder; the last beat is silent
    for b in range(4):
        step = Q if b == 0 else E if b == 1 else S
        for t in range(0, BAR, step):
            at = b * BAR + t
            if b == 3 and t >= 3 * Q:
                break
            s.note(p, clap, 60, at, S, 0.3 + 0.6 * at / (4 * BAR))
    p = pat("Crash", 1)
    s.note(p, crash, 60, 0, 2 * Q, 0.8)

    # Bass: the sketch's saw line, rolling off the kick (three sixteenths after each beat), octave jumps and a turn at the end.
    def rolling(name, roots):
        p = pat(name)
        for b in range(4):
            root = roots[b]
            for beat in range(4):
                at = b * BAR + beat * Q
                last = beat == 3 and b % 2 == 1
                tones = [root, root + 12, root] if not last else [root + 12, root + 10, root + 7]
                for k, tone in enumerate(tones):
                    s.note(p, bass, tone, at + (k + 1) * S, S - 10, (0.9, 0.75, 0.8)[k], humanize=0.06)
        return p

    rolling("Bass pedal", [n("C2")] * 4)
    rolling("Bass 1", [PROGRESSION[0][0]] * 2 + [PROGRESSION[1][0]] * 2)
    rolling("Bass 2", [PROGRESSION[2][0]] * 2 + [PROGRESSION[3][0]] * 2)

    # Pads
    p = pat("Pad sus")
    for b in range(4):
        s.chord(p, pad, CSUS if b < 3 else [n("C3"), n("Eb3"), n("G3"), n("C4")], b * BAR, BAR - 10, 0.75)
    p1 = pat("Pad 1")
    p2 = pat("Pad 2")
    for half, pp in enumerate((p1, p2)):
        for k in range(2):
            s.chord(pp, pad, PROGRESSION[half * 2 + k][1], k * 2 * BAR, 2 * BAR - 10, 0.75)

    # Stabs: a 3-3-2 figure (sixteenths 0, 3, 6 ... ) answering the kick.
    figure = [3, 6, 10, 14]

    def stabs(name, voicings):
        p = pat(name)
        for b in range(4):
            for st in figure:
                s.chord(p, stab, voicings[b], b * BAR + st * S, S + 20, 0.8 if st == 3 else 0.65, humanize=0.08)
        return p

    stabs("Stabs pedal", [PROGRESSION[0][2], PROGRESSION[0][2], PROGRESSION[0][2], [n("G3"), n("C4"), n("F4")]])
    stabs("Stabs 1", [PROGRESSION[0][2]] * 2 + [PROGRESSION[1][2]] * 2)
    stabs("Stabs 2", [PROGRESSION[2][2]] * 2 + [PROGRESSION[3][2]] * 2)

    # Melody: the long version (8 bars as two patterns), and the 3-3-2 version of the drop.
    m1 = pat("Melody 1")
    m2 = pat("Melody 2")
    for pitch, beat, beats in MELODY:
        target, offset = (m1, 0) if beat < 16 else (m2, 16)
        s.note(target, lead, n(pitch), int((beat - offset) * Q), int(beats * Q) - 20, 0.85 if beat % 4 == 0 else 0.75)
    r1 = pat("Melody drop 1")
    r2 = pat("Melody drop 2")
    for pitch, beat, beats in MELODY:
        target, offset = (r1, 0) if beat < 16 else (r2, 16)
        at = int((beat - offset) * Q)
        if beats >= 2:  # three hits per two beats: 0, 3, 6 sixteenths (the sketch's pattern 12)
            for k, st in enumerate((0, 3, 6)):
                s.note(target, lead, n(pitch), at + st * S, 2 * S, (0.9, 0.7, 0.75)[k], humanize=0.05)
        else:
            s.note(target, lead, n(pitch), at, int(beats * Q) - 10, 0.75)

    # Arp: sixteenths through the chord, up and over (the sketch's pulses made to follow the chords).
    order = [0, 1, 2, 3, 2, 1, 3, 1]

    def arpeggio(name, chords, density=16):
        p = pat(name)
        for b in range(4):
            tones = chords[b]
            for st in range(16):
                if density == 8 and st % 2:
                    continue
                s.note(p, arp, tones[order[st % 8]], b * BAR + st * S, S - 15, 0.8 if st % 4 == 0 else 0.6, humanize=0.08)
        return p

    arpeggio("Arp 1", [PROGRESSION[0][3]] * 2 + [PROGRESSION[1][3]] * 2)
    arpeggio("Arp 2", [PROGRESSION[2][3]] * 2 + [PROGRESSION[3][3]] * 2)
    arpeggio("Arp eighths 1", [PROGRESSION[0][3]] * 2 + [PROGRESSION[1][3]] * 2, density=8)
    arpeggio("Arp eighths 2", [PROGRESSION[2][3]] * 2 + [PROGRESSION[3][3]] * 2, density=8)

    # Effects: the riser (eight bars), the impact.
    p = pat("Riser", 8)
    s.note(p, riser, n("C4"), 0, 8 * BAR - Q, 0.9)
    p = pat("Impact", 1)
    s.note(p, impact, n("C2"), 0, 2 * Q, 1.0)
    p = pat("Outro pad", 8)
    s.chord(p, pad, CSUS, 0, 8 * BAR - 10, 0.7)

    # --- arrangement (bars counted from 0) ---------------------------------------------------------------------------
    pl = s.place
    # Intro 0-15
    pl(P["Kick"], 0, 12); pl(P["Kick fill"], 12, 4)
    pl(P["Hats light"], 0, 8); pl(P["Hats"], 8, 8)
    pl(P["Pad sus"], 0, 16)
    pl(P["Rim"], 4, 12)
    pl(P["Clap"], 8, 8); pl(P["Off hats"], 8, 8)
    pl(P["Bass pedal"], 12, 4)
    # Groove 16-31
    pl(P["Crash"], 16, 1)
    pl(P["Kick"], 16, 12); pl(P["Kick fill"], 28, 4)
    for name in ("Hats", "Clap", "Off hats", "Rim", "Bass pedal"):
        pl(P[name], 16, 16)
    pl(P["Pad sus"], 16, 8)
    pl(P["Stabs pedal"], 24, 8)
    # Theme 32-47
    pl(P["Crash"], 32, 1); pl(P["Impact"], 32, 1)
    pl(P["Kick"], 32, 12); pl(P["Kick fill"], 44, 4)
    for name in ("Hats", "Clap", "Off hats", "Ride"):
        pl(P[name], 32, 16)
    for start in (32, 40):
        pl(P["Bass 1"], start, 4); pl(P["Bass 2"], start + 4, 4)
        pl(P["Pad 1"], start, 4); pl(P["Pad 2"], start + 4, 4)
        pl(P["Melody 1"], start, 4); pl(P["Melody 2"], start + 4, 4)
        pl(P["Stabs 1"], start, 4); pl(P["Stabs 2"], start + 4, 4)
    # Breakdown 48-63: the beat goes, the harmony stays; the build from 56
    for start in (48, 56):
        pl(P["Pad 1"], start, 4); pl(P["Pad 2"], start + 4, 4)
    pl(P["Melody 1"], 48, 4); pl(P["Melody 2"], 52, 4)
    pl(P["Arp eighths 1"], 52, 4)
    pl(P["Arp 1"], 56, 4); pl(P["Arp 2"], 60, 4)
    pl(P["Hats light"], 52, 4); pl(P["Hats"], 56, 8)
    pl(P["Clap roll"], 60, 4)
    pl(P["Bass 1"], 56, 4); pl(P["Bass 2"], 60, 4)
    pl(P["Riser"], 56, 8)
    # Drop 64-87
    pl(P["Crash"], 64, 1); pl(P["Impact"], 64, 1); pl(P["Crash"], 80, 1)
    pl(P["Kick"], 64, 20); pl(P["Kick fill"], 84, 4)
    for name in ("Hats", "Clap", "Off hats", "Ride", "Rim"):
        pl(P[name], 64, 24)
    for start in (64, 72, 80):
        for a, b in (("Bass 1", "Bass 2"), ("Pad 1", "Pad 2"), ("Melody drop 1", "Melody drop 2"), ("Arp 1", "Arp 2"),
                     ("Stabs 1", "Stabs 2")):
            pl(P[a], start, 4); pl(P[b], start + 4, 4)
    # Outro 88-95
    pl(P["Crash"], 88, 1)
    pl(P["Kick"], 88, 8)
    for name in ("Hats", "Off hats", "Rim", "Bass pedal"):
        pl(P[name], 88, 8)
    pl(P["Outro pad"], 88, 8)
    pl(P["Arp eighths 1"], 88, 4)

    # Reversed cymbals into the theme and the drop (audio clips on the first track).
    crash_ticks = int(4.0 * BPM / 60.0 * Q)  # the 4-second cymbal
    for bar, pan in ((32, -0.25), (64, 0.25)):
        length = min(crash_ticks, 2 * BAR)
        s.audio("Reverse cymbal", "lib:kits/studio/crashl-4", bar * BAR - length, length, gain=0.6, pan=pan, reverse=True,
                fade_in=1.0)

    # --- mixer --------------------------------------------------------------------------------------------------------------
    eff = cat.effect
    t = s.tracks
    t[0]["name"] = "Audio FX"
    s.set_effects(t[0]["effects"], eff("Filter", Type=1, Cutoff=250.0, Resonance=0.1))
    t[0]["sends"] = [0.4, 0.0]
    kick_track = s.track_number(kick)

    s.set_effects(s.mixer_of(kick)["effects"],
                  eff("Parametric EQ", Low_freq=55.0, Low_gain=0.0, Mid_freq=320.0, Mid_gain=-4.0, Mid_Q=1.2, High_freq=6000.0, High_gain=1.0),
                  eff("Compressor", Threshold=-14.0, Ratio=3.0, Attack=15.0, Release=90.0, Makeup=2.0))
    s.set_effects(s.mixer_of(clap)["effects"],
                  eff("Filter", Type=1, Cutoff=200.0, Resonance=0.1),
                  eff("Parametric EQ", Mid_freq=1200.0, Mid_gain=2.0, High_freq=9000.0, High_gain=2.0))
    s.mixer_of(clap)["sends"] = [0.22, 0.0]
    for ch in (hat, ohat, ride):
        s.set_effects(s.mixer_of(ch)["effects"], eff("Filter", Type=1, Cutoff=500.0, Resonance=0.1),
                      eff("Parametric EQ", High_freq=10000.0, High_gain=1.5))
        s.mixer_of(ch)["sends"] = [0.08, 0.0]
    s.mixer_of(ohat)["effects"][2] = eff("Compressor", Threshold=-20.0, Ratio=3.0, Attack=1.0, Release=120.0, Sidechain=float(kick_track))
    s.set_effects(s.mixer_of(rim)["effects"], eff("Filter", Type=1, Cutoff=300.0))
    s.mixer_of(rim)["sends"] = [0.15, 0.25]
    s.set_effects(s.mixer_of(crash)["effects"], eff("Filter", Type=1, Cutoff=400.0))
    s.mixer_of(crash)["sends"] = [0.25, 0.0]

    # Bass: tidy lows, then ducked by the kick (the sidechain hears mixer track `kick_track`).
    s.set_effects(s.mixer_of(bass)["effects"],
                  eff("Filter", Type=1, Cutoff=35.0, Resonance=0.0),
                  eff("Parametric EQ", Low_freq=70.0, Low_gain=1.5, Mid_freq=280.0, Mid_gain=-3.0, Mid_Q=1.0, High_freq=5000.0, High_gain=-2.0),
                  eff("Compressor", Threshold=-24.0, Ratio=6.0, Attack=0.5, Release=110.0, Makeup=1.0, Sidechain=float(kick_track)),
                  eff("Stereo Imager", Width=100.0, Mono_bass=200.0))
    # Pad: low cut, the pumping sidechain, wide.
    s.set_effects(s.mixer_of(pad)["effects"],
                  eff("Filter", Type=1, Cutoff=220.0, Resonance=0.05),
                  eff("Parametric EQ", Mid_freq=350.0, Mid_gain=-3.0, Mid_Q=0.9, High_freq=7000.0, High_gain=-2.0),
                  eff("Compressor", Threshold=-30.0, Ratio=8.0, Attack=1.0, Release=180.0, Makeup=3.0, Sidechain=float(kick_track)),
                  eff("Stereo Imager", Width=140.0))
    s.mixer_of(pad)["sends"] = [0.3, 0.0]
    s.set_effects(s.mixer_of(stab)["effects"],
                  eff("Filter", Type=1, Cutoff=280.0),
                  eff("Delay", Time=DOTTED_EIGHTH_MS, Feedback=0.3, Tone=0.55, Mix=0.2, Mode=1.0),
                  eff("Compressor", Threshold=-24.0, Ratio=4.0, Attack=1.0, Release=140.0, Makeup=1.5, Sidechain=float(kick_track)))
    s.mixer_of(stab)["sends"] = [0.2, 0.0]
    s.set_effects(s.mixer_of(lead)["effects"],
                  eff("Filter", Type=1, Cutoff=300.0),
                  eff("Parametric EQ", Mid_freq=3000.0, Mid_gain=2.5, Mid_Q=0.8, Low_freq=250.0, Low_gain=-2.0),
                  eff("Chorus", Rate=0.4, Depth=2.0, Mix=0.2))
    s.mixer_of(lead)["sends"] = [0.25, 0.3]
    s.set_effects(s.mixer_of(arp)["effects"],
                  eff("Filter", Type=1, Cutoff=400.0),
                  eff("Compressor", Threshold=-22.0, Ratio=4.0, Attack=1.0, Release=120.0, Sidechain=float(kick_track)),
                  eff("Stereo Imager", Width=150.0))
    s.mixer_of(arp)["sends"] = [0.15, 0.35]
    s.set_effects(s.mixer_of(riser)["effects"], eff("Filter", Type=1, Cutoff=300.0))
    s.mixer_of(riser)["sends"] = [0.35, 0.0]
    s.mixer_of(impact)["sends"] = [0.3, 0.0]

    for ch in drums:
        s.mixer_of(ch)["vca"] = 0b10  # VCA 2: the drums
    for ch in synths + [bass]:
        s.mixer_of(ch)["vca"] = 0b01  # VCA 1: the synths

    # Aux A: a hall, its lows cut; aux B: a dotted-eighth ping-pong delay.
    s.aux[0]["name"] = "Reverb"
    s.set_effects(s.aux[0]["effects"],
                  eff("Reverb", Size=0.85, Damping=0.5, Width=1.0, Mix=1.0, Type=0.0, Pre_delay=25.0),
                  eff("Parametric EQ", Low_freq=300.0, Low_gain=-12.0, High_freq=8000.0, High_gain=-3.0))
    s.aux[0]["volume"] = 0.75
    s.aux[1]["name"] = "Delay"
    s.set_effects(s.aux[1]["effects"],
                  eff("Delay", Time=DOTTED_EIGHTH_MS, Feedback=0.45, Tone=0.5, Mix=1.0, Mode=1.0),
                  eff("Filter", Type=1, Cutoff=350.0))
    s.aux[1]["volume"] = 0.6

    # Master: a high-pass (swept in the build), a gentle EQ, glue, the maximizer.
    s.set_effects(s.master,
                  eff("Filter", Type=1, Cutoff=20.0, Resonance=0.1),
                  eff("Parametric EQ", Low_freq=40.0, Low_gain=-1.0, Mid_freq=400.0, Mid_gain=-1.0, Mid_Q=0.7, High_freq=12000.0, High_gain=1.5),
                  eff("Compressor", Threshold=-12.0, Ratio=2.0, Attack=30.0, Release=150.0, Makeup=1.0),
                  eff("Maximizer", Input=3.5, Ceiling=-2.5, Release=80.0, Glue=0.3))

    # --- automation (four lanes) -----------------------------------------------------------------------------------------
    cutoff = cat.plugin_param("SunBurst", "Cutoff")
    # Lane 1: the pad's filter opens through the intro and again in the breakdown.
    s.automate("channelPlugin", 0, 0, 16, [(0, 0.22, "smooth"), (16 * BAR, 0.55, "smooth")], index=pad, param=cutoff)
    s.automate("channelPlugin", 0, 48, 16, [(0, 0.3, "smooth"), (12 * BAR, 0.7, "smooth"), (16 * BAR, 0.55, "smooth")], index=pad, param=cutoff)
    # Lane 2: the arp's filter in the breakdown, then the high-pass on the whole mix in the last four bars before the drop.
    s.automate("channelPlugin", 1, 52, 8, [(0, 0.15, "smooth"), (8 * BAR, 0.6, "smooth")], index=arp, param=cutoff)
    hp = cat.param_index("Filter", "Cutoff")
    s.automate("masterEffect", 1, 60, 4, [(0, 20.0, "smooth"), (3.75 * BAR, 700.0, "linear"), (4 * BAR - 1, 20.0, "hold")], slot=0, param=hp)
    # Lane 3: the melody's reverb send grows through the breakdown; the drums fade at the very end (VCA 2).
    s.automate("trackSend", 2, 44, 20, [(0, 0.25, "smooth"), (8 * BAR, 0.6, "smooth"), (16 * BAR, 0.6, "smooth"),
                                         (20 * BAR - 1, 0.25, "hold")], index=s.channels[lead]["mixerTrack"], slot=0)
    s.automate("vcaLevel", 2, 92, 4, [(0, 1.0, "smooth"), (4 * BAR, 0.0, "smooth")], slot=1)
    # Lane 4: the riser swells; the synths fade out (VCA 1).
    s.automate("channelVolume", 3, 56, 8, [(0, 0.05, "smooth"), (8 * BAR, 0.28, "smooth")], index=riser)
    # ... and before the riser, the pad comes up to carry the breakdown.
    s.automate("channelVolume", 3, 47, 9, [(0, 0.125, "smooth"), (BAR, 0.3, "linear"), (8 * BAR, 0.3, "smooth"),
                                            (9 * BAR - 1, 0.125, "hold")], index=pad)
    s.automate("vcaLevel", 3, 86, 8, [(0, 1.0, "smooth"), (8 * BAR, 0.0, "smooth")], slot=0)
    s.lane_names = {1: "Effects"}

    for bar, name in ((0, "Intro"), (16, "Groove"), (32, "Theme"), (48, "Breakdown"), (56, "Build"), (64, "Drop"), (88, "Outro")):
        s.marker(bar, name)
    s.current_pattern = P["Melody drop 1"]
    return s


if __name__ == "__main__":
    build(Catalog(sys.argv[1])).write(sys.argv[2])
