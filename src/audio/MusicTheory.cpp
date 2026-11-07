#include "audio/MusicTheory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>

namespace sls::theory
{
namespace
{
// xorshift32: the same "random" choices for the same seed.
struct Rng
{
    std::uint32_t state;
    explicit Rng (std::uint32_t seed) noexcept : state (seed != 0 ? seed : 0x2545f491u) {}
    double next() noexcept // -1..1
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double> (state) / 2147483647.5 - 1.0;
    }
};

int clampPitch (int p) noexcept { return std::clamp (p, 0, 127); }
int mod12 (int p) noexcept { return ((p % 12) + 12) % 12; }

// The notes a tool works on: the selected ones, or all of them when none is selected. Makes `selected` the right size.
std::vector<size_t> targets (const std::vector<Note>& notes, std::vector<bool>& selected)
{
    selected.resize (notes.size(), false);
    std::vector<size_t> out;
    for (size_t i = 0; i < notes.size(); ++i)
        if (selected[i])
            out.push_back (i);
    if (out.empty())
    {
        out.resize (notes.size());
        std::iota (out.begin(), out.end(), size_t { 0 });
    }
    return out;
}

// Groups of target notes that start on the same tick (chords), each sorted from the lowest pitch, in time order.
std::vector<std::vector<size_t>> chordsOf (const std::vector<Note>& notes, const std::vector<size_t>& which)
{
    std::map<int, std::vector<size_t>> byStart;
    for (const auto i : which)
        byStart[notes[i].startTick].push_back (i);
    std::vector<std::vector<size_t>> groups;
    for (auto& [start, group] : byStart)
    {
        std::sort (group.begin(), group.end(), [&] (size_t a, size_t b) { return notes[a].pitch < notes[b].pitch; });
        groups.push_back (group);
    }
    return groups;
}

// Removes the notes at `drop` (any order) from notes / selected.
void removeAt (std::vector<Note>& notes, std::vector<bool>& selected, std::vector<size_t> drop)
{
    std::sort (drop.begin(), drop.end());
    drop.erase (std::unique (drop.begin(), drop.end()), drop.end());
    for (auto it = drop.rbegin(); it != drop.rend(); ++it)
    {
        notes.erase (notes.begin() + static_cast<std::ptrdiff_t> (*it));
        selected.erase (selected.begin() + static_cast<std::ptrdiff_t> (*it));
    }
}

int indexOfScale (const char* name)
{
    const auto& all = scales();
    for (size_t i = 0; i < all.size(); ++i)
        if (std::string (all[i].name) == name)
            return static_cast<int> (i);
    return 0;
}

// A seven-note scale to build chords from: the scale itself, or the major / natural minor scale on its root.
std::vector<int> heptatonicIntervals (const Scale& scale)
{
    const auto& all = scales();
    auto mask = all[static_cast<size_t> (std::clamp (scale.type, 0, static_cast<int> (all.size()) - 1))].mask;
    if (__builtin_popcount (mask) != 7)
        mask = (mask & (1u << 3)) != 0 && (mask & (1u << 4)) == 0 ? all[static_cast<size_t> (indexOfScale ("Natural minor"))].mask
                                                                  : all[static_cast<size_t> (indexOfScale ("Major"))].mask;
    std::vector<int> intervals;
    for (int i = 0; i < 12; ++i)
        if ((mask & (1u << i)) != 0)
            intervals.push_back (i);
    return intervals;
}
} // namespace

