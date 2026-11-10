#pragma once

#include <algorithm>
#include <vector>

#include "audio/Project.h"

namespace sls::steps
{
// The channel rack's step sequencer, in the manner of FL Studio's: a channel whose notes in a pattern are all plain steps (one
// sixteenth long, on the grid, on the channel's step note) is shown and edited as squares; any other note - a chord, a melody, a
// longer note written in the piano roll - turns the row into a preview of the piano roll, and clearing the notes brings the squares
// back. Since 0.42.0 the grid is exactly the pattern (16 steps for the default one bar of 4/4, 4 more for every quarter the + button
// adds): there are no greyed steps and nothing grows or repeats by itself. Plain C++ (no JUCE), so it is tested headless.

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

// The steps shown and played: the whole pattern (0.42.0; the notes and the length the user set before no longer change it).
inline int activeSteps (const std::vector<Note>& /*notes*/, int /*setSteps*/, int patternTicks) noexcept
{
    return patternSteps (patternTicks);
}

// What plays: the notes as written (0.42.0). Before, a step channel's active steps were repeated to the end of the pattern; now the
// grid is the whole pattern and nothing repeats by itself.
inline std::vector<Note> playedNotes (const std::vector<Note>& notes, int /*patternTicks*/)
{
    return notes;
}

// Projects saved from 0.31.0 to 0.41.0 (0.42.0 opens them as they sounded): a step channel played its active part (the length
// the user set, or one bar, or the whole bars the notes reached) again and again to the end of the pattern. That repetition is
// written out as real notes, since the grid no longer repeats by itself.
inline int legacyActiveSteps (const std::vector<Note>& notes, int setSteps, int patternTicks) noexcept
{
    const int extent = extentSteps (notes);
    const int wanted = setSteps > 0 ? std::max (setSteps, extent) : std::max (kBarSteps, (extent + kBarSteps - 1) / kBarSteps * kBarSteps);
    return std::clamp (wanted, 1, patternSteps (patternTicks));
}

inline void bakeLegacyRepeats (std::vector<Note>& notes, int setSteps, int patternTicks, int pitch)
{
    if (notes.empty() || ! isStepSequence (notes, pitch))
        return;
    const int active = legacyActiveSteps (notes, setSteps, patternTicks) * kStepTicks;
    if (active >= patternTicks)
        return;
    std::vector<Note> out;
    for (int offset = 0; offset < patternTicks; offset += active)
        for (auto n : notes)
            if (n.startTick < active && n.startTick + offset < patternTicks)
            {
                n.startTick += offset;
                out.push_back (n);
            }
    std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.startTick < b.startTick; });
    notes = std::move (out);
}

inline bool stepOn (const std::vector<Note>& notes, int step, int pitch) noexcept
{
    return std::any_of (notes.begin(), notes.end(), [&] (const Note& n) { return n.startTick == step * kStepTicks && n.pitch == pitch; });
}

// Left button: on. Right button: off. Returns true when anything changed. (`setSteps` is kept for the callers; it is not used.)
inline bool setStep (std::vector<Note>& notes, int& setSteps, int step, bool on, int patternTicks, int pitch, float velocity = 0.9f)
{
    if (step < 0 || step >= patternSteps (patternTicks))
        return false;
    static_cast<void> (setSteps);
    bool changed = false;
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

} // namespace sls::steps
