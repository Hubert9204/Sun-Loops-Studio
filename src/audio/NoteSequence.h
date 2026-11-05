#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace sls
{
constexpr int kTicksPerBeat = 480;

struct Note
{
    int pitch = 60;                    // MIDI note number, 0..127
    int startTick = 0;
    int lengthTicks = kTicksPerBeat;
    float velocity = 0.8f;             // 0..1
};

// Immutable snapshot of the pattern, read by the audio thread.
// Build it with Sequence::make(), then hand it to SequenceExchange::publish().
struct Sequence
{
    std::vector<Note> notes;           // sorted by startTick, all inside [0, lengthTicks)
    int lengthTicks = 16 * kTicksPerBeat; // loop length
    std::uint64_t revision = 0;        // assigned by SequenceExchange

    static Sequence make (std::vector<Note> input, int loopLengthTicks)
    {
        Sequence s;
        s.lengthTicks = std::max (1, loopLengthTicks);

        std::vector<Note> kept;
        kept.reserve (input.size());
        for (auto n : input)
        {
            if (n.startTick < 0 || n.startTick >= s.lengthTicks || n.lengthTicks <= 0)
                continue;
            n.pitch = std::clamp (n.pitch, 0, 127);
            n.velocity = std::clamp (n.velocity, 0.0f, 1.0f);
            n.lengthTicks = std::min (n.lengthTicks, s.lengthTicks - n.startTick);
            kept.push_back (n);
        }

        std::stable_sort (kept.begin(), kept.end(), [] (const Note& a, const Note& b)
        {
            return a.startTick != b.startTick ? a.startTick < b.startTick : a.pitch < b.pitch;
        });

        s.notes = std::move (kept);
        return s;
    }
};
} // namespace sls