// --- scales ---------------------------------------------------------------------------------------------------------------
const std::vector<ScaleInfo>& scales()
{
    const auto m = [] (std::initializer_list<int> steps)
    {
        std::uint16_t mask = 0;
        for (const int s : steps)
            mask = static_cast<std::uint16_t> (mask | (1u << s));
        return mask;
    };
    static const std::vector<ScaleInfo> list {
        { "Chromatic", 0x0fff },
        { "Major", m ({ 0, 2, 4, 5, 7, 9, 11 }) },
        { "Natural minor", m ({ 0, 2, 3, 5, 7, 8, 10 }) },
        { "Harmonic minor", m ({ 0, 2, 3, 5, 7, 8, 11 }) },
        { "Melodic minor", m ({ 0, 2, 3, 5, 7, 9, 11 }) },
        { "Dorian", m ({ 0, 2, 3, 5, 7, 9, 10 }) },
        { "Phrygian", m ({ 0, 1, 3, 5, 7, 8, 10 }) },
        { "Lydian", m ({ 0, 2, 4, 6, 7, 9, 11 }) },
        { "Mixolydian", m ({ 0, 2, 4, 5, 7, 9, 10 }) },
        { "Locrian", m ({ 0, 1, 3, 5, 6, 8, 10 }) },
        { "Major pentatonic", m ({ 0, 2, 4, 7, 9 }) },
        { "Minor pentatonic", m ({ 0, 3, 5, 7, 10 }) },
        { "Blues", m ({ 0, 3, 5, 6, 7, 10 }) },
    };
    return list;
}

bool Scale::contains (int pitch) const noexcept
{
    const auto& all = scales();
    const auto mask = all[static_cast<size_t> (std::clamp (type, 0, static_cast<int> (all.size()) - 1))].mask;
    return (mask & (1u << mod12 (pitch - root))) != 0;
}

int Scale::snap (int pitch) const noexcept
{
    pitch = clampPitch (pitch);
    for (int d = 0; d < 12; ++d)
    {
        if (pitch - d >= 0 && contains (pitch - d))
            return pitch - d;
        if (pitch + d <= 127 && contains (pitch + d))
            return pitch + d;
    }
    return pitch;
}

int Scale::step (int pitch, int steps) const noexcept
{
    int p = snap (pitch);
    const int dir = steps >= 0 ? 1 : -1;
    for (int s = 0; s < std::abs (steps); ++s)
    {
        int q = p + dir;
        while (q >= 0 && q <= 127 && ! contains (q))
            q += dir;
        if (q < 0 || q > 127)
            break;
        p = q;
    }
    return p;
}

std::vector<int> Scale::pitchClasses() const
{
    std::vector<int> out;
    for (int i = 0; i < 12; ++i)
        if (contains (root + i))
            out.push_back (mod12 (root + i));
    return out;
}

int Scale::size() const noexcept
{
    int n = 0;
    for (int i = 0; i < 12; ++i)
        n += contains (root + i) ? 1 : 0;
    return n;
}

const char* pitchClassName (int pitchClass)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[mod12 (pitchClass)];
}

std::string noteName (int pitch)
{
    return std::string (pitchClassName (pitch)) + std::to_string (pitch / 12 - 1);
}

// --- chords -----------------------------------------------------------------------------------------------------------------
const std::vector<ChordInfo>& chords()
{
    static const std::vector<ChordInfo> list {
        { "Major", { 0, 4, 7 } },          { "Minor", { 0, 3, 7 } },           { "Diminished", { 0, 3, 6 } },
        { "Augmented", { 0, 4, 8 } },      { "Sus2", { 0, 2, 7 } },            { "Sus4", { 0, 5, 7 } },
        { "Power (5)", { 0, 7, 12 } },     { "Major 6", { 0, 4, 7, 9 } },      { "Minor 6", { 0, 3, 7, 9 } },
        { "Dominant 7", { 0, 4, 7, 10 } }, { "Major 7", { 0, 4, 7, 11 } },     { "Minor 7", { 0, 3, 7, 10 } },
        { "Minor 7 b5", { 0, 3, 6, 10 } }, { "Diminished 7", { 0, 3, 6, 9 } }, { "Add 9", { 0, 4, 7, 14 } },
        { "Dominant 9", { 0, 4, 7, 10, 14 } }, { "Major 9", { 0, 4, 7, 11, 14 } }, { "Minor 9", { 0, 3, 7, 10, 14 } },
    };
    return list;
}

std::vector<int> chordPitches (int root, int chord, int inversion)
{
    const auto& all = chords();
    const auto& info = all[static_cast<size_t> (std::clamp (chord, 0, static_cast<int> (all.size()) - 1))];
    std::vector<int> pitches;
    pitches.reserve (info.intervals.size());
    for (const int i : info.intervals)
        pitches.push_back (root + i);
    inversion = std::clamp (inversion, 0, static_cast<int> (pitches.size()) - 1);
    for (int k = 0; k < inversion; ++k)
    {
        pitches.front() += 12;
        std::rotate (pitches.begin(), pitches.begin() + 1, pitches.end());
    }
    for (auto& p : pitches)
        p = clampPitch (p);
    return pitches;
}

