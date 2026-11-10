#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "audio/NoteSequence.h"

namespace sls
{
// Tempo changes in the song (0.46.0). The song starts at the toolbar's BPM; each marker sets a new tempo from its tick on (a
// step, as in FL Studio's tempo markers - no gradual ramps). Only Song mode uses them; patterns play at the toolbar's BPM.
// Plain C++ (no JUCE), tested.
constexpr double kMinBpm = 20.0, kMaxBpm = 400.0;
constexpr int kMaxTempoMarkers = 256;

struct TempoMarker
{
    int tick = 0;
    double bpm = 120.0;
    bool operator== (const TempoMarker&) const = default;
};

// Sorted by tick, ticks above 0, one marker per tick (the last one given wins), tempos clamped, at most kMaxTempoMarkers.
inline std::vector<TempoMarker> tidyTempoMarkers (std::vector<TempoMarker> markers)
{
    markers.erase (std::remove_if (markers.begin(), markers.end(), [] (const TempoMarker& m) { return m.tick <= 0 || ! std::isfinite (m.bpm); }),
                   markers.end());
    std::stable_sort (markers.begin(), markers.end(), [] (const TempoMarker& a, const TempoMarker& b) { return a.tick < b.tick; });
    std::vector<TempoMarker> out;
    for (auto m : markers)
    {
        m.bpm = std::clamp (m.bpm, kMinBpm, kMaxBpm);
        if (! out.empty() && out.back().tick == m.tick)
            out.back() = m;
        else
            out.push_back (m);
    }
    if (out.size() > static_cast<size_t> (kMaxTempoMarkers))
        out.resize (static_cast<size_t> (kMaxTempoMarkers));
    return out;
}

// The tempo at `tick`: the last marker at or before it, else the song's starting tempo. `markers` must be tidy.
inline double tempoAt (const std::vector<TempoMarker>& markers, double baseBpm, double tick) noexcept
{
    double bpm = std::clamp (baseBpm, kMinBpm, kMaxBpm);
    for (const auto& m : markers)
    {
        if (m.tick > tick)
            break;
        bpm = m.bpm;
    }
    return bpm;
}

// Seconds from the start of the song to `tick`, through every tempo on the way.
inline double tickToSeconds (const std::vector<TempoMarker>& markers, double baseBpm, double tick) noexcept
{
    double seconds = 0.0, at = 0.0, bpm = std::clamp (baseBpm, kMinBpm, kMaxBpm);
    for (const auto& m : markers)
    {
        if (m.tick >= tick)
            break;
        seconds += (m.tick - at) * 60.0 / (bpm * kTicksPerBeat);
        at = m.tick;
        bpm = m.bpm;
    }
    return seconds + std::max (0.0, tick - at) * 60.0 / (bpm * kTicksPerBeat);
}

// The tick reached after `seconds` from the start of the song (the inverse of tickToSeconds).
inline double secondsToTick (const std::vector<TempoMarker>& markers, double baseBpm, double seconds) noexcept
{
    double at = 0.0, elapsed = 0.0, bpm = std::clamp (baseBpm, kMinBpm, kMaxBpm);
    for (const auto& m : markers)
    {
        const double span = (m.tick - at) * 60.0 / (bpm * kTicksPerBeat);
        if (elapsed + span > seconds)
            break;
        elapsed += span;
        at = m.tick;
        bpm = m.bpm;
    }
    return at + std::max (0.0, seconds - elapsed) * bpm * kTicksPerBeat / 60.0;
}
} // namespace sls
