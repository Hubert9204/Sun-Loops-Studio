#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "audio/Arrangement.h"
#include "audio/SamplerProgram.h"

namespace sls
{
// The Slicer (0.37.0), as in FL Studio's Slicex: a loop cut into slices, slice n on key C5 + n, played back by notes that
// follow the project's tempo. These helpers are plain C++ and tested; SlicerWindow is the interface.
namespace slicer
{
// The tempo at which `frames` of audio last a whole number of bars of 4/4 (1, 2, 4, 8 or 16), the one nearest to `near` within
// 60..200 BPM; 0 when none is (a sound shorter than a bar at 200 BPM or longer than 16 bars at 60).
inline double guessLoopBpm (int frames, double sampleRate, double near)
{
    if (frames < 2 || sampleRate <= 0.0)
        return 0.0;
    const double seconds = frames / sampleRate;
    double best = 0.0;
    for (const int bars : { 1, 2, 4, 8, 16 })
    {
        const double bpm = bars * 4.0 * 60.0 / seconds;
        if (bpm >= 60.0 && bpm <= 200.0 && (best <= 0.0 || std::abs (bpm - near) < std::abs (best - near)))
            best = bpm;
    }
    return best;
}

// Slice starts (frames, from 0) every `ticks` at the loop's tempo: a slice per 16th with ticks = kTicksPerBeat / 4.
inline std::vector<int> gridStarts (int frames, double sampleRate, double loopBpm, int ticks)
{
    std::vector<int> starts { 0 };
    if (frames < 2 || sampleRate <= 0.0 || loopBpm <= 0.0 || ticks <= 0)
        return starts;
    const double step = ticks / static_cast<double> (kTicksPerBeat) * 60.0 / loopBpm * sampleRate;
    if (step < 2.0)
        return starts;
    for (int i = 1;; ++i)
    {
        const auto f = static_cast<int> (std::lround (i * step));
        if (f >= frames - 1 || starts.size() >= 128)
            break;
        starts.push_back (f);
    }
    return starts;
}

// The notes that play the loop as it was: one per slice, at the slice's place and as long as the slice, in ticks at the loop's
// tempo (so when the project's tempo changes, the slices follow it, as in Slicex). Notes past `maxTicks` are left out; the
// last one that starts before is shortened to fit.
inline std::vector<Note> score (const SamplerProgram& program, double sampleRate, double loopBpm, int maxTicks)
{
    std::vector<Note> notes;
    if (sampleRate <= 0.0 || loopBpm <= 0.0 || maxTicks <= 0)
        return notes;
    const double ticksPerFrame = loopBpm / 60.0 * kTicksPerBeat / sampleRate;
    for (size_t i = 0; i < program.sliceStarts.size(); ++i)
    {
        const int key = program.sliceFirstKey + static_cast<int> (i);
        const auto zone = std::find_if (program.zones.begin(), program.zones.end(), [key] (const SampleZone& z) { return z.loKey == key; });
        if (zone == program.zones.end() || key > 127)
            continue;
        const int start = static_cast<int> (std::lround (program.sliceStarts[i] * ticksPerFrame));
        if (start >= maxTicks)
            break;
        const int length = std::max (1, static_cast<int> (std::lround (zone->sample->numFrames() * ticksPerFrame)));
        notes.push_back ({ key, start, std::min (length, maxTicks - start), 0.8f });
    }
    return notes;
}

// Moves a slice start to `frame`, kept strictly between its neighbours (two frames at least on each side). The first slice
// always starts at 0 and does not move. Returns the index of the moved start.
inline int moveStart (std::vector<int>& starts, int index, int frame, int frames)
{
    if (index <= 0 || index >= static_cast<int> (starts.size()))
        return index;
    const int lo = starts[static_cast<size_t> (index - 1)] + 2;
    const int hi = (index + 1 < static_cast<int> (starts.size()) ? starts[static_cast<size_t> (index + 1)] : frames) - 2;
    if (lo <= hi)
        starts[static_cast<size_t> (index)] = std::clamp (frame, lo, hi);
    return index;
}

// Adds a start at `frame` (not closer than two frames to another, at most 128 slices); returns its index, or -1.
inline int addStart (std::vector<int>& starts, int frame, int frames)
{
    if (frame <= 1 || frame >= frames - 2 || starts.size() >= 128)
        return -1;
    const auto it = std::lower_bound (starts.begin(), starts.end(), frame);
    if ((it != starts.end() && *it - frame < 2) || (it != starts.begin() && frame - *(it - 1) < 2))
        return -1;
    const auto index = it - starts.begin(); // before inserting: the vector may move
    starts.insert (it, frame);
    return static_cast<int> (index);
}

// The slice (index into `starts`) that holds `frame`.
inline int sliceAt (const std::vector<int>& starts, int frame)
{
    if (starts.empty())
        return -1;
    const auto it = std::upper_bound (starts.begin(), starts.end(), frame);
    return std::max (0, static_cast<int> (it - starts.begin()) - 1);
}
} // namespace slicer
} // namespace sls