std::vector<int> diatonicChord (const Scale& scale, int degree, int size, int octave)
{
    const auto intervals = heptatonicIntervals (scale);
    const int base = 12 * (octave + 1) + mod12 (scale.root);
    std::vector<int> pitches;
    for (int k = 0; k < std::clamp (size, 1, 7); ++k)
    {
        const int idx = std::max (0, degree) + 2 * k;
        pitches.push_back (clampPitch (base + intervals[static_cast<size_t> (idx % 7)] + 12 * (idx / 7)));
    }
    return pitches;
}

// --- tools --------------------------------------------------------------------------------------------------------------------
void transpose (std::vector<Note>& notes, std::vector<bool>& selected, int semitones)
{
    for (const auto i : targets (notes, selected))
        notes[i].pitch = clampPitch (notes[i].pitch + semitones);
}

void transposeInScale (std::vector<Note>& notes, std::vector<bool>& selected, const Scale& scale, int steps)
{
    for (const auto i : targets (notes, selected))
        notes[i].pitch = scale.step (notes[i].pitch, steps);
}

void snapToScale (std::vector<Note>& notes, std::vector<bool>& selected, const Scale& scale)
{
    for (const auto i : targets (notes, selected))
        notes[i].pitch = scale.snap (notes[i].pitch);
}

void quantize (std::vector<Note>& notes, std::vector<bool>& selected, int gridTicks, float strength, bool ends, int patternTicks)
{
    gridTicks = std::max (1, gridTicks);
    strength = std::clamp (strength, 0.0f, 1.0f);
    const auto onGrid = [gridTicks] (int t) { return static_cast<int> (std::lround (static_cast<double> (t) / gridTicks)) * gridTicks; };
    for (const auto i : targets (notes, selected))
    {
        auto& n = notes[i];
        const int end = n.startTick + n.lengthTicks;
        int start = n.startTick + static_cast<int> (std::lround (strength * (onGrid (n.startTick) - n.startTick)));
        start = std::clamp (start, 0, std::max (0, patternTicks - 1));
        int newEnd = end + (start - n.startTick); // the length is kept unless the ends are quantized too
        if (ends)
        {
            const int target = std::max (onGrid (end), start + gridTicks);
            newEnd = end + static_cast<int> (std::lround (strength * (target - end)));
            newEnd = std::max (newEnd, start + std::min (gridTicks, n.lengthTicks));
        }
        n.startTick = start;
        n.lengthTicks = std::clamp (newEnd - start, 1, std::max (1, patternTicks - start));
    }
}

void humanize (std::vector<Note>& notes, std::vector<bool>& selected, int ticks, float velocity, std::uint32_t seed, int patternTicks)
{
    Rng rng (seed);
    for (const auto i : targets (notes, selected))
    {
        auto& n = notes[i];
        const int shift = static_cast<int> (std::lround (rng.next() * ticks));
        const int end = n.startTick + n.lengthTicks;
        n.startTick = std::clamp (n.startTick + shift, 0, std::max (0, patternTicks - 1));
        n.lengthTicks = std::clamp (end - n.startTick, std::max (1, n.lengthTicks / 2), std::max (1, patternTicks - n.startTick));
        n.velocity = std::clamp (n.velocity + static_cast<float> (rng.next()) * velocity, 0.05f, 1.0f);
    }
}

void legato (std::vector<Note>& notes, std::vector<bool>& selected, int patternTicks)
{
    const auto which = targets (notes, selected);
    for (const auto i : which)
    {
        int next = -1;
        for (const auto j : which)
            if (notes[j].startTick > notes[i].startTick && (next < 0 || notes[j].startTick < next))
                next = notes[j].startTick;
        if (next > 0)
            notes[i].lengthTicks = next - notes[i].startTick;
        else
            notes[i].lengthTicks = std::max (notes[i].lengthTicks, 1);
        notes[i].lengthTicks = std::min (notes[i].lengthTicks, std::max (1, patternTicks - notes[i].startTick));
    }
}

