#include "audio/LoopStarter.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace sls::loopstarter
{
namespace
{
constexpr int kBeat = kTicksPerBeat;
constexpr int kBar = 4 * kBeat;
constexpr int k16 = kBeat / 4;
constexpr int k8 = kBeat / 2;

// A small random generator whose results are the same with every compiler and library (the standard distributions are not).
struct Rng
{
    std::uint32_t state;
    explicit Rng (std::uint32_t seed) : state (seed * 2654435761u + 0x9e3779b9u) { next(); }
    std::uint32_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    int below (int n) noexcept { return n <= 1 ? 0 : static_cast<int> (next() % static_cast<std::uint32_t> (n)); }
    bool chance (int percent) noexcept { return below (100) < percent; }
    float between (float a, float b) noexcept { return a + (b - a) * static_cast<float> (next() % 10001u) / 10000.0f; }
};

std::uint32_t mix (std::uint32_t a, std::uint32_t b) noexcept
{
    return (a * 0x85ebca6bu) ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2));
}

void hit (std::vector<Note>& notes, int key, int tick, float velocity, int length = k16)
{
    notes.push_back ({ key, tick, length, std::clamp (velocity, 0.05f, 1.0f) });
}

// --- drums ------------------------------------------------------------------------------------------------------------------
// One bar of a groove at `at` (bar `b` of `bars`), on 16th steps. Each genre has its fixed backbone and parts chosen by chance.
struct Groove
{
    Rng& r;
    std::vector<Note>& n;
    int at = 0;
    void step (int key, int s, float v, int length = k16) { hit (n, key, at + s * k16, v, length); }
};

