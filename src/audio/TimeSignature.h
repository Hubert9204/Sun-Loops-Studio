#pragma once

#include <string>
#include <vector>

#include "audio/NoteSequence.h"

namespace sls
{
// The time signature (0.46.0): beats per bar and the note value of a beat. One for the whole project. Only signatures whose bar
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
} // namespace sls