void staccato (std::vector<Note>& notes, std::vector<bool>& selected, float factor, int minimumTicks)
{
    for (const auto i : targets (notes, selected))
        notes[i].lengthTicks = std::max (std::min (minimumTicks, notes[i].lengthTicks),
                                         static_cast<int> (std::lround (notes[i].lengthTicks * std::clamp (factor, 0.05f, 1.0f))));
}

void strum (std::vector<Note>& notes, std::vector<bool>& selected, int ticks, bool up)
{
    for (auto group : chordsOf (notes, targets (notes, selected)))
    {
        if (! up)
            std::reverse (group.begin(), group.end());
        for (size_t r = 1; r < group.size(); ++r)
        {
            auto& n = notes[group[r]];
            const int end = n.startTick + n.lengthTicks;
            const int offset = std::min (static_cast<int> (r) * ticks, std::max (0, n.lengthTicks - std::max (15, n.lengthTicks / 4)));
            n.startTick += offset;
            n.lengthTicks = end - n.startTick;
        }
    }
}

void arpeggiate (std::vector<Note>& notes, std::vector<bool>& selected, int rateTicks, ArpPattern pattern, std::uint32_t seed)
{
    rateTicks = std::max (10, rateTicks);
    Rng rng (seed);
    std::vector<Note> made;
    std::vector<size_t> drop;
    for (const auto& group : chordsOf (notes, targets (notes, selected)))
    {
        if (group.size() < 2)
            continue; // a single note is left as it is
        int length = 0;
        float velocity = 0.0f;
        std::vector<int> pitches;
        for (const auto i : group)
        {
            length = std::max (length, notes[i].lengthTicks);
            velocity += notes[i].velocity / static_cast<float> (group.size());
            pitches.push_back (notes[i].pitch);
        }
        std::vector<int> order;
        switch (pattern)
        {
            case ArpPattern::Up: order = pitches; break;
            case ArpPattern::Down: order.assign (pitches.rbegin(), pitches.rend()); break;
            case ArpPattern::UpDown:
                order = pitches;
                for (size_t k = pitches.size() - 2; k >= 1; --k)
                    order.push_back (pitches[k]);
                break;
            case ArpPattern::Random: break;
        }
        const int start = notes[group.front()].startTick;
        int last = -1;
        const bool varied = std::any_of (pitches.begin(), pitches.end(), [&] (int p) { return p != pitches.front(); });
        for (int t = start, k = 0; t < start + length; t += rateTicks, ++k)
        {
            Note n;
            if (pattern == ArpPattern::Random)
            {
                int pick = 0;
                do
                    pick = static_cast<int> (std::lround ((rng.next() * 0.5 + 0.5) * static_cast<double> (pitches.size() - 1)));
                while (pitches[static_cast<size_t> (pick)] == last && varied);
                n.pitch = pitches[static_cast<size_t> (pick)];
            }
            else
                n.pitch = order[static_cast<size_t> (k) % order.size()];
            last = n.pitch;
            n.startTick = t;
            n.lengthTicks = std::min (rateTicks, start + length - t);
            n.velocity = velocity;
            made.push_back (n);
        }
        drop.insert (drop.end(), group.begin(), group.end());
    }
    if (drop.empty())
        return;
    // the notes that were not part of a chord keep their selection; the new ones are selected
    removeAt (notes, selected, drop);
    for (const auto& n : made)
    {
        notes.push_back (n);
        selected.push_back (true);
    }
}

void chop (std::vector<Note>& notes, std::vector<bool>& selected, int pieceTicks)
{
    pieceTicks = std::max (10, pieceTicks);
    std::vector<Note> made;
    std::vector<size_t> drop;
    for (const auto i : targets (notes, selected))
    {
        const auto n = notes[i];
        if (n.lengthTicks <= pieceTicks)
            continue;
        for (int t = 0; t < n.lengthTicks; t += pieceTicks)
        {
            Note piece = n;
            piece.startTick = n.startTick + t;
            piece.lengthTicks = std::min (pieceTicks, n.lengthTicks - t);
            if (t > 0)
                piece.link = NoteLink::None;
            made.push_back (piece);
        }
        drop.push_back (i);
    }
    removeAt (notes, selected, drop);
    for (const auto& n : made)
    {
        notes.push_back (n);
        selected.push_back (true);
    }
}

