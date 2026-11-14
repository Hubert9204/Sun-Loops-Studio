"""Writes Sun Loops Studio project files (.slp) from Python: the demo projects in resources/demos are made with it.

A Song collects channels, patterns, playlist clips, mixer tracks, aux buses, master effects, automation and audio clips and
writes them in the layout src/library/ProjectFile.cpp reads. The plugin presets (SunBurst, HexForge) and the effects' parameter
tables come from the catalog that `DemoTool catalog` writes (see tools/demos/README.md), so the files hold exactly what the
program would save.
"""
import json
import random

TPB = 480            # ticks per quarter note (src/audio/NoteSequence.h)
BAR = 4 * TPB
Q, E, S = TPB, TPB // 2, TPB // 4

# EffectType -> the key a project file uses (ProjectFile.cpp, effectKey) and its name in the catalog (effectName).
EFFECT_KEYS = {
    "Equalizer": "equalizer", "Reverb": "reverb", "Delay": "delay", "Compressor": "compressor", "Overdrive": "overdrive",
    "Chorus": "chorus", "Phaser": "phaser", "Filter": "filter", "Parametric EQ": "parametric-eq", "Maximizer": "maximizer",
    "Transient": "transient", "Stereo Imager": "imager", "Time Shaper": "time-shaper", "Pitch Corrector": "pitch-corrector",
    "Flanger": "flanger", "Gate": "gate", "Bass Boost": "bass-boost", "Panomatic": "panomatic", "Squeeze": "squeeze",
    "Vibrato": "vibrato", "Parametric EQ 2": "parametric-eq-2", "Multiband Compressor": "multiband-compressor",
    "WaveShaper": "waveshaper", "Vocoder": "vocoder", "Peak Controller": "peak-controller", "Convolver": "convolver",
}
AUTOMATION_KINDS = ["channelVolume", "channelPan", "trackVolume", "trackPan", "trackEffect", "masterEffect", "channelPlugin",
                    "trackPlugin", "masterPlugin", "trackSend", "auxVolume", "auxPan", "vcaLevel"]
SHAPES = {"linear": 0, "hold": 1, "smooth": 2}
SLOTS = 10

NOTE_NAMES = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5, "F#": 6, "Gb": 6, "G": 7, "G#": 8,
              "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11}


def n(name):
    """'C4' -> 60, 'Eb3' -> 51."""
    i = 2 if len(name) > 2 and name[1] in "#b" else 1
    return NOTE_NAMES[name[:i]] + 12 * (int(name[i:]) + 1)


class Catalog:
    def __init__(self, path):
        with open(path) as f:
            self.data = json.load(f)

    def effect(self, name, **values):
        """A built-in effect slot, every parameter at its default unless given by name ('Threshold'=-20, 'Mid_freq'=...)."""
        table = self.data["effects"][name]
        params = [p["def"] for p in table]
        names = [p["name"].lower().replace(" ", "_").replace("-", "_") for p in table]
        for key, value in values.items():
            k = key.lower()
            if k not in names:
                raise KeyError(f"{name} has no parameter {key}: {names}")
            info = table[names.index(k)]
            if not info["min"] - 1e-6 <= value <= info["max"] + 1e-6:
                raise ValueError(f"{name}.{key} = {value} is outside {info['min']}..{info['max']}")
            params[names.index(k)] = float(value)
        return {"type": EFFECT_KEYS[name], "bypass": False, "params": params}

    def param_index(self, name, effect_param):
        names = [p["name"] for p in self.data["effects"][name]]
        return names.index(effect_param)

    def plugin(self, plugin, preset):
        info = self.data[plugin]
        if preset not in info["presets"]:
            raise KeyError(f"{plugin} has no preset {preset}")
        return {"description": info["description"], "state": info["presets"][preset]}

    def plugin_param(self, plugin, name):
        return self.data[plugin]["parameters"].index(name)

    def synth(self, patch):
        return self.data["synthPatches"][patch]

    def program_settings(self, program_id):
        for p in self.data["programs"]:
            if p["id"] == program_id:
                return p["sampler"]
        raise KeyError(program_id)


def empty_slot():
    return {"type": "none", "bypass": False, "params": []}


