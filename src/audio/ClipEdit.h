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

// Gives audio clip `clip` new time-stretch / pitch-shift settings: renders the file's audio again (`original`, or `data` if
// the clip was never stretched) and swaps it in. The clip keeps covering the same stretch of the music, so its
// length in ticks and the offset into its audio change by the same factor as the audio does; the length is then kept
// within what the audio can fill at `bpm`. Returns false, leaving the clip as it was, if there is nothing to stretch or the
// audio is too long. The caller must check that the longer clip still fits on its track (Arrangement::canPlaceAudio).
inline bool restretchClip (AudioClip& clip, const StretchSettings& wanted, double bpm)
{
    const auto base = clip.original != nullptr ? clip.original : clip.data;
    if (base == nullptr || base->numFrames() < 2)
        return false;

    const auto settings = wanted.sanitised();
    const auto processed = stretchSample (base, settings);
    if (processed == nullptr)
        return false;

    const double change = settings.lengthRatio / clip.stretch.sanitised().lengthRatio;
    clip.offsetSeconds *= change;
    clip.lengthTicks = std::max (1, static_cast<int> (std::lround (clip.lengthTicks * change)));
    clip.data = processed;
    clip.original = settings.isNeutral() ? nullptr : base;
    clip.stretch = settings;
    clip.lengthTicks = std::min (clip.lengthTicks, std::max (1, clip.naturalLengthTicks (bpm)));
    return true;
}

// The stretch that makes audio recorded at `sourceBpm` play in time at the project tempo `projectBpm` (the length ratio is
// source / project: a 90 BPM loop in a 120 BPM project becomes 0.75 times as long). Returns 1 for tempos that make no sense.
inline double lengthRatioForTempo (double sourceBpm, double projectBpm) noexcept
{
    if (! (sourceBpm > 0.0) || ! (projectBpm > 0.0) || ! std::isfinite (sourceBpm) || ! std::isfinite (projectBpm))
        return 1.0;
    return std::clamp (sourceBpm / projectBpm, kMinStretchRatio, kMaxStretchRatio);
}

// The sample editor saved an edited copy of an audio clip's audio. Clip `index` (found again by its audio, `expected`, if the list changed)
// now plays `edited` from its start, for as long as it lasts or as far as fits on its track, and is cut at `sliceSeconds` (positions
// inside `edited`, snapped to ticks at `bpm`). Returns an error text and changes nothing, or an empty string.
inline std::string useEditedAudio (Arrangement& a, int index, const std::shared_ptr<const SampleData>& expected,
                                   std::shared_ptr<const SampleData> edited, const std::vector<double>& sliceSeconds, double bpm)
{
    if (edited == nullptr || edited->numFrames() < 2)
        return "There is no audio to use.";
    if (! (bpm > 0.0))
        return "The tempo is not valid.";
    if (index < 0 || index >= static_cast<int> (a.audioClips.size()) || a.audioClips[static_cast<size_t> (index)].data != expected)
    {
        index = -1; // the clip moved in the list: look for it by its audio
        for (size_t i = 0; i < a.audioClips.size(); ++i)
            if (a.audioClips[i].data == expected)
                index = static_cast<int> (i);
        if (index < 0)
            return "The clip was changed or removed while the editor was open, so the edit could not be put back. The edited file has been saved in the Samples folder.";
    }

    auto clip = a.audioClips[static_cast<size_t> (index)];
    clip.data = std::move (edited);
    clip.original = nullptr;
    clip.stretch = {};
    clip.offsetSeconds = 0.0;
    const int natural = std::max (1, clip.naturalLengthTicks (bpm));
    int lo = 0, hi = natural; // the longest length that fits (a longer clip never fits where a shorter one does not)
    while (lo < hi)
    {
        const int mid = (lo + hi + 1) / 2;
        auto probe = clip;
        probe.lengthTicks = mid;
        if (a.canPlaceAudio (probe, index))
            lo = mid;
        else
            hi = mid - 1;
    }
    if (lo < 1)
        return "There is no room for the edited clip at its position. The edited file has been saved in the Samples folder.";
    clip.lengthTicks = lo;
    a.audioClips[static_cast<size_t> (index)] = clip;

    int last = clip.startTick;
    for (const double seconds : sliceSeconds)
    {
        if (! std::isfinite (seconds))
            continue;
        const int tick = clip.startTick + static_cast<int> (std::lround (secondsToTicks (seconds, bpm)));
        if (tick <= last || tick >= clip.endTick())
            continue;
        const int piece = a.audioClipAt (clip.track, tick);
        if (piece >= 0 && splitAudioClip (a, piece, tick, bpm) >= 0)
            last = tick;
    }
    return {};
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
