#pragma once

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// SunSymphony (0.66.0, Milestone 0), the orchestral sampler: what an instrument is. Plain C++ (no JUCE), immutable once handed to
// the audio thread, so the engine can be tested headless. See claude/orchestral-sampler-plan.md for where this is going.
namespace orchestra
{
// The audio of one sample, decoded to float. Shared (read-only) by every zone and voice that plays it.
struct SampleBuffer
{
    std::vector<float> left, right; // right is empty for a mono sample
    double sampleRate = 48000.0;
    std::string source;             // where it was read from (for messages)

    int frames() const noexcept { return static_cast<int> (left.size()); }
    bool stereo() const noexcept { return ! right.empty(); }
};

// How a zone loops (Milestone 2 adds the rest; M0 plays Off and Forward).
enum class LoopMode
{
    Off,      // one-shot: the sample plays to its end (or until the release has faded it)
    Forward,  // loopStart..loopEnd repeats while the note sounds
    Bounce,   // forwards, then backwards
    Sustain   // repeats while the key is held, then plays on to the end
};

// One zone: a sample and the keys and velocities it answers to.
struct Region
{
    std::shared_ptr<const SampleBuffer> sample;
    int loKey = 0, hiKey = 127, rootKey = 60;
    int loVel = 1, hiVel = 127;
    float tuneCents = 0.0f;  // fine and coarse tuning together
    float gainDb = 0.0f;
    float pan = 0.0f;        // -1..+1
    LoopMode loop = LoopMode::Off;
    int loopStart = 0, loopEnd = 0; // frames; loopEnd > loopStart when the region loops
    int seqLength = 1, seqPosition = 1; // round robin: plays on the notes whose count (modulo seqLength) is seqPosition - 1
    float keytrack = 100.0f;  // cents per key away from rootKey (0: unpitched, every key plays the sample as recorded)
    bool oneShot = false;     // the key's release does not stop it: it plays to its end (percussion)
    float releaseSeconds = -1.0f; // its own release time (-1: the instrument's)
    int articulation = -1;        // 0.84.0: the articulation it belongs to (-1: it plays in every one)

    bool answers (int key, int velocity, unsigned roundRobin = 0) const noexcept
    {
        return key >= loKey && key <= hiKey && velocity >= loVel && velocity <= hiVel && sample != nullptr && sample->frames() >= 4
               && (seqLength <= 1 || static_cast<int> (roundRobin % static_cast<unsigned> (seqLength)) == seqPosition - 1);
    }
    bool loops() const noexcept { return loop != LoopMode::Off && loopEnd > loopStart + 1 && sample != nullptr && loopEnd <= sample->frames(); }
};

// 0.84.0: one articulation of an instrument (sustain, pizzicato, staccato...) and the key that chooses it (a keyswitch, as in
// orchestral libraries: a key outside the instrument's range that sounds nothing and makes the next notes use this articulation).
struct Articulation
{
    std::string name;
    int keyswitch = -1;
};

// An instrument: its zones, the keys a real player can play, its amplitude envelope; 0.84.0: its articulations, if it has several.
struct Instrument
{
    std::string id;          // "test-tone", "strings/violins-1"... (what a saved state refers to)
    std::string name;
    std::vector<Region> regions;
    int playableLo = 0, playableHi = 127; // outside the real range a key is silent
    float attackSeconds = 0.002f;
    float releaseSeconds = 0.25f;
    std::vector<Articulation> articulations; // 0.84.0: empty when it has one (every zone plays)
    int defaultArticulation = 0;             // the one that plays before a keyswitch is pressed

    // The zones a note plays (several when zones overlap, e.g. a left and a right channel). At most `max` are written to `out`.
    // `roundRobin` counts the notes played (the engine keeps one count per part); zones in a round-robin sequence take turns.
    // `articulation`: the one chosen (zones of the others are left out).
    int pick (int key, int velocity, const Region** out, int max, unsigned roundRobin = 0, int articulation = -1) const noexcept
    {
        int n = 0;
        if (key < playableLo || key > playableHi)
            return 0;
        for (const auto& r : regions)
            if (n < max && r.answers (key, velocity, roundRobin) && (r.articulation < 0 || articulation < 0 || r.articulation == articulation))
                out[n++] = &r;
        return n;
    }

    // The articulation `key` switches to, or -1 when it is not a keyswitch.
    int keyswitchArticulation (int key) const noexcept
    {
        for (size_t a = 0; a < articulations.size(); ++a)
            if (articulations[a].keyswitch == key)
                return static_cast<int> (a);
        return -1;
    }

    // 0.84.0: one instrument made of several (each an articulation, e.g. the SFZ files of a library instrument), chosen by keyswitches
    // on the keys just below the lowest key any of them plays (at most C1, MIDI 24, as most orchestral libraries have them). Each
    // keeps its own release. nullptr when none is given.
    static std::shared_ptr<const Instrument> combine (const std::string& id, const std::string& name,
                                                      const std::vector<std::pair<std::string, std::shared_ptr<const Instrument>>>& parts);

    // A built-in instrument that needs no library: a soft organ-like tone (a few harmonics), one looped sample per octave. It lets
    // the plugin make a sound before the orchestral library is installed, and the tests check the engine against a known pitch.
    static std::shared_ptr<const Instrument> makeTestTone (double sampleRate = 48000.0);

    // Reads an SFZ file's zones (the subset sls::sfz understands: keys, velocities, tuning, volume, pan, loops). `loadSample` turns a
    // sample path, as written in the file, into audio (nullptr: the zone is left out). Returns nullptr when nothing could be played.
    static std::shared_ptr<const Instrument> fromSfz (const std::string& sfzText, const std::string& id, const std::string& name,
                                                      const std::function<std::shared_ptr<const SampleBuffer> (const std::string&)>& loadSample,
                                                      std::string& error);
};

inline double midiToHz (double note) noexcept { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }
} // namespace orchestra
