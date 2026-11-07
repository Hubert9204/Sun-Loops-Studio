"""Helpers shared by the scripts that build the built-in sounds (see README.md in this folder)."""
import json, os, subprocess, tempfile
import numpy as np
import soundfile as sf

def read(path):
    d, sr = sf.read(path, dtype="float64", always_2d=True)
    return d, sr

def trim(x, sr, ref, start_db=-60.0, end_db=-66.0, max_seconds=None, fade_seconds=0.3, pre_seconds=0.002):
    """Cuts the silence before the sound (from `pre_seconds` before the first sample above `start_db` of `ref`) and the tail
    below `end_db` of `ref` (at most `max_seconds` long), with a fade (cos^2) over the last `fade_seconds`."""
    env = np.abs(x).max(axis=1)
    above = np.nonzero(env > ref * 10 ** (start_db / 20))[0]
    start = max(0, int(above[0]) - int(pre_seconds * sr)) if len(above) else 0
    tail = np.nonzero(env > ref * 10 ** (end_db / 20))[0]
    end = int(tail[-1]) + 1 if len(tail) else len(x)
    if max_seconds is not None:
        end = min(end, start + int(max_seconds * sr))
    y = x[start:end].copy()
    fade = min(len(y), int(fade_seconds * sr))
    if fade > 0:
        y[-fade:] *= (np.cos(np.linspace(0, np.pi / 2, fade)) ** 2)[:, None]
    return y

def encode(x, sr, path, quality=3):
    """Ogg Vorbis at the given quality; mono if `x` has one column."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    peak = float(np.abs(x).max())
    assert peak <= 1.0, (path, peak)
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, "x.wav")
        sf.write(wav, x.astype(np.float32), sr, subtype="FLOAT")
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", wav, "-c:a", "libvorbis", "-q:a", str(quality), "-map_metadata", "-1", path],
                       check=True)
    return os.path.getsize(path)

def write_json(path, value):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(value, f, indent=1)
        f.write("\n")

def layer_gain(ratio, keep=1.0 / 3.0):
    """The gain for a layer whose peak is `ratio` times the peak of the loudest layer of the same note: it keeps `keep` of the
    level difference (in dB) between the layers; the rest is played by the velocity."""
    return float(ratio) ** (keep - 1.0)