void flip (std::vector<Note>& notes, std::vector<bool>& selected)
{
    const auto which = targets (notes, selected);
    if (which.empty())
        return;
    int lo = 127, hi = 0;
    for (const auto i : which)
    {
        lo = std::min (lo, notes[i].pitch);
        hi = std::max (hi, notes[i].pitch);
    }
    for (const auto i : which)
        notes[i].pitch = clampPitch (lo + hi - notes[i].pitch);
}

void velocityRamp (std::vector<Note>& notes, std::vector<bool>& selected, float from, float to)
{
    const auto which = targets (notes, selected);
    if (which.empty())
        return;
    int first = notes[which.front()].startTick, last = first;
    for (const auto i : which)
    {
        first = std::min (first, notes[i].startTick);
        last = std::max (last, notes[i].startTick);
    }
    for (const auto i : which)
    {
        const float x = last > first ? static_cast<float> (notes[i].startTick - first) / static_cast<float> (last - first) : 0.0f;
        notes[i].velocity = std::clamp (from + (to - from) * x, 0.0f, 1.0f);
    }
}

void setVelocity (std::vector<Note>& notes, std::vector<bool>& selected, float velocity)
{
    for (const auto i : targets (notes, selected))
        notes[i].velocity = std::clamp (velocity, 0.0f, 1.0f);
}

void randomizeVelocity (std::vector<Note>& notes, std::vector<bool>& selected, float low, float high, std::uint32_t seed)
{
    Rng rng (seed);
    for (const auto i : targets (notes, selected))
        notes[i].velocity = std::clamp (low + (high - low) * static_cast<float> (0.5 * rng.next() + 0.5), 0.0f, 1.0f);
}

// --- generators -------------------------------------------------------------------------------------------------------------
const std::vector<ProgressionInfo>& progressions()
{
    static const std::vector<ProgressionInfo> list {
        { "Pop: I - V - vi - IV", { 0, 4, 5, 3 }, false },
        { "Sensitive: vi - IV - I - V", { 5, 3, 0, 4 }, false },
        { "Doo-wop: I - vi - IV - V", { 0, 5, 3, 4 }, false },
        { "Classic: I - IV - V - I", { 0, 3, 4, 0 }, false },
        { "Jazz: ii - V - I - vi (sevenths)", { 1, 4, 0, 5 }, true },
        { "Soul: I - iii - IV - ii (sevenths)", { 0, 2, 3, 1 }, true },
        { "Royal road: IV - V - iii - vi", { 3, 4, 2, 5 }, false },
        { "Canon: I - V - vi - iii - IV - I - IV - V", { 0, 4, 5, 2, 3, 0, 3, 4 }, false },
        { "Minor drama: i - VI - III - VII", { 0, 5, 2, 6 }, false },
        { "Epic: i - VII - VI - VII", { 0, 6, 5, 6 }, false },
        { "Dark: i - iv - v - i", { 0, 3, 4, 0 }, false },
        { "Lo-fi: ii - V - I - I (sevenths)", { 1, 4, 0, 0 }, true },
    };
    return list;
}

const char* chordRhythmName (ChordRhythm r)
{
    switch (r)
    {
        case ChordRhythm::Held: return "Held chords";
        case ChordRhythm::Stabs: return "Syncopated stabs";
        case ChordRhythm::Pulse: return "Eighth-note pulse";
        case ChordRhythm::Arpeggio: return "Arpeggio";
    }
    return "Held chords";
}