std::vector<Note> drums (Genre g, int bars, std::uint32_t seed)
{
    using namespace drum;
    std::vector<Note> notes;
    Rng r (mix (seed, 11));
    // choices that hold for the whole loop
    const int kickTemplate = r.below (4);
    const bool rimSyncopation = r.chance (60);
    const bool fill = r.chance (70);
    const bool hatsSixteenths = r.chance (50);
    const int hatAccent = r.below (3);

    for (int b = 0; b < bars; ++b)
    {
        Groove gr { r, notes, b * kBar };
        const bool last = b == bars - 1;
        const bool fillHere = last && fill && bars > 1;
        switch (g)
        {
            case Genre::House:
                for (int beat = 0; beat < 4; ++beat)
                {
                    gr.step (kick, beat * 4, beat == 0 ? 1.0f : 0.92f);
                    gr.step (openHat, beat * 4 + 2, 0.7f + 0.05f * static_cast<float> (beat % 2), k8);
                    if (hatsSixteenths)
                    {
                        gr.step (closedHat, beat * 4 + 1, 0.3f);
                        gr.step (closedHat, beat * 4 + 3, 0.4f);
                    }
                }
                gr.step (clap, 4, 0.9f);
                gr.step (clap, 12, 0.92f);
                if (rimSyncopation)
                    for (const int s : { 3, 10 })
                        gr.step (rim, s + (kickTemplate % 2), 0.45f);
                if (fillHere)
                    for (int s = 13; s < 16; ++s)
                        gr.step (clap, s, 0.5f + 0.12f * static_cast<float> (s - 13));
                break;

            case Genre::Techno:
                for (int beat = 0; beat < 4; ++beat)
                {
                    gr.step (kick, beat * 4, 1.0f);
                    gr.step (openHat, beat * 4 + 2, 0.55f, k8);
                    gr.step (extra51, beat * 4 + 2, 0.35f); // ride (modern kit)
                    for (int s = 0; s < 4; ++s)
                        gr.step (closedHat, beat * 4 + s, s == hatAccent ? 0.55f : 0.25f);
                    if (kickTemplate == 3 && beat % 2 == 1)
                        gr.step (kick, beat * 4 + 3, 0.35f); // a rumbling ghost kick
                }
                gr.step (snare2, 4, 0.7f);
                gr.step (snare2, 12, 0.72f);
                if (rimSyncopation)
                    gr.step (rim, 7, 0.5f), gr.step (rim, 14, 0.4f);
                break;

            case Genre::HipHop:
            case Genre::LoFi:
            {
                static constexpr std::array<std::array<int, 4>, 4> kicks { { { 0, 7, 10, -1 }, { 0, 3, 10, -1 }, { 0, 8, 10, 15 }, { 0, 6, 9, -1 } } };
                for (const int s : kicks[static_cast<size_t> ((kickTemplate + b) % 4)])
                    if (s >= 0)
                        gr.step (kick, s, s == 0 ? 1.0f : 0.85f);
                const bool lofi = g == Genre::LoFi;
                gr.step (lofi && rimSyncopation ? rim : snare, 4, lofi ? 0.72f : 0.95f);
                gr.step (lofi && rimSyncopation ? rim : snare, 12, lofi ? 0.75f : 0.97f);
                for (int e = 0; e < 8; ++e)
                {
                    if (e == 7 && b % 2 == 1)
                        gr.step (openHat, 14, 0.55f, k8);
                    else
                        gr.step (closedHat, e * 2, (e % 2 == 0 ? 0.55f : 0.38f) * (lofi ? 0.75f : 1.0f));
                }
                if (lofi)
                    for (int s = 0; s < 16; ++s)
                        if (s % 2 == 1)
                            gr.step (extra51, s, 0.18f + 0.04f * static_cast<float> (s % 4 == 3)); // shaker (boom kit)
                if (! lofi && rimSyncopation)
                    gr.step (snare, 7, 0.3f); // a ghost note
                break;
            }

            case Genre::Trap:
            {
                static constexpr std::array<std::array<int, 4>, 4> kicks { { { 0, 7, 10, -1 }, { 0, 3, 6, 11 }, { 0, 10, 13, -1 }, { 0, 5, 11, 14 } } };
                for (const int s : kicks[static_cast<size_t> ((kickTemplate + b) % 4)])
                    if (s >= 0)
                        gr.step (kick, s, s == 0 ? 1.0f : 0.88f);
                gr.step (clap, 8, 0.95f); // half time: beat 3
                gr.step (snare, 8, 0.6f);
                // hats in eighths, with a roll here and there (16th triplets or 32nds)
                const int rollBeat = r.below (4);
                const bool triplets = r.chance (50);
                for (int beat = 0; beat < 4; ++beat)
                {
                    const int start = gr.at + beat * kBeat;
                    if (beat == rollBeat)
                    {
                        const int step = triplets ? kBeat / 6 : kBeat / 8;
                        for (int t = 0, i = 0; t < kBeat; t += step, ++i)
                            hit (notes, closedHat, start + t, 0.35f + 0.05f * static_cast<float> (i % 4), step);
                    }
                    else
                    {
                        hit (notes, closedHat, start, 0.6f);
                        hit (notes, closedHat, start + k8, 0.45f);
                    }
                }
                if (b % 2 == 1)
                    gr.step (openHat, 15, 0.5f, k8);
                break;
            }

            case Genre::DrumAndBass:
                gr.step (kick, 0, 1.0f);
                gr.step (kick, 10, 0.9f);
                if (kickTemplate >= 2)
                    gr.step (kick, 2 + (kickTemplate - 2), 0.6f);
                gr.step (snare, 4, 1.0f);
                gr.step (snare, 12, 1.0f);
                for (int e = 0; e < 8; ++e)
                    gr.step (closedHat, e * 2, e % 2 == 0 ? 0.5f : 0.35f);
                for (int beat = 0; beat < 4; ++beat)
                    gr.step (extra51, beat * 4 + 2, 0.3f); // ride (punch kit)
                if (rimSyncopation)
                    for (const int s : { 7, 9, 14 })
                        gr.step (snare, s, 0.25f + 0.05f * static_cast<float> (r.below (3)));
                if (fillHere)
                    for (int s = 12; s < 16; ++s)
                        gr.step (s % 2 == 0 ? midTom : highTom, s, 0.7f);
                break;

            case Genre::Pop:
            case Genre::Rock:
            case Genre::Synthwave:
            {
                gr.step (kick, 0, 1.0f);
                gr.step (kick, 8, 0.92f);
                if (kickTemplate == 1)
                    gr.step (kick, 10, 0.8f);
                if (kickTemplate == 2)
                    gr.step (kick, 6, 0.75f);
                gr.step (snare, 4, 0.95f);
                gr.step (snare, 12, 0.97f);
                if (g == Genre::Synthwave)
                    gr.step (clap, 4, 0.6f), gr.step (clap, 12, 0.6f);
                const bool sixteenths = g == Genre::Synthwave || (g == Genre::Pop && hatsSixteenths);
                for (int s = 0; s < 16; s += sixteenths ? 1 : 2)
                    if (! (fillHere && s >= 12))
                        gr.step (closedHat, s, s % 4 == 0 ? 0.6f : (s % 2 == 0 ? 0.45f : 0.3f));
                if (b == 0)
                    gr.step (crash, 0, 0.75f, kBeat);
                if (fillHere)
                    for (int s = 12; s < 16; ++s)
                        gr.step (s < 14 ? highTom : (s == 14 ? midTom : lowTom), s, 0.65f + 0.08f * static_cast<float> (s - 12));
                break;
            }

            case Genre::Reggaeton:
                for (int beat = 0; beat < 4; ++beat)
                    gr.step (kick, beat * 4, 0.95f);
                for (const int s : { 3, 6, 11, 14 }) // the dembow
                    gr.step (snare, s, s == 6 || s == 14 ? 0.9f : 0.75f);
                for (int e = 0; e < 8; ++e)
                    gr.step (closedHat, e * 2, e % 2 == 0 ? 0.4f : 0.5f);
                if (rimSyncopation)
                    for (const int s : { 0, 3, 6, 10, 12 }) // a 3-2 clave (boom kit's clave)
                        gr.step (extra50, s, 0.35f);
                if (kickTemplate >= 2)
                    gr.step (extra43, 7, 0.5f), gr.step (extra43, 15, 0.45f); // conga
                break;
        }
    }
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.startTick < b.startTick; });
    return notes;
}

