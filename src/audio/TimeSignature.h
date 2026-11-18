#pragma once

#include <algorithm>
#include <climits>
#include <string>
#include <vector>

#include "audio/NoteSequence.h"

namespace sls
{
// The time signature (0.46.0): beats per bar and the note value of a beat. The project's (patterns use it; 0.83.0: the song can change it, see MeterMarker). Only signatures whose bar
// is a whole number of quarter notes are offered (pattern lengths are counted in quarters). Plain C++, tested.
struct TimeSignature
{
    int beats = 4;
    int unit = 4; // 4 = a quarter note is a beat, 8 = an eighth

    int beatTicks() const noexcept { return kTicksPerBeat * 4 / (unit == 8 ? 8 : 4); }
    int barTicks() const noexcept { return beats * beatTicks(); }
    int barQuarters() const noexcept { return barTicks() / kTicksPerBeat; }
    std::string text() const { return std::to_string (beats) + "/" + std::to_string (unit); }
    bool operator== (const TimeSignature&) const = default;

    static const std::vector<TimeSignature>& choices()
    {
        static const std::vector<TimeSignature> list { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 4 }, { 7, 4 }, { 6, 8 }, { 12, 8 } };
        return list;
    }
    // One of the choices, or 4/4.
    static TimeSignature valid (int beats, int unit)
    {
        for (const auto& c : choices())
            if (c.beats == beats && c.unit == unit)
                return c;
        return {};
    }
};

// The time signature the interface draws and counts bars with: set by the main window when a project opens or the signature
// changes. Message thread only (the audio thread gets it in the Project).
inline TimeSignature& interfaceMeter()
{
    static TimeSignature meter;
    return meter;
}

// Time signature changes in the song (0.83.0, as FL Studio's time signature markers): from `tick`, which is always at the start
// of a bar of the signature before, the bars have the marker's signature. The song starts in the project's signature. Bars,
// the grid, the snap to bars, the metronome's accent and the plugins' bar position follow them; patterns keep the project's.
constexpr int kMaxMeterMarkers = 128;

struct MeterMarker
{
    int tick = 0;
    TimeSignature meter;
    bool operator== (const MeterMarker&) const = default;
};

// Sorted, each on the bar line nearest to where it was asked (at least one bar after the one before), one of the offered
// signatures, none that repeats the signature it finds, at most kMaxMeterMarkers.
inline std::vector<MeterMarker> tidyMeterMarkers (std::vector<MeterMarker> markers, const TimeSignature& base)
{
    markers.erase (std::remove_if (markers.begin(), markers.end(), [] (const MeterMarker& m) { return m.tick <= 0; }), markers.end());
    std::stable_sort (markers.begin(), markers.end(), [] (const MeterMarker& a, const MeterMarker& b) { return a.tick < b.tick; });
    std::vector<MeterMarker> out;
    long long at = 0;
    TimeSignature current = base;
    for (auto m : markers)
    {
        m.meter = TimeSignature::valid (m.meter.beats, m.meter.unit);
        const long long bar = current.barTicks();
        const long long bars = std::max (1LL, (static_cast<long long> (m.tick) - at + bar / 2) / bar);
        const long long tick = at + bars * bar;
        if (tick > INT_MAX / 2)
            break;
        if (! out.empty() && out.back().tick == static_cast<int> (tick)) // (two asked for the same bar line: the last one wins)
        {
            out.back().meter = m.meter;
            continue;
        }
        if (m.meter == current)
            continue;
        out.push_back ({ static_cast<int> (tick), m.meter });
        at = tick;
        current = m.meter;
        if (out.size() >= static_cast<size_t> (kMaxMeterMarkers))
            break;
    }
    // a marker set to the signature before it repeats it: gone
    std::vector<MeterMarker> kept;
    TimeSignature previous = base;
    for (const auto& m : out)
        if (! (m.meter == previous))
        {
            kept.push_back (m);
            previous = m.meter;
        }
    return kept;
}

// A stretch of the song in one signature: it starts at `startTick` with bar number `firstBar` (from 0) and ends at `endTick`
// (INT_MAX for the last one).
struct MeterSegment
{
    int startTick = 0;
    int endTick = INT_MAX;
    long long firstBar = 0;
    TimeSignature meter;
};

// Every segment of the song, in order (`markers` tidy).
inline std::vector<MeterSegment> meterSegments (const std::vector<MeterMarker>& markers, const TimeSignature& base)
{
    std::vector<MeterSegment> out;
    MeterSegment seg { 0, INT_MAX, 0, base };
    for (const auto& m : markers)
    {
        if (m.tick <= seg.startTick)
            continue;
        seg.endTick = m.tick;
        out.push_back (seg);
        seg.firstBar += (m.tick - seg.startTick) / std::max (1, seg.meter.barTicks());
        seg.startTick = m.tick;
        seg.endTick = INT_MAX;
        seg.meter = m.meter;
    }
    out.push_back (seg);
    return out;
}

// The segment that holds `tick`.
inline MeterSegment meterSegmentAt (const std::vector<MeterMarker>& markers, const TimeSignature& base, double tick)
{
    MeterSegment seg { 0, INT_MAX, 0, base };
    for (const auto& m : markers)
    {
        if (m.tick > tick)
        {
            seg.endTick = m.tick;
            break;
        }
        if (m.tick <= seg.startTick)
            continue;
        seg.firstBar += (m.tick - seg.startTick) / std::max (1, seg.meter.barTicks());
        seg.startTick = m.tick;
        seg.meter = m.meter;
    }
    return seg;
}

// The tick where bar `bar` (from 0) starts.
inline long long barStartTick (const std::vector<MeterMarker>& markers, const TimeSignature& base, long long bar)
{
    const auto segments = meterSegments (markers, base);
    for (const auto& s : segments)
        if (s.endTick == INT_MAX || bar < s.firstBar + (s.endTick - s.startTick) / std::max (1, s.meter.barTicks()))
            return s.startTick + (std::max (bar, s.firstBar) - s.firstBar) * s.meter.barTicks();
    return 0;
}

// Bar (from 0), beat in the bar (from 0) and ticks past the beat at `tick`.
struct BarPosition
{
    long long bar = 0;
    int beat = 0;
    int ticks = 0;
    TimeSignature meter;
};

inline BarPosition barPositionAt (const std::vector<MeterMarker>& markers, const TimeSignature& base, long long tick)
{
    tick = std::max (0LL, tick);
    const auto seg = meterSegmentAt (markers, base, static_cast<double> (tick));
    const long long bar = seg.meter.barTicks(), beat = seg.meter.beatTicks(), inside = tick - seg.startTick;
    return { seg.firstBar + inside / bar, static_cast<int> ((inside % bar) / beat), static_cast<int> (inside % beat), seg.meter };
}

// The signature changes the interface draws and counts bars with (with interfaceMeter()); message thread only.
inline std::vector<MeterMarker>& interfaceMeterMarkers()
{
    static std::vector<MeterMarker> markers;
    return markers;
}
} // namespace sls