std::vector<ChordSpan> progressionChords (const Scale& scale, int which, int chordTicks, int patternTicks)
{
    const auto& all = progressions();
    const auto& prog = all[static_cast<size_t> (std::clamp (which, 0, static_cast<int> (all.size()) - 1))];
    chordTicks = std::max (kTicksPerBeat, chordTicks);
    std::vector<ChordSpan> out;
    for (int t = 0, k = 0; t < patternTicks; t += chordTicks, ++k)
    {
        const int degree = prog.degrees[static_cast<size_t> (k) % prog.degrees.size()];
        const auto pitches = diatonicChord (scale, degree, prog.sevenths ? 4 : 3, 4);
        ChordSpan span;
        span.startTick = t;
        span.lengthTicks = std::min (chordTicks, patternTicks - t);
        span.root = mod12 (pitches.front());
        for (const int p : pitches)
            span.pitchClasses.push_back (mod12 (p));
        out.push_back (span);
    }
    return out;
}

std::vector<Note> progressionNotes (const Scale& scale, int which, ChordRhythm rhythm, int chordTicks, int patternTicks, int octave)
{
    const auto spans = progressionChords (scale, which, chordTicks, patternTicks);
    const double centre = 12.0 * (octave + 1) + 7.0; // around G4 for octave 4
    std::vector<Note> out;
    std::vector<int> previous;
    for (const auto& span : spans)
    {
        // every inversion of the chord in the octaves around the centre; the one that moves least from the previous chord wins
        // (the first chord: the one closest to the centre)
        std::vector<int> base (span.pitchClasses.begin(), span.pitchClasses.end());
        for (size_t k = 1; k < base.size(); ++k)
            while (base[k] <= base[k - 1])
                base[k] += 12; // stacked upwards from the root
        std::vector<int> best;
        double bestCost = 1.0e9;
        for (int inversion = 0; inversion < static_cast<int> (base.size()); ++inversion)
        {
            auto v = base;
            for (int k = 0; k < inversion; ++k)
            {
                v.front() += 12;
                std::rotate (v.begin(), v.begin() + 1, v.end());
            }
            for (int shift = -2; shift <= 10; ++shift)
            {
                std::vector<int> cand (v);
                for (auto& p : cand)
                    p += 12 * shift;
                if (cand.front() < 0 || cand.back() > 127)
                    continue;
                const double mean = std::accumulate (cand.begin(), cand.end(), 0.0) / static_cast<double> (cand.size());
                double cost = std::abs (mean - centre) * (previous.empty() ? 1.0 : 0.35);
                if (! previous.empty())
                    for (size_t k = 0; k < cand.size(); ++k)
                        cost += std::abs (cand[k] - previous[std::min (k, previous.size() - 1)]);
                if (cost < bestCost)
                {
                    bestCost = cost;
                    best = cand;
                }
            }
        }
        previous = best;

        const auto add = [&] (int at, int length, float velocity)
        {
            if (at >= span.startTick + span.lengthTicks || length <= 0)
                return;
            length = std::min (length, span.startTick + span.lengthTicks - at);
            for (const int p : best)
                out.push_back ({ clampPitch (p), at, length, velocity, NoteLink::None });
        };
        const int s = span.startTick, len = span.lengthTicks;
        switch (rhythm)
        {
            case ChordRhythm::Held:
                add (s, len - 20, 0.72f);
                break;
            case ChordRhythm::Stabs:
            {
                // a dotted quarter, an eighth, then the rest (scaled to the chord's length)
                const double unit = len / 8.0;
                add (s, static_cast<int> (3 * unit) - 20, 0.75f);
                add (s + static_cast<int> (3 * unit), static_cast<int> (unit) - 20, 0.58f);
                add (s + static_cast<int> (5 * unit), static_cast<int> (3 * unit) - 20, 0.66f);
                break;
            }
            case ChordRhythm::Pulse:
                for (int t = s, k = 0; t < s + len; t += kTicksPerBeat / 2, ++k)
                    add (t, kTicksPerBeat / 2 - 40, k % 2 == 0 ? 0.74f : 0.58f);
                break;
            case ChordRhythm::Arpeggio:
            {
                std::vector<Note> chord;
                std::vector<bool> sel;
                for (const int p : best)
                {
                    chord.push_back ({ clampPitch (p), s, len, 0.7f, NoteLink::None });
                    sel.push_back (true);
                }
                arpeggiate (chord, sel, kTicksPerBeat / 2, ArpPattern::UpDown, 1);
                out.insert (out.end(), chord.begin(), chord.end());
                break;
            }
        }
    }
    return out;
}

