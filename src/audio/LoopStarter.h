#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "audio/MusicTheory.h"
#include "audio/NoteSequence.h"

namespace sls::loopstarter
{
// The Loop Starter (0.30.0, in the manner of FL Studio's): a starting point for a track in a genre - drums, a bassline, chords and
// a melody that fit together, in a key, at a tempo that suits the genre - made by rules and chance, not by a trained model (the
// user chose offline features without AI). The same genre, key and variation always give the same loop; another variation gives
// another drum groove, progression, bassline and melody. Plain C++, so it can be tested without a window; the app turns the
// parts into channels with the built-in sounds.

enum class Genre
{
    House,
    Techno,
    HipHop,
    Trap,
    DrumAndBass,
    Pop,
    LoFi,
    Reggaeton,
    Rock,
    Synthwave
};
constexpr int kGenreCount = 10;

struct GenreInfo
{
    const char* name;
    double bpm;      // the tempo the loop is made for
    bool minor;      // the usual mode (the user can choose the other)
    float swing;     // for the drums (0..1, as a channel's swing)
};
const GenreInfo& genreInfo (Genre g);

enum class Role
{
    Drums,
    Bass,
    Chords,
    Melody
};

// A sound for a part, in the app's terms: "kit:<builtin kit id>" (a drum machine kit, e.g. "kit:gen:punch"), "program:<library
// program>" (e.g. "program:grand-piano"), or "patch:<factory synth patch name>" (e.g. "patch:Pluck"). `fallback` is a synth patch
// for when the first is not available (a build without the sound library).
struct Sound
{
    std::string id;
    std::string fallback;
};

struct Part
{
    Role role = Role::Drums;
    std::string name;  // "Drums", "Bass", "Chords", "Melody"
    Sound sound;
    float volume = 0.8f;
    float swing = 0.0f;
    std::vector<Note> notes; // in the pattern, sorted by start
};

struct Loop
{
    Genre genre = Genre::House;
    double bpm = 120.0;
    theory::Scale scale;
    std::string progression; // its name, for the status line
    std::vector<Part> parts;  // drums, bass, chords, melody (those asked for)
    int patternTicks = 2 * 4 * kTicksPerBeat; // its length: the request's, in whole bars (0.56.0: the new pattern gets it)
};

struct Request
{
    Genre genre = Genre::House;
    int key = 9;          // pitch class of the root (9 = A)
    bool minor = true;
    std::uint32_t variation = 1;
    int patternTicks = 2 * 4 * kTicksPerBeat; // the pattern's length (whole bars)
    bool drums = true, bass = true, chords = true, melody = true;
};

Loop make (const Request& request);

// General MIDI drum keys of the built-in kits (pad 1 = key 36).
namespace drum
{
constexpr int kick = 36, rim = 37, snare = 38, clap = 39, snare2 = 40, lowTom = 41, closedHat = 42, extra43 = 43, pedalHat = 44,
              midTom = 45, openHat = 46, extra47 = 47, highTom = 48, crash = 49, extra50 = 50, extra51 = 51;
}
} // namespace sls::loopstarter