// --- per-genre choices ------------------------------------------------------------------------------------------------------
struct Style
{
    const char* kit;
    std::array<const char*, 2> bassSounds; // one is chosen per variation
    const char* bassFallback;
    std::array<theory::BassStyle, 2> bassStyles;
    int bassLowest;
    std::array<const char*, 2> chordSounds;
    const char* chordFallback;
    theory::ChordRhythm chordRhythm;
    int chordOctave;
    std::array<const char*, 2> leadSounds;
    int leadDensity; // notes per bar, about
    int leadGrid;    // ticks
    std::vector<int> minorProgressions, majorProgressions;
};

const Style& styleFor (Genre g)
{
    using BS = theory::BassStyle;
    using CR = theory::ChordRhythm;
    static const std::array<Style, kGenreCount> styles { {
        { "kit:gen:punch", { "patch:FM Bass", "patch:Square Bass" }, "patch:Square Bass", { BS::Syncopated, BS::Pulse }, 33,
          { "patch:FM E-Piano", "program:grand-piano" }, "patch:FM E-Piano", CR::Stabs, 4, { "patch:Pluck", "patch:Glass Keys" }, 5, k16,
          { 8, 1, 10 }, { 0, 1, 6 } },
        { "kit:gen:modern", { "patch:Acid Bass", "patch:FM Bass" }, "patch:Acid Bass", { BS::Pulse, BS::Syncopated }, 33,
          { "patch:Soft Pad", "patch:PWM Strings" }, "patch:Soft Pad", CR::Held, 4, { "patch:FM Bell", "patch:Pluck" }, 3, k16,
          { 10, 9, 8 }, { 3, 0, 6 } },
        { "kit:gen:boom", { "program:electric-bass", "patch:FM Bass" }, "patch:FM Bass", { BS::Roots, BS::Syncopated }, 28,
          { "program:grand-piano", "patch:FM E-Piano" }, "patch:FM E-Piano", CR::Held, 4, { "patch:Vibraphone", "patch:Glass Keys" }, 4, k8,
          { 8, 10, 4 }, { 4, 5, 1 } },
        { "kit:gen:modern", { "patch:808 Sub", "patch:808 Sub" }, "patch:808 Sub", { BS::Roots, BS::Syncopated }, 24,
          { "patch:Morph Pad", "patch:Vowel Choir" }, "patch:Soft Pad", CR::Held, 4, { "patch:FM Bell", "patch:Pluck" }, 5, k16,
          { 8, 9, 10 }, { 1, 6, 0 } },
        { "kit:gen:punch", { "patch:FM Growl", "patch:Additive Bass" }, "patch:Square Bass", { BS::Syncopated, BS::Roots }, 28,
          { "patch:Morph Pad", "patch:Soft Pad" }, "patch:Soft Pad", CR::Held, 4, { "patch:Pluck", "patch:FM Bell" }, 4, k8,
          { 8, 9, 1 }, { 6, 1, 0 } },
        { "kit:lib:studio", { "program:electric-bass", "program:electric-bass-staccato" }, "patch:FM Bass", { BS::RootFifth, BS::Pulse }, 28,
          { "program:grand-piano", "patch:FM E-Piano" }, "patch:FM E-Piano", CR::Pulse, 4, { "patch:Pluck", "patch:Glass Keys" }, 5, k8,
          { 1, 8, 9 }, { 0, 1, 2, 6 } },
        { "kit:gen:boom", { "patch:Plucked Bass", "program:electric-bass" }, "patch:Plucked Bass", { BS::Roots, BS::RootFifth }, 28,
          { "patch:FM E-Piano", "program:grand-piano" }, "patch:FM E-Piano", CR::Held, 4, { "patch:Vibraphone", "patch:Glass Keys" }, 3, k8,
          { 11, 4 }, { 11, 4, 5 } },
        { "kit:gen:boom", { "patch:808 Sub", "patch:FM Bass" }, "patch:808 Sub", { BS::Pulse, BS::Roots }, 24,
          { "patch:Pluck", "patch:Soft Pad" }, "patch:Soft Pad", CR::Stabs, 4, { "patch:Pluck", "patch:FM Bell" }, 5, k16,
          { 8, 1, 9 }, { 1, 0, 2 } },
        { "kit:lib:rock", { "program:electric-bass-staccato", "program:electric-bass" }, "patch:Square Bass", { BS::Pulse, BS::RootFifth }, 28,
          { "patch:Steel Guitar", "patch:FM Drawbars" }, "patch:FM Drawbars", CR::Pulse, 3, { "patch:Saw Lead", "patch:Brass" }, 4, k8,
          { 9, 10, 8 }, { 3, 0, 2 } },
        { "kit:gen:punch", { "patch:Square Bass", "patch:FM Bass" }, "patch:Square Bass", { BS::Octaves, BS::Pulse }, 28,
          { "patch:PWM Strings", "patch:Soft Pad" }, "patch:Soft Pad", CR::Held, 4, { "patch:Saw Lead", "patch:Brass" }, 4, k8,
          { 8, 9, 1 }, { 6, 0, 1 } },
    } };
    return styles[static_cast<size_t> (g)];
}