std::vector<ChordSpan> detectChords (const std::vector<Note>& notes, int spanTicks, int patternTicks)
{
    struct Template
    {
        std::vector<int> intervals;
    };
    static const std::vector<Template> templates { { { 0, 4, 7 } }, { { 0, 3, 7 } }, { { 0, 3, 6 } }, { { 0, 4, 7, 10 } },
                                                   { { 0, 4, 7, 11 } }, { { 0, 3, 7, 10 } }, { { 0, 4, 7, 9 } }, { { 0, 5, 7 } }, { { 0, 7 } } };
    spanTicks = std::max (kTicksPerBeat / 4, spanTicks);
    std::vector<ChordSpan> raw;
    std::vector<bool> found;
    for (int s = 0; s < patternTicks; s += spanTicks)
    {
        const int e = std::min (patternTicks, s + spanTicks);
        std::array<double, 12> weight {};
        int lowest = 128;
        for (const auto& n : notes)
        {
            const int overlap = std::min (e, n.startTick + n.lengthTicks) - std::max (s, n.startTick);
            if (overlap <= 0)
                continue;
            weight[static_cast<size_t> (mod12 (n.pitch))] += overlap;
            lowest = std::min (lowest, n.pitch);
        }
        ChordSpan span;
        span.startTick = s;
        span.lengthTicks = e - s;
        if (lowest > 127)
        {
            raw.push_back (span);
            found.push_back (false);
            continue;
        }
        const double total = std::accumulate (weight.begin(), weight.end(), 0.0);
        double bestScore = -1.0e18;
        for (int root = 0; root < 12; ++root)
        {
            if (weight[static_cast<size_t> (root)] <= 0.0)
                continue; // the root must be played
            for (const auto& t : templates)
            {
                double score = 0.0;
                std::array<bool, 12> in {};
                for (const int i : t.intervals)
                    in[static_cast<size_t> (mod12 (root + i))] = true;
                for (int pc = 0; pc < 12; ++pc)
                    score += in[static_cast<size_t> (pc)] ? weight[static_cast<size_t> (pc)] : -0.5 * weight[static_cast<size_t> (pc)];
                score -= 0.002 * total * static_cast<double> (t.intervals.size()); // the smaller chord when they explain as much
                if (root == mod12 (lowest))
                    score += 0.01 * total; // the bass note decides a tie
                if (score > bestScore)
                {
                    bestScore = score;
                    span.root = root;
                    span.pitchClasses.clear();
                    for (const int i : t.intervals)
                        span.pitchClasses.push_back (mod12 (root + i));
                }
            }
        }
        raw.push_back (span);
        found.push_back (true);
    }
    // parts without notes take the chord before them (or the first one found)
    int firstFound = -1;
    for (size_t i = 0; i < raw.size(); ++i)
        if (found[i])
        {
            firstFound = static_cast<int> (i);
            break;
        }
    if (firstFound < 0)
        return {};
    for (size_t i = 0; i < raw.size(); ++i)
        if (! found[i])
        {
            const auto& from = i > 0 && i > static_cast<size_t> (firstFound) ? raw[i - 1] : raw[static_cast<size_t> (firstFound)];
            raw[i].root = from.root;
            raw[i].pitchClasses = from.pitchClasses;
        }
    // neighbours with the same chord become one
    std::vector<ChordSpan> merged;
    for (const auto& span : raw)
        if (! merged.empty() && merged.back().root == span.root && merged.back().pitchClasses == span.pitchClasses)
            merged.back().lengthTicks += span.lengthTicks;
        else
            merged.push_back (span);
    return merged;
}

const char* bassStyleName (BassStyle s)
{
    switch (s)
    {
        case BassStyle::Roots: return "Roots (one per chord)";
        case BassStyle::Pulse: return "Eighth-note pulse";
        case BassStyle::Octaves: return "Octaves";
        case BassStyle::RootFifth: return "Root and fifth";
        case BassStyle::Walking: return "Walking";
        case BassStyle::Syncopated: return "Syncopated";
    }
    return "Roots";
}

