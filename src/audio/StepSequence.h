#pragma once

#include <algorithm>
#include <vector>

#include "audio/Project.h"

namespace sls::steps
{
// The channel rack's step sequencer (0.31.0), in the manner of FL Studio's: a channel whose notes in a pattern are all plain steps
// (one sixteenth long, on the grid, on the channel's step note) is shown and edited as squares; any other note - a chord, a melody,
// a longer note written in the piano roll - turns the row into a preview of the piano roll, and clearing the notes brings the
// squares back. A step channel has an active length (16 steps by default); the steps after it, up to the end of the pattern, are
// greyed out and play the active steps again, so a one-bar drum loop fills a longer pattern. Writing a step in the greyed part, or
// dragging the end of the active part, makes it longer. Plain C++ (no JUCE), so it is tested headless.

constexpr int kStepTicks = kSixteenthTicks;
constexpr int kBarSteps = 16; // 4 quarter notes: the block the active part grows by

// The note a step plays: the drum machine's first pad (the kick) on a drum channel, the root note on every other channel.
inline int stepPitch (const Channel& c) noexcept { return c.instrument == Instrument::Drums ? 36 : c.rootPitch; }

inline bool isStepNote (const Note& n, int pitch) noexcept
{
    return n.pitch == pitch && n.startTick >= 0 && n.startTick % kStepTicks == 0 && n.lengthTicks > 0 && n.lengthTicks <= kStepTicks;
}

// True when the notes are shown as squares: none at all, or only steps.
inline bool isStepSequence (const std::vector<Note>& notes, int pitch) noexcept
{
    return std::all_of (notes.begin(), notes.end(), [pitch] (const Note& n) { return isStepNote (n, pitch); });
}

inline int patternSteps (int patternTicks) noexcept { return std::max (1, patternTicks / kStepTicks); }

// The steps the notes reach (the last one's step + 1; 0 for none).
inline int extentSteps (const std::vector<Note>& notes) noexcept
{
    int extent = 0;
    for (const auto& n : notes)
        extent = std::max (extent, n.startTick / kStepTicks + 1);
    return extent;
}

// The active length in steps. `setSteps` is the length the user set (0 = not set: one bar, or as many whole bars as the notes
// reach). Never shorter than the notes, never longer than the pattern.
inline int activeSteps (const std::vector<Note>& notes, int setSteps, int patternTicks) noexcept
{
    const int extent = extentSteps (notes);
    const int wanted = setSteps > 0 ? std::max (setSteps, extent) : std::max (kBarSteps, (extent + kBarSteps - 1) / kBarSteps * kBarSteps);
    return std::clamp (wanted, 1, patternSteps (patternTicks));
}

// What plays: a step channel's active steps repeated to the end of the pattern; any other channel's notes as they are.
inline std::vector<Note> playedNotes (const Channel& c, const std::vector<Note>& notes, int setSteps, int patternTicks)
{
    if (notes.empty() || ! isStepSequence (notes, stepPitch (c)))
        return notes;
    const int active = activeSteps (notes, setSteps, patternTicks) * kStepTicks;
    if (active >= patternTicks)
        return notes;
    std::vector<Note> out;
    out.reserve (notes.size() * static_cast<size_t> ((patternTicks + active - 1) / active));
    for (int offset = 0; offset < patternTicks; offset += active)
        for (auto n : notes)
            if (n.startTick + offset < patternTicks)
            {
                n.startTick += offset;
                out.push_back (n);
            }
    std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.startTick < b.startTick; });
    return out;
}

inline bool stepOn (const std::vector<Note>& notes, int step, int pitch) noexcept
{
    return std::any_of (notes.begin(), notes.end(), [&] (const Note& n) { return n.startTick == step * kStepTicks && n.pitch == pitch; });
}

// Whether a greyed step sounds (it repeats an active one).
inline bool repeatedStepOn (const std::vector<Note>& notes, int step, int active, int pitch) noexcept
{
    return active > 0 && stepOn (notes, step % active, pitch);
}

// Makes the active part reach `step`: it grows to the end of the bar of 16 steps that holds it (or the end of the pattern), and
// the steps it takes in are filled with what they were playing (so nothing changes in the sound). Returns true when it grew.
inline bool activate (std::vector<Note>& notes, int& setSteps, int step, int patternTicks, int pitch)
{
    const int total = patternSteps (patternTicks);
    const int active = activeSteps (notes, setSteps, patternTicks);
    if (step < active || step >= total)
        return false;
    const int target = std::min (total, (step / kBarSteps + 1) * kBarSteps);
    std::vector<Note> copies;
    for (int s = active; s < target; ++s)
        for (const auto& n : notes)
            if (n.startTick == (s % active) * kStepTicks && n.pitch == pitch)
            {
                auto copy = n;
                copy.startTick = s * kStepTicks;
                copies.push_back (copy);
            }
    notes.insert (notes.end(), copies.begin(), copies.end());
    setSteps = target;
    return true;
}

// Left button: on. Right button: off. A step in the greyed part makes the active part reach it first. Returns true when anything
// changed.
inline bool setStep (std::vector<Note>& notes, int& setSteps, int step, bool on, int patternTicks, int pitch, float velocity = 0.9f)
{
    if (step < 0 || step >= patternSteps (patternTicks))
        return false;
    bool changed = activate (notes, setSteps, step, patternTicks, pitch);
    const int tick = step * kStepTicks;
    const auto it = std::find_if (notes.begin(), notes.end(), [&] (const Note& n) { return n.startTick == tick && n.pitch == pitch; });
    if (on && it == notes.end())
    {
        Note n;
        n.pitch = pitch;
        n.startTick = tick;
        n.lengthTicks = kStepTicks;
        n.velocity = velocity;
        notes.push_back (n);
        changed = true;
    }
    else if (! on && it != notes.end())
    {
        notes.erase (it);
        changed = true;
    }
    return changed;
}

// The end of the active part dragged to `steps` (1 .. the pattern's steps). Made shorter, the steps after the new end are deleted;
// made longer, the new steps are filled with what they were playing. Returns true when anything changed.
inline bool resize (std::vector<Note>& notes, int& setSteps, int steps, int patternTicks, int pitch)
{
    steps = std::clamp (steps, 1, patternSteps (patternTicks));
    const int active = activeSteps (notes, setSteps, patternTicks);
    if (steps == active && setSteps == steps)
        return false;
    if (steps > active)
    {
        std::vector<Note> copies;
        for (int s = active; s < steps; ++s)
            for (const auto& n : notes)
                if (n.startTick == (s % active) * kStepTicks && n.pitch == pitch)
                {
                    auto copy = n;
                    copy.startTick = s * kStepTicks;
                    copies.push_back (copy);
                }
        notes.insert (notes.end(), copies.begin(), copies.end());
    }
    else
        notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const Note& n) { return n.startTick >= steps * kStepTicks; }), notes.end());
    setSteps = steps;
    return true;
}
} // namespace sls::steps