// --- melody -----------------------------------------------------------------------------------------------------------------
const theory::ChordSpan* chordAt (const std::vector<theory::ChordSpan>& spans, int tick)
{
    for (const auto& s : spans)
        if (tick >= s.startTick && tick < s.startTick + s.lengthTicks)
            return &s;
    return spans.empty() ? nullptr : &spans.back();
}

bool inChord (const theory::ChordSpan& c, int pitch)
{
    const int pc = ((pitch % 12) + 12) % 12;
    return std::find (c.pitchClasses.begin(), c.pitchClasses.end(), pc) != c.pitchClasses.end();
}

// The chord tone nearest `near` within [lo, hi].
int nearestChordTone (const theory::ChordSpan& c, int near, int lo, int hi)
{
    int best = near, bestDistance = 1000;
    for (int p = lo; p <= hi; ++p)
        if (inChord (c, p) && std::abs (p - near) < bestDistance)
        {
            bestDistance = std::abs (p - near);
            best = p;
        }
    return best;
}

// A one-bar rhythm: onsets on the grid, the downbeat always, more on the beats than between them.
std::vector<int> rhythmMotif (Rng& r, int density, int grid)
{
    std::vector<int> onsets { 0 };
    const int slots = kBar / grid;
    for (int i = 1; i < slots && static_cast<int> (onsets.size()) < density + 2; ++i)
    {
        const int t = i * grid;
        const int weight = t % kBeat == 0 ? 70 : (t % k8 == 0 ? 45 : 30);
        if (r.chance (weight * density / 4))
            onsets.push_back (t);
    }
    if (onsets.size() < 2)
        onsets.push_back (2 * kBeat);
    return onsets;
}