std::vector<Note> basslineNotes (const std::vector<ChordSpan>& chordSpans, BassStyle style, const Scale& scale, int lowest)
{
    std::vector<Note> out;
    lowest = std::clamp (lowest, 0, 115);
    const auto rootPitch = [lowest] (int pc) { return lowest + mod12 (pc - lowest); };
    const int eighth = kTicksPerBeat / 2, sixteenth = kTicksPerBeat / 4;
    for (size_t c = 0; c < chordSpans.size(); ++c)
    {
        const auto& span = chordSpans[c];
        const int root = rootPitch (span.root);
        // the fifth of the chord (or its third note when it has no perfect fifth), above the root
        int fifth = root + 7;
        if (std::find (span.pitchClasses.begin(), span.pitchClasses.end(), mod12 (span.root + 7)) == span.pitchClasses.end() && span.pitchClasses.size() >= 3)
            fifth = root + mod12 (span.pitchClasses[2] - span.root);
        int third = root + 4;
        if (span.pitchClasses.size() >= 2)
            third = root + mod12 (span.pitchClasses[1] - span.root);
        const int end = span.startTick + span.lengthTicks;
        const auto add = [&] (int pitch, int at, int length, float velocity)
        {
            if (at >= end || length <= 0)
                return;
            out.push_back ({ clampPitch (pitch), at, std::min (length, end - at), velocity, NoteLink::None });
        };
        switch (style)
        {
            case BassStyle::Roots:
                add (root, span.startTick, span.lengthTicks - 20, 0.9f);
                break;
            case BassStyle::Pulse:
                for (int t = span.startTick, k = 0; t < end; t += eighth, ++k)
                    add (root, t, eighth - 40, k % 2 == 0 ? 0.9f : 0.72f);
                break;
            case BassStyle::Octaves:
                for (int t = span.startTick, k = 0; t < end; t += eighth, ++k)
                    add (k % 2 == 0 ? root : root + 12, t, eighth - 40, k % 2 == 0 ? 0.9f : 0.75f);
                break;
            case BassStyle::RootFifth:
                for (int t = span.startTick, k = 0; t < end; t += kTicksPerBeat, ++k)
                    add (k % 2 == 0 ? root : fifth, t, kTicksPerBeat - 40, k % 2 == 0 ? 0.9f : 0.78f);
                break;
            case BassStyle::Walking:
            {
                const int beats = std::max (1, span.lengthTicks / kTicksPerBeat);
                const int nextRoot = c + 1 < chordSpans.size() ? rootPitch (chordSpans[c + 1].root) : root;
                // root, third, fifth ... and a note a semitone (or a scale step) away from the next chord's root to lead into it
                std::vector<int> line { root, third, fifth, root + 12 };
                for (int b = 0; b < beats; ++b)
                {
                    int pitch = line[static_cast<size_t> (b) % line.size()];
                    if (b == beats - 1 && beats > 1)
                        pitch = scale.contains (nextRoot - 1) || ! scale.contains (nextRoot + 1) ? nextRoot - 1 : nextRoot + 1;
                    add (pitch, span.startTick + b * kTicksPerBeat, kTicksPerBeat - 30, b == 0 ? 0.9f : 0.78f);
                }
                break;
            }
            case BassStyle::Syncopated:
            {
                // 16ths: 0 root (3), 3 root (3), 6 octave (2), 8 root (3), 11 fifth (3), 14 root (2), repeated every bar
                const int hits[][3] = { { 0, 0, 3 }, { 3, 0, 3 }, { 6, 12, 2 }, { 8, 0, 3 }, { 11, 7, 3 }, { 14, 0, 2 } };
                for (int barStart = span.startTick; barStart < end; barStart += 16 * sixteenth)
                    for (const auto& h : hits)
                        add (h[1] == 7 ? fifth : root + h[1], barStart + h[0] * sixteenth, h[2] * sixteenth - 30, h[0] % 4 == 0 ? 0.9f : 0.74f);
                break;
            }
        }
    }
    return out;
}
} // namespace sls::theory
