#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "audio/Arrangement.h"

namespace sls
{
// Editing the playlist's clips: the arithmetic behind the Split, Cut and Trim tools, kept free of the GUI so it can
// be tested. Audio clips are positioned in ticks but their audio lasts a fixed time, so cutting one at a tick moves the
// start of the right-hand piece into the audio by the time those ticks last at the tempo `bpm` of the moment.
// (Cut audio at the tempo you will keep: changing the tempo afterwards moves the pieces but not the audio in them.)

inline double ticksToSeconds (double ticks, double bpm) noexcept
{
    return bpm > 0.0 ? ticks * 60.0 / (bpm * kTicksPerBeat) : 0.0;
}

inline double secondsToTicks (double seconds, double bpm) noexcept
{
    return seconds * bpm / 60.0 * kTicksPerBeat;
}

// Cuts audio clip `index` in two at `tick`, which must lie strictly inside it. Returns the index of the new right-hand
// piece, or -1 (nothing changed) if the cut is not inside the clip.
inline int splitAudioClip (Arrangement& a, int index, int tick, double bpm)
{
    if (index < 0 || index >= static_cast<int> (a.audioClips.size()) || bpm <= 0.0)
        return -1;

    auto& left = a.audioClips[static_cast<size_t> (index)];
    if (tick <= left.startTick || tick >= left.endTick())
        return -1;

    AudioClip right = left;
    right.startTick = tick;
    right.lengthTicks = left.endTick() - tick;
    right.offsetSeconds = left.offsetSeconds + ticksToSeconds (tick - left.startTick, bpm);
    left.lengthTicks = tick - left.startTick;

    a.audioClips.push_back (std::move (right));
    return static_cast<int> (a.audioClips.size()) - 1;
}

// Removes the part [from, to) of audio clip `index`. What is left of the clip is kept: nothing (the clip is gone), one
// piece (the cut reached an edge) or two (the cut was in the middle; the right-hand piece is appended). Returns how
// many pieces remain, or -1 if the range does not touch the clip.
inline int cutAudioRange (Arrangement& a, int index, int from, int to, double bpm)
{
    if (index < 0 || index >= static_cast<int> (a.audioClips.size()) || bpm <= 0.0)
        return -1;

    auto clip = a.audioClips[static_cast<size_t> (index)];
    from = std::max (from, clip.startTick);
    to = std::min (to, clip.endTick());
    if (to <= from)
        return -1;

    if (from <= clip.startTick && to >= clip.endTick())
    {
        a.audioClips.erase (a.audioClips.begin() + index);
        return 0;
    }

    auto& stored = a.audioClips[static_cast<size_t> (index)];
    if (from <= clip.startTick) // the beginning is cut away
    {
        stored.offsetSeconds = clip.offsetSeconds + ticksToSeconds (to - clip.startTick, bpm);
        stored.startTick = to;
        stored.lengthTicks = clip.endTick() - to;
        return 1;
    }
    if (to >= clip.endTick()) // the end is cut away
    {
        stored.lengthTicks = from - clip.startTick;
        return 1;
    }

    AudioClip right = clip;
    right.startTick = to;
    right.lengthTicks = clip.endTick() - to;
    right.offsetSeconds = clip.offsetSeconds + ticksToSeconds (to - clip.startTick, bpm);
    stored.lengthTicks = from - clip.startTick;
    a.audioClips.push_back (std::move (right));
    return 2;
}

// `clip` with its start edge moved to `newStart` and the audio left where it was (so the sound does not move, only
// more or less of it is heard). Moving the edge left beyond the beginning of the audio, past the end of the clip, or
// below tick 0 stops at the limit. Dragging the edge never changes where the end of the clip is.
inline AudioClip trimmedAudioStart (const AudioClip& clip, int newStart, double bpm)
{
    AudioClip out = clip;
    if (bpm <= 0.0)
        return out;

    const int earliestForAudio = clip.startTick - static_cast<int> (std::floor (secondsToTicks (clip.offsetSeconds, bpm)));
    newStart = std::clamp (newStart, std::max (0, earliestForAudio), clip.endTick() - 1);

    out.startTick = newStart;
    out.lengthTicks = clip.endTick() - newStart;
    out.offsetSeconds = std::max (0.0, clip.offsetSeconds + ticksToSeconds (newStart - clip.startTick, bpm));
    return out;
}

// Pattern clips are whole patterns long: they are cut at pattern boundaries ("units").
// Splits pattern clip `index` at the boundary `unit` (strictly inside it). Returns the new right-hand clip's index or -1.
inline int splitPatternClip (Arrangement& a, int index, int unit)
{
    if (index < 0 || index >= static_cast<int> (a.clips.size()))
        return -1;

    auto& left = a.clips[static_cast<size_t> (index)];
    if (unit <= left.startUnit || unit >= left.endUnit())
        return -1;

    Clip right = left;
    right.startUnit = unit;
    right.lengthUnits = left.endUnit() - unit;
    left.lengthUnits = unit - left.startUnit;
    a.clips.push_back (right);
    return static_cast<int> (a.clips.size()) - 1;
}

// Removes the units [fromUnit, toUnit) of pattern clip `index`; same results as cutAudioRange.
inline int cutPatternRange (Arrangement& a, int index, int fromUnit, int toUnit)
{
    if (index < 0 || index >= static_cast<int> (a.clips.size()))
        return -1;

    const auto clip = a.clips[static_cast<size_t> (index)];
    fromUnit = std::max (fromUnit, clip.startUnit);
    toUnit = std::min (toUnit, clip.endUnit());
    if (toUnit <= fromUnit)
        return -1;

    if (fromUnit <= clip.startUnit && toUnit >= clip.endUnit())
    {
        a.clips.erase (a.clips.begin() + index);
        return 0;
    }

    auto& stored = a.clips[static_cast<size_t> (index)];
    if (fromUnit <= clip.startUnit)
    {
        stored.startUnit = toUnit;
        stored.lengthUnits = clip.endUnit() - toUnit;
        return 1;
    }
    if (toUnit >= clip.endUnit())
    {
        stored.lengthUnits = fromUnit - clip.startUnit;
        return 1;
    }

    Clip right = clip;
    right.startUnit = toUnit;
    right.lengthUnits = clip.endUnit() - toUnit;
    stored.lengthUnits = fromUnit - clip.startUnit;
    a.clips.push_back (right);
    return 2;
}

// A pattern name as typed by the user: trimmed, never empty, at most 40 characters. Returns false for an empty name.
inline bool cleanPatternName (std::string& name)
{
    const auto first = name.find_first_not_of (" \t\r\n");
    if (first == std::string::npos)
        return false;
    const auto last = name.find_last_not_of (" \t\r\n");
    name = name.substr (first, last - first + 1);
    if (name.size() > 40)
        name.resize (40);
    return true;
}
} // namespace sls
