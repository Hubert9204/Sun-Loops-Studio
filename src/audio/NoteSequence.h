#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sls
{
constexpr int kTicksPerBeat = 480;
constexpr int kSixteenthTicks = kTicksPerBeat / 4; // the step of the step sequencer and of the swing
constexpr float kMaxSwingShiftTicks = 60.0f; // swing 100 % delays off-beat notes by half a 16th step

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
    // 0.62.0, as the note properties of FL Studio's Piano Roll (its event lanes):
    float pan = 0.0f;                  // -1 (left) .. +1 (right), on top of the channel's pan
    float release = 0.5f;              // 0..1: 0.5 leaves the instrument's release alone, 0 is a quarter of it, 1 four times
    float finePitch = 0.0f;            // cents, -1200..+1200
    std::uint8_t colour = 0;           // 0..15: the note's colour group (shown in the Piano Roll)
    bool muted = false;                // a muted note stays in the pattern but does not play (the Mute tool)

    bool hasProperties() const noexcept
    {
        return std::abs (pan) > 0.0f || std::abs (release - 0.5f) > 0.0f || std::abs (finePitch) > 0.0f || colour != 0 || muted;
    }
    // The factor on the release time (0.25 .. 1 .. 4).
    float releaseScale() const noexcept { return std::pow (4.0f, (std::clamp (release, 0.0f, 1.0f) - 0.5f) * 2.0f); }
};

// Immutable snapshot of the pattern, read by the audio thread.
// Sequence::make() validates and sorts a list of notes (used when a Project is built).
struct Sequence
{
    std::vector<Note> notes;           // sorted by startTick, all inside [0, lengthTicks)
    int lengthTicks = 16 * kTicksPerBeat; // loop length
    std::uint64_t revision = 0;        // assigned by SequenceExchange

    static Sequence make (const std::vector<Note>& input, int loopLengthTicks)
    {
        Sequence s;
        s.lengthTicks = std::max (1, loopLengthTicks);

        std::vector<Note> kept;
        kept.reserve (input.size());
        for (auto n : input)
        {
            if (n.startTick < 0 || n.startTick >= s.lengthTicks || n.lengthTicks <= 0)
                continue;
            if (n.muted) // 0.62.0: a muted note is not played
                continue;
            n.pitch = std::clamp (n.pitch, 0, 127);
            n.velocity = std::clamp (n.velocity, 0.0f, 1.0f);
            n.pan = std::isfinite (n.pan) ? std::clamp (n.pan, -1.0f, 1.0f) : 0.0f;
            n.release = std::isfinite (n.release) ? std::clamp (n.release, 0.0f, 1.0f) : 0.5f;
            n.finePitch = std::isfinite (n.finePitch) ? std::clamp (n.finePitch, -1200.0f, 1200.0f) : 0.0f;
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
