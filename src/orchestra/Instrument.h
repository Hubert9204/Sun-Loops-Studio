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

    bool answers (int key, int velocity) const noexcept
    {
        return key >= loKey && key <= hiKey && velocity >= loVel && velocity <= hiVel && sample != nullptr && sample->frames() >= 4;
    }
    bool loops() const noexcept { return loop != LoopMode::Off && loopEnd > loopStart + 1 && sample != nullptr && loopEnd <= sample->frames(); }
};

// An instrument (M0: one articulation): its zones, the keys a real player can play, its amplitude envelope.
struct Instrument
{
    std::string id;          // "test-tone", "strings/violins-1"... (what a saved state refers to)
    std::string name;
    std::vector<Region> regions;
    int playableLo = 0, playableHi = 127; // outside the real range a key is silent
    float attackSeconds = 0.002f;
    float releaseSeconds = 0.25f;

    // The zones a note plays (several when zones overlap, e.g. a left and a right channel). At most `max` are written to `out`.
    int pick (int key, int velocity, const Region** out, int max) const noexcept
    {
        int n = 0;
        if (key < playableLo || key > playableHi)
            return 0;
        for (const auto& r : regions)
            if (n < max && r.answers (key, velocity))
                out[n++] = &r;
        return n;
    }

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
