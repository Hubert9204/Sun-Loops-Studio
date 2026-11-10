#pragma once

#include <algorithm>
#include <iterator>

namespace sls::typing
{
// The computer keyboard as a MIDI keyboard (0.32.0), laid out like the typing keyboards of other music programs: the home row is
// the white keys and the row above the black keys, A plays C5 (MIDI 60, in FL Studio's names where middle C is C5).
//
//    W E   T Y U   O P            black keys: C#  D#    F#  G#  A#    C#  D#
//   A S D F G H J K L ; '         white keys: C  D  E  F  G  A  B  C  D  E  F
//
// Z and X move it an octave down and up, C and V the velocity. Keys of other layouts that sit where ; and ' are (the Italian ò and
// à, the German ö and ä, the Spanish ñ) count as ; and '. Plain C++, so it is tested headless.

constexpr int kBasePitch = 60; // A
constexpr int kMinOctave = -5, kMaxOctave = 4;

// The semitone above A's note that a character plays, -1 for a character that is not a note key.
inline int semitoneFor (char32_t c) noexcept
{
    if (c >= 'A' && c <= 'Z')
        c = c - 'A' + 'a';
    switch (c)
    {
        case 'a': return 0;
        case 'w': return 1;
        case 's': return 2;
        case 'e': return 3;
        case 'd': return 4;
        case 'f': return 5;
        case 't': return 6;
        case 'g': return 7;
        case 'y': return 8;
        case 'h': return 9;
        case 'u': return 10;
        case 'j': return 11;
        case 'k': return 12;
        case 'o': return 13;
        case 'l': return 14;
        case 'p': return 15;
        case ';': case U'ò': case U'Ò': case U'ö': case U'Ö': case U'ñ': case U'Ñ': return 16; // ò ö ñ
        case '\'': case U'à': case U'À': case U'ä': case U'Ä': return 17;                              // à ä
        default: return -1;
    }
}

// The MIDI note a character plays with the keyboard moved `octave` octaves, -1 for none (not a note key, or off the MIDI range).
inline int pitchFor (char32_t c, int octave) noexcept
{
    const int s = semitoneFor (c);
    if (s < 0)
        return -1;
    const int pitch = kBasePitch + 12 * std::clamp (octave, kMinOctave, kMaxOctave) + s;
    return pitch >= 0 && pitch <= 127 ? pitch : -1;
}

enum class Command
{
    None,
    OctaveDown,
    OctaveUp,
    VelocityDown,
    VelocityUp
};

inline Command commandFor (char32_t c) noexcept
{
    if (c >= 'A' && c <= 'Z')
        c = c - 'A' + 'a';
    switch (c)
    {
        case 'z': return Command::OctaveDown;
        case 'x': return Command::OctaveUp;
        case 'c': return Command::VelocityDown;
        case 'v': return Command::VelocityUp;
        default: return Command::None;
    }
}

// Velocity steps for C and V (MIDI values).
inline int nextVelocity (int velocity, bool up) noexcept
{
    static constexpr int steps[] = { 1, 20, 40, 60, 80, 100, 110, 127 };
    if (up)
    {
        for (const int s : steps)
            if (s > velocity)
                return s;
        return 127;
    }
    for (int i = static_cast<int> (std::size (steps)) - 1; i >= 0; --i)
        if (steps[i] < velocity)
            return steps[i];
    return 1;
}
} // namespace sls::typing