std::vector<Note> melody (const theory::Scale& scale, const std::vector<theory::ChordSpan>& spans, const Style& style, int bars,
                          std::uint32_t seed)
{
    Rng r (mix (seed, 47));
    constexpr int lo = 67, hi = 86; // G4 .. D6
    const auto motifA = rhythmMotif (r, style.leadDensity, style.leadGrid);
    const auto motifB = rhythmMotif (r, style.leadDensity, style.leadGrid);
    std::vector<Note> out;
    int previous = scale.snap (74);
    for (int b = 0; b < bars; ++b)
    {
        // A A' A B: the last bar of every four answers; with fewer bars, the last one does
        const bool answer = (b % 4 == 3) || (bars < 4 && b == bars - 1 && bars > 1);
        const auto& rhythm = answer ? motifB : motifA;
        for (size_t i = 0; i < rhythm.size(); ++i)
        {
            const int start = b * kBar + rhythm[i];
            const int next = i + 1 < rhythm.size() ? b * kBar + rhythm[i + 1] : (b + 1) * kBar;
            const auto* chord = chordAt (spans, start);
            int pitch;
            if (chord != nullptr && (rhythm[i] % kBeat == 0 || r.chance (35)))
                pitch = nearestChordTone (*chord, previous + (r.below (5) - 2), lo, hi); // strong beat: a chord tone
            else
                pitch = scale.step (previous, r.below (5) - 2); // otherwise a step (or a repeat) in the scale
            if (pitch < lo)
                pitch = scale.step (pitch, 2);
            if (pitch > hi)
                pitch = scale.step (pitch, -2);
            // the very last note: the chord's root (or nearest chord tone), held
            const bool final = b == bars - 1 && i + 1 == rhythm.size();
            if (final && chord != nullptr)
                pitch = nearestChordTone (*chord, previous, lo, hi);
            const int gap = next - start;
            const int length = final ? std::max (gap - 20, k8) : std::max (40, std::min (gap - 20, r.chance (60) ? gap - 20 : k8));
            out.push_back ({ std::clamp (pitch, 0, 127), start, length, rhythm[i] % kBeat == 0 ? r.between (0.78f, 0.9f) : r.between (0.6f, 0.75f) });
            previous = pitch;
        }
    }
    return out;
}
} // namespace

const GenreInfo& genreInfo (Genre g)
{
    static const std::array<GenreInfo, kGenreCount> info { {
        { "House", 124.0, true, 0.0f },
        { "Techno", 130.0, true, 0.0f },
        { "Hip-hop (boom bap)", 90.0, true, 0.25f },
        { "Trap", 140.0, true, 0.0f },
        { "Drum & bass", 174.0, true, 0.0f },
        { "Pop", 110.0, false, 0.0f },
        { "Lo-fi", 78.0, false, 0.5f },
        { "Reggaeton", 95.0, true, 0.0f },
        { "Rock", 120.0, false, 0.0f },
        { "Synthwave", 100.0, true, 0.0f },
    } };
    return info[static_cast<size_t> (std::clamp (static_cast<int> (g), 0, kGenreCount - 1))];
}

