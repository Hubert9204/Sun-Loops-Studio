#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace sls
{
constexpr int kTicksPerBeat = 480;
constexpr int kSixteenthTicks = kTicksPerBeat / 4; // the step of the step sequencer and of the swing

// How a note joins the one sounding before it in the same channel (see Player::noteOn): Legato = the same sound goes on, its
// pitch jumps to this note's pitch (no new attack); Slide = the same, but the pitch glides there (at most an eighth note). With
// no note sounding at its start, a Legato or Slide note simply plays as a normal note.
enum class NoteLink : std::uint8_t { None, Legato, Slide };

struct Note
{
    int pitch = 60;                    // MIDI note number, 0..127
    int startTick = 0;
    int lengthTicks = kTicksPerBeat;
    float velocity = 0.8f;             // 0..1
    NoteLink link = NoteLink::None;
};

// Immutable snapshot of the pattern, read by the audio thread.
// Sequence::make() validates and sorts a list of notes (used when a Project is built).
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
