#pragma once

#include <cmath>
#include <cstdio>
#include <string>

#include "audio/NoteSequence.h"
#include "audio/TimeSignature.h"

namespace sls
{
// Positions as the playlist's ruler and readouts show them, in the project's time signature (0.46.0; 4/4 before).

// "3.2.120": bar 3, beat 2, 120 ticks past the beat (all counting from 1 except the ticks).
inline std::string formatBarsBeatsTicks (long long tick, const TimeSignature& meter = interfaceMeter())
{
    if (tick < 0)
        tick = 0;
    const long long bar = meter.barTicks(), beat = meter.beatTicks();
    char buffer[48];
    std::snprintf (buffer, sizeof buffer, "%lld.%lld.%03lld", tick / bar + 1, (tick % bar) / beat + 1, tick % beat);
    return buffer;
}

// A length in ticks as bars, beats and ticks that it spans (not a position, so it counts from 0): "1.0.240".
inline std::string formatLengthBarsBeatsTicks (long long ticks, const TimeSignature& meter = interfaceMeter())
{
    if (ticks < 0)
        ticks = 0;
    const long long bar = meter.barTicks(), beat = meter.beatTicks();
    char buffer[48];
    std::snprintf (buffer, sizeof buffer, "%lld.%lld.%03lld", ticks / bar, (ticks % bar) / beat, ticks % beat);
    return buffer;
}

// "1:04.250" (minutes:seconds.milliseconds), rounded to the millisecond.
inline std::string formatSeconds (double seconds)
{
    if (! std::isfinite (seconds) || seconds < 0.0)
        seconds = 0.0;
    const long long ms = std::llround (seconds * 1000.0);
    char buffer[48];
    std::snprintf (buffer, sizeof buffer, "%lld:%02lld.%03lld", ms / 60000, (ms / 1000) % 60, ms % 1000);
    return buffer;
}

// The smallest "round" time step (1 ms ... 2 minutes) that is at least `minPixels` wide when one second is
// `pixelsPerSecond` wide: where the ruler puts its time marks.
inline double niceTimeStepSeconds (double pixelsPerSecond, double minPixels)
{
    static constexpr double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0 };
    for (double step : steps)
        if (step * pixelsPerSecond >= minPixels)
            return step;
    return 120.0;
}

// How many bars apart the ruler numbers its bars: 1, 2, 4, 8... so that two numbers are at least `minPixels` apart.
inline int barLabelEvery (double pixelsPerBar, double minPixels)
{
    int every = 1;
    while (every < 64 && pixelsPerBar * every < minPixels)
        every *= 2;
    return every;
}
} // namespace sls