Loop make (const Request& request)
{
    const auto genre = static_cast<Genre> (std::clamp (static_cast<int> (request.genre), 0, kGenreCount - 1));
    const auto& info = genreInfo (genre);
    const auto& style = styleFor (genre);
    const int bars = std::max (1, request.patternTicks / kBar);
    const int patternTicks = bars * kBar;
    const std::uint32_t seed = mix (request.variation, static_cast<std::uint32_t> (genre) + 1u);
    Rng r (seed);

    Loop loop;
    loop.genre = genre;
    loop.bpm = info.bpm;
    loop.patternTicks = patternTicks;
    loop.scale.root = ((request.key % 12) + 12) % 12;
    loop.scale.type = request.minor ? 2 : 1; // natural minor / major (theory::scales())

    const auto& candidates = request.minor ? style.minorProgressions : style.majorProgressions;
    const int progression = candidates[static_cast<size_t> (r.below (static_cast<int> (candidates.size())))];
    loop.progression = theory::progressions()[static_cast<size_t> (progression)].name;
    // four chords per pattern for the usual progressions (one a bar in four bars); eight-chord ones get half as long each
    const int chordCount = static_cast<int> (theory::progressions()[static_cast<size_t> (progression)].degrees.size());
    const int chordTicks = std::max (kBeat, patternTicks / std::max (4, chordCount));
    const auto spans = theory::progressionChords (loop.scale, progression, chordTicks, patternTicks);
    const int variant = r.below (2);

    if (request.drums)
    {
        Part p;
        p.role = Role::Drums;
        p.name = "Drums";
        p.sound = { style.kit, "kit:gen:punch" };
        p.volume = 0.85f;
        p.swing = info.swing;
        p.notes = drums (genre, bars, seed);
        loop.parts.push_back (std::move (p));
    }
    if (request.bass)
    {
        Part p;
        p.role = Role::Bass;
        p.name = "Bass";
        p.sound = { style.bassSounds[static_cast<size_t> (variant)], style.bassFallback };
        p.volume = 0.8f;
        p.swing = info.swing;
        p.notes = theory::basslineNotes (spans, style.bassStyles[static_cast<size_t> (r.below (2))], loop.scale, style.bassLowest);
        loop.parts.push_back (std::move (p));
    }
    if (request.chords)
    {
        Part p;
        p.role = Role::Chords;
        p.name = "Chords";
        p.sound = { style.chordSounds[static_cast<size_t> (variant)], style.chordFallback };
        p.volume = 0.6f;
        p.swing = info.swing;
        p.notes = theory::progressionNotes (loop.scale, progression, style.chordRhythm, chordTicks, patternTicks, style.chordOctave);
        loop.parts.push_back (std::move (p));
    }
    if (request.melody)
    {
        Part p;
        p.role = Role::Melody;
        p.name = "Melody";
        p.sound = { style.leadSounds[static_cast<size_t> (r.below (2))], "patch:Pluck" };
        p.volume = 0.5f;
        p.swing = info.swing;
        p.notes = melody (loop.scale, spans, style, bars, seed);
        loop.parts.push_back (std::move (p));
    }

    for (auto& part : loop.parts)
    {
        // everything inside the pattern, sorted
        part.notes.erase (std::remove_if (part.notes.begin(), part.notes.end(),
                                          [patternTicks] (const Note& n) { return n.startTick < 0 || n.startTick >= patternTicks; }),
                          part.notes.end());
        for (auto& n : part.notes)
            n.lengthTicks = std::clamp (n.lengthTicks, 1, patternTicks - n.startTick);
        std::stable_sort (part.notes.begin(), part.notes.end(), [] (const Note& a, const Note& b)
                          { return a.startTick != b.startTick ? a.startTick < b.startTick : a.pitch < b.pitch; });
    }
    return loop;
}
} // namespace sls::loopstarter