class Song:
    def __init__(self, catalog, bpm, seed=1):
        self.cat = catalog
        self.bpm = bpm
        self.channels = []
        self.patterns = []
        self.clips = []
        self.tracks = [self._track("Track 1")]  # mixer track 1: audio clips (the playlist's first track plays through it)
        self.aux = [self._aux("Aux A"), self._aux("Aux B")]
        self.vca = [{"level": 1.0, "mute": False}, {"level": 1.0, "mute": False}]
        self.master = [empty_slot() for _ in range(SLOTS)]
        self.automation = []
        self.audio_clips = []
        self.markers = []
        self.lane_names = {}
        self.rng = random.Random(seed)
        self.master_volume = 0.8
        self.current_pattern = 0

    # ------------------------------------------------------------------ mixer
    @staticmethod
    def _track(name):
        return {"name": name, "volume": 1.0, "pan": 0.0, "mute": False, "solo": False,
                "effects": [empty_slot() for _ in range(SLOTS)], "sends": [0.0, 0.0], "sendsPreFader": False, "vca": 0}

    @staticmethod
    def _aux(name):
        return {"name": name, "volume": 1.0, "pan": 0.0, "mute": False, "effects": [empty_slot() for _ in range(SLOTS)]}

    def track(self, index):
        return self.tracks[index]

    def set_effects(self, slots, *effects):
        for i, e in enumerate(effects):
            slots[i] = e

    # ------------------------------------------------------------------ channels
    def _channel(self, name, instrument, volume, pan, own_track, **extra):
        index = len(self.channels)
        if own_track:
            self.tracks.append(self._track(name))
            mixer = len(self.tracks) - 1
        else:
            mixer = own_track if isinstance(own_track, int) else 0
        c = {"name": name, "instrument": instrument, "sample": "", "rootPitch": 60, "volume": volume, "pan": pan,
             "mute": False, "solo": False, "pitch": 0.0, "swing": 0.0, "mixerTrack": mixer}
        c.update(extra)
        self.channels.append(c)
        for p in self.patterns:
            p["notes"].append([])
        return index

    def sampler(self, name, sample, volume=0.8, pan=0.0, **sampler):
        settings = {"envelope": False, "attack": 0.005, "decay": 0.3, "sustain": 1.0, "release": 0.15, "loop": "off",
                    "loopStart": 0.0, "loopEnd": 1.0, "filter": "off", "cutoff": 1.0, "resonance": 0.0}
        settings.update(sampler)
        return self._channel(name, "sampler", volume, pan, True, sample="builtin:" + sample, sampler=settings)

    def program(self, name, program_id, volume=0.8, pan=0.0):
        """A sampler channel playing an instrument of the sound library (the grand piano, the electric bass)."""
        return self._channel(name, "sampler", volume, pan, True, sampler=self.cat.program_settings(program_id),
                             program={"kind": "builtin", "file": "builtin:program:" + program_id})

    def synth(self, name, patch, volume=0.8, pan=0.0, **changes):
        """A built-in synth channel with a factory patch; `changes` replace top-level settings of the patch (noise=1.0, ...)."""
        settings = json.loads(json.dumps(self.cat.synth(patch)))
        for key, value in changes.items():
            if key not in settings:
                raise KeyError(f"synth patches have no setting {key}")
            settings[key] = value
        return self._channel(name, "synth", volume, pan, True, synth=settings)

    def plugin(self, name, plugin, preset, volume=0.8, pan=0.0):
        return self._channel(name, "plugin", volume, pan, True, plugin=self.cat.plugin(plugin, preset))

    def mixer_of(self, channel):
        return self.tracks[self.channels[channel]["mixerTrack"]]

    def track_number(self, channel):
        """The 1-based number of a channel's mixer track (what a compressor's sidechain parameter takes)."""
        return self.channels[channel]["mixerTrack"] + 1

    # ------------------------------------------------------------------ patterns and notes
    def pattern(self, name, bars=4):
        self.patterns.append({"name": name, "notes": [[] for _ in self.channels], "beats": bars * 4})
        return len(self.patterns) - 1

    def note(self, pattern, channel, pitch, start, length, velocity=0.8, humanize=0.0):
        p = self.patterns[pattern]
        ticks = p["beats"] * TPB
        if not 0 <= start < ticks:
            raise ValueError(f"note at {start} outside pattern {p['name']} ({ticks} ticks)")
        length = max(1, min(int(length), ticks - start))
        if humanize:
            velocity *= 1.0 + self.rng.uniform(-humanize, humanize)
        velocity = min(1.0, max(0.05, velocity))
        p["notes"][channel].append([int(pitch), int(start), length, round(velocity, 4)])

    def chord(self, pattern, channel, pitches, start, length, velocity=0.8, humanize=0.0, strum=0):
        """`strum`: ticks between one note and the next, lowest first (a rolled chord)."""
        for i, p in enumerate(sorted(pitches)):
            self.note(pattern, channel, p, start + i * strum, length - i * strum, velocity, humanize)

    def route(self, channel_or_track, bus_track, level=1.0):
        """Free routing (0.62.0): the track goes to `bus_track` instead of the master."""
        t = self.tracks[channel_or_track]
        t["toMaster"] = False
        t["trackSends"] = [{"track": bus_track, "level": level}]

    def bus(self, name):
        """A mixer track that only collects other tracks (a drum bus)."""
        self.tracks.append(self._track(name))
        return len(self.tracks) - 1

    # ------------------------------------------------------------------ playlist
    def place(self, pattern, start_bar, bars, lane=None):
        """A clip of the pattern from start_bar for `bars` bars (it repeats over its own length) on the pattern's own track."""
        self.clips.append({"pattern": pattern, "track": lane, "startTick": start_bar * BAR, "lengthTicks": bars * BAR})

    def audio(self, name, sample, start_tick, length_ticks, gain=1.0, pan=0.0, reverse=False, fade_in=0.0, fade_out=0.0):
        self.audio_clips.append({"source": "builtin:" + sample, "name": name, "track": 0, "startTick": int(start_tick),
                                 "lengthTicks": int(length_ticks), "gain": gain, "offsetSeconds": 0.0, "pan": pan,
                                 "reverse": reverse, "fadeIn": fade_in, "fadeOut": fade_out})

    def automate(self, kind, lane, start_bar, bars, points, index=0, slot=0, param=0):
        """points: (tick inside the clip, value, shape) with shape 'linear', 'hold' or 'smooth'."""
        pts = [[float(t), float(v), SHAPES[sh]] for t, v, sh in points]
        self.automation.append({"kind": kind, "index": index, "slot": slot, "param": param, "lane": lane,
                                "startTick": start_bar * BAR, "lengthTicks": bars * BAR, "points": pts})

    def marker(self, bar, name):
        self.markers.append([bar * BAR, name])

    # ------------------------------------------------------------------ the file
    def to_json(self):
        for c in self.automation:
            for o in self.automation:
                if o is not c and o["lane"] == c["lane"] and c["startTick"] < o["startTick"] + o["lengthTicks"] \
                        and o["startTick"] < c["startTick"] + c["lengthTicks"]:
                    raise ValueError(f"automation clips overlap on lane {c['lane']}")
        # The playlist: 4 free tracks (audio and spare), then one per pattern (the layout the program makes, 0.44.0).
        playlist = [{"track": 0, "name": "Audio"}]
        for t, name in self.lane_names.items():
            playlist.append({"track": t, "name": name})
        clips = []
        for c in self.clips:
            lane = 4 + c["pattern"] if c["track"] is None else c["track"]
            clips.append({"pattern": c["pattern"], "track": lane, "startTick": c["startTick"], "lengthTicks": c["lengthTicks"]})
        for i, p in enumerate(self.patterns):
            playlist.append({"track": 4 + i, "name": p["name"], "pattern": i})
        longest = max(p["beats"] for p in self.patterns)
        doc = {
            "format": "sun-loops-studio-project", "version": 1, "bpm": float(self.bpm), "masterVolume": self.master_volume,
            "songMode": True, "currentPattern": self.current_pattern,
            "channels": self.channels, "tracks": self.tracks,
            "console": {"aux": self.aux, "vca": self.vca},
            "master": {"effects": self.master},
            "patterns": [{"name": p["name"], "notes": [sorted(ns, key=lambda x: (x[1], x[0])) for ns in p["notes"]],
                          "beats": p["beats"]} for p in self.patterns],
            "patternBeats": longest,
            "clips": sorted(clips, key=lambda c: (c["startTick"], c["track"])),
            "trackCount": 4 + len(self.patterns),
            "playlistTracks": playlist,
            "audioClips": self.audio_clips,
            "automation": self.automation,
            "markers": sorted(self.markers),
        }
        if longest % 4 == 0:
            doc["patternBars"] = longest // 4
        return json.dumps(doc, indent=1)

    def write(self, path):
        with open(path, "w") as f:
            f.write(self.to_json())
