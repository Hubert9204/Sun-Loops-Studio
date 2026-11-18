#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "audio/NoteSequence.h"

namespace sls
{
// Tempo changes in the song (0.46.0). The song starts at the toolbar's BPM; each marker sets a new tempo from its tick on: at
// once (a step, as FL Studio's tempo markers), or (0.83.0) gliding to it from the tempo before, in a straight line over the ticks
// between the marker before (or the start of the song) and this one - a ramp, as a tempo automation clip's slope in FL Studio.
// Only Song mode uses them; patterns play at the toolbar's BPM. Plain C++ (no JUCE), tested.
constexpr double kMinBpm = 20.0, kMaxBpm = 400.0;
constexpr int kMaxTempoMarkers = 256;

struct TempoMarker
{
    int tick = 0;
    double bpm = 120.0;
    bool ramp = false; // 0.83.0: the tempo glides to `bpm` from the marker before (else it changes at `tick`)
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

namespace tempo_detail
{
// One stretch of the song between two markers: from `length` ticks, the tempo goes from b0 to b1 in a straight line (b0 == b1:
// one tempo). Seconds after `dt` ticks of it, and the ticks after `seconds`.
inline double spanSeconds (double dt, double b0, double b1, double length) noexcept
{
    if (dt <= 0.0)
        return 0.0;
    if (std::fabs (b1 - b0) < 1.0e-9 || length <= 0.0)
        return dt * 60.0 / (b0 * kTicksPerBeat);
    const double slope = (b1 - b0) / length;               // BPM per tick
    const double bpmThere = b0 + slope * std::min (dt, length);
    double seconds = 60.0 / kTicksPerBeat / slope * std::log (bpmThere / b0);
    if (dt > length) // (past the end of the ramp: at its last tempo)
        seconds += (dt - length) * 60.0 / (b1 * kTicksPerBeat);
    return seconds;
}

inline double spanTicks (double seconds, double b0, double b1, double length) noexcept
{
    if (seconds <= 0.0)
        return 0.0;
    if (std::fabs (b1 - b0) < 1.0e-9 || length <= 0.0)
        return seconds * b0 * kTicksPerBeat / 60.0;
    const double slope = (b1 - b0) / length;
    const double bpmThere = b0 * std::exp (seconds * kTicksPerBeat * slope / 60.0);
    return (bpmThere - b0) / slope;
}
} // namespace tempo_detail

// The tempo at `tick`: the last marker at or before it, else the song's starting tempo; inside a ramp, the tempo on its line.
// `markers` must be tidy.
inline double tempoAt (const std::vector<TempoMarker>& markers, double baseBpm, double tick) noexcept
{
    double bpm = std::clamp (baseBpm, kMinBpm, kMaxBpm), at = 0.0;
    for (const auto& m : markers)
    {
        if (m.tick > tick)
        {
            if (m.ramp && m.tick > at)
                return bpm + (m.bpm - bpm) * std::clamp ((tick - at) / (m.tick - at), 0.0, 1.0);
            break;
        }
        bpm = m.bpm;
        at = m.tick;
    }
    return bpm;
}

// True when the tempo changes nowhere (no markers, or every one keeps the tempo it finds).
inline bool tempoIsConstant (const std::vector<TempoMarker>& markers, double baseBpm) noexcept
{
    const double bpm = std::clamp (baseBpm, kMinBpm, kMaxBpm);
    return std::all_of (markers.begin(), markers.end(), [bpm] (const TempoMarker& m) { return std::fabs (m.bpm - bpm) < 1.0e-9; });
}

// Seconds from the start of the song to `tick`, through every tempo on the way.
inline double tickToSeconds (const std::vector<TempoMarker>& markers, double baseBpm, double tick) noexcept
{
    double seconds = 0.0, at = 0.0, bpm = std::clamp (baseBpm, kMinBpm, kMaxBpm);
    for (const auto& m : markers)
    {
        const double length = m.tick - at, end = m.ramp ? m.bpm : bpm;
        if (m.tick >= tick)
            return seconds + tempo_detail::spanSeconds (std::max (0.0, tick - at), bpm, end, length);
        seconds += tempo_detail::spanSeconds (length, bpm, end, length);
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
        const double length = m.tick - at, end = m.ramp ? m.bpm : bpm;
        const double span = tempo_detail::spanSeconds (length, bpm, end, length);
        if (elapsed + span > seconds)
            return at + std::min (length, tempo_detail::spanTicks (std::max (0.0, seconds - elapsed), bpm, end, length));
        elapsed += span;
        at = m.tick;
        bpm = m.bpm;
    }
    return at + std::max (0.0, seconds - elapsed) * bpm * kTicksPerBeat / 60.0;
}
} // namespace sls
