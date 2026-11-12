#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/SampleData.h"

namespace sls
{
// The advanced sampler (0.16.0): how a sampler channel shapes its sound, and multi-sample programs (zones by key and velocity, as
// in a SoundFont). Plain C++, immutable once shared with the audio thread.

enum class LoopMode : std::uint8_t { Off, Forward, PingPong }; // 0.62.0: PingPong plays the loop forwards, then backwards
// What a sampler channel's LFO moves (0.62.0).
enum class SamplerLfoTarget : std::uint8_t { Off, Pitch, Cutoff, Pan, Volume };
enum class SamplerFilter : std::uint8_t { Off, LowPass, HighPass, BandPass };

// Settings of a sampler channel. With the envelope off and no loop a note plays its sample once to the end, whatever its length (the
// behaviour of every version before 0.16.0, and still the default). With the envelope on, or a loop, a note sounds while it is held
// and then fades with the release.
struct SamplerSettings
{
    bool envelope = false;
    float attack = 0.005f;  // seconds, 0..10
    float decay = 0.3f;     // seconds, 0..10
    float sustain = 1.0f;   // level, 0..1
    float release = 0.15f;  // seconds, 0..10

    LoopMode loop = LoopMode::Off;
    float loopStart = 0.0f; // fractions of the channel's sample (0..1); a program's zones have their own loop points
    float loopEnd = 1.0f;

    SamplerFilter filter = SamplerFilter::Off;
    float cutoff = 1.0f;    // 0..1, logarithmic from 20 Hz to 20 kHz (see cutoffHz)
    float resonance = 0.0f; // 0..1

    // 0.61.0, as FL Studio's sampler channel: play the sample backwards, start further into it (SMP START), fade it in and out
    // (the IN and OUT knobs). Start and the fades are fractions of the sample (0..1).
    // 0.62.0, as the loop options, envelopes and LFOs of FL Studio's sampler channel:
    float crossfade = 0.0f;                       // forward loops: the end of the loop fades into its start over this much of its length, 0..1
    SamplerLfoTarget lfoTarget = SamplerLfoTarget::Off;
    float lfoRate = 5.0f;                         // Hz, 0.05..20
    float lfoDepth = 0.5f;                        // 0..1: pitch up to 2 semitones, cutoff up to 3 octaves, pan and volume full
    float cutoffEnv = 0.0f;                       // octaves the envelope moves the cutoff (-5..5); needs the envelope and a filter
    float pitchEnv = 0.0f;                        // semitones (-24..24): a note starts this far from its pitch and settles on it
    float pitchEnvTime = 0.1f;                    // seconds, 0.005..5

    bool reverse = false;
    float start = 0.0f;     // 0..0.99
    float fadeIn = 0.0f;    // 0..1
    float fadeOut = 0.0f;   // 0..1

    static double cutoffHz (float c) noexcept { return 20.0 * std::pow (1000.0, std::clamp (static_cast<double> (c), 0.0, 1.0)); }

    // A note sounds as long as it is held (and its release) instead of to the end of the sample.
    bool sustains() const noexcept { return envelope || loop != LoopMode::Off; }

    SamplerSettings sanitised() const noexcept
    {
        SamplerSettings s = *this;
        s.attack = std::clamp (finiteOr (s.attack, 0.005f), 0.0f, 10.0f);
        s.decay = std::clamp (finiteOr (s.decay, 0.3f), 0.0f, 10.0f);
        s.sustain = std::clamp (finiteOr (s.sustain, 1.0f), 0.0f, 1.0f);
        s.release = std::clamp (finiteOr (s.release, 0.15f), 0.0f, 10.0f);
        s.loopStart = std::clamp (finiteOr (s.loopStart, 0.0f), 0.0f, 1.0f);
        s.loopEnd = std::clamp (finiteOr (s.loopEnd, 1.0f), 0.0f, 1.0f);
        if (s.loopEnd < s.loopStart)
            std::swap (s.loopStart, s.loopEnd);
        s.cutoff = std::clamp (finiteOr (s.cutoff, 1.0f), 0.0f, 1.0f);
        s.resonance = std::clamp (finiteOr (s.resonance, 0.0f), 0.0f, 1.0f);
        s.start = std::clamp (finiteOr (s.start, 0.0f), 0.0f, 0.99f);
        s.fadeIn = std::clamp (finiteOr (s.fadeIn, 0.0f), 0.0f, 1.0f);
        s.fadeOut = std::clamp (finiteOr (s.fadeOut, 0.0f), 0.0f, 1.0f);
        if (static_cast<int> (s.loop) > static_cast<int> (LoopMode::PingPong))
            s.loop = LoopMode::Off;
        s.crossfade = std::clamp (finiteOr (s.crossfade, 0.0f), 0.0f, 1.0f);
        if (static_cast<int> (s.lfoTarget) > static_cast<int> (SamplerLfoTarget::Volume))
            s.lfoTarget = SamplerLfoTarget::Off;
        s.lfoRate = std::clamp (finiteOr (s.lfoRate, 5.0f), 0.05f, 20.0f);
        s.lfoDepth = std::clamp (finiteOr (s.lfoDepth, 0.5f), 0.0f, 1.0f);
        s.cutoffEnv = std::clamp (finiteOr (s.cutoffEnv, 0.0f), -5.0f, 5.0f);
        s.pitchEnv = std::clamp (finiteOr (s.pitchEnv, 0.0f), -24.0f, 24.0f);
        s.pitchEnvTime = std::clamp (finiteOr (s.pitchEnvTime, 0.1f), 0.005f, 5.0f);
        if (static_cast<int> (s.filter) > static_cast<int> (SamplerFilter::BandPass))
            s.filter = SamplerFilter::Off;
        return s;
    }
};

// One sample of a multi-sample program, and the keys and velocities it answers to.
struct SampleZone
{
    std::shared_ptr<const SampleData> sample;
    int rootKey = 60;       // the key at which the sample plays at its own pitch
    float tune = 0.0f;      // semitones (fine and coarse tuning together)
    float keyTrack = 1.0f;  // how much the pitch follows the key: 1 = a semitone per key, 0 = the same pitch on every key
    int loKey = 0, hiKey = 127;
    int loVel = 1, hiVel = 127;
    float gain = 1.0f;      // 0..4
    float pan = 0.0f;       // -1..+1
    LoopMode loop = LoopMode::Off;
    int loopStart = 0, loopEnd = 0; // frames of `sample`

    bool answers (int key, int velocity) const noexcept
    {
        return key >= loKey && key <= hiKey && velocity >= loVel && velocity <= hiVel && sample != nullptr && sample->numFrames() >= 2;
    }
};

// A multi-sample instrument: for every note the zones that answer to its key and velocity play together (a SoundFont often has a left
// and a right zone for one stereo sample). `source` and `preset` say where it was loaded from, so a project can load it again.
// One slice of a Slicer program (0.37.0): its own pitch, level, pan and direction.
struct SliceSettings
{
    float tune = 0.0f;   // semitones, -24..+24
    float gain = 1.0f;   // 0..2
    float pan = 0.0f;    // -1..+1
    bool reverse = false;

    bool operator== (const SliceSettings&) const = default;

    SliceSettings sanitised() const noexcept
    {
        SliceSettings s = *this;
        s.tune = std::clamp (finiteOr (s.tune, 0.0f), -24.0f, 24.0f);
        s.gain = std::clamp (finiteOr (s.gain, 1.0f), 0.0f, 2.0f);
        s.pan = std::clamp (finiteOr (s.pan, 0.0f), -1.0f, 1.0f);
        return s;
    }
};

struct SamplerProgram
{
    std::string name;
    std::string source; // the .sf2 file (or empty: made in the program, e.g. from slices)
    int preset = -1;    // index of the preset in that file
    std::vector<int> sliceStarts; // a program made from slices of `source`: where they begin (frames) and the key of the first
    int sliceFirstKey = 60;
    std::vector<SliceSettings> sliceSettings; // per slice, in the order of sliceStarts (shorter: the rest are default) (0.37.0)
    bool sliceCut = false;  // a slice cuts the one still sounding on the channel (the Slicer's default, as in Slicex)
    double sliceBpm = 0.0;  // the loop's own tempo, for writing its notes; 0 = not known
    std::vector<SampleZone> zones; // empty: a placeholder for a program whose file is missing (it keeps the reference; the channel is silent)

    bool owns (const SampleData* s) const noexcept
    {
        for (const auto& z : zones)
            if (z.sample.get() == s)
                return true;
        return false;
    }

    // Every number in its range, loop points inside their sample, zones without audio removed.
    SamplerProgram sanitised() const
    {
        SamplerProgram p = *this;
        p.zones.erase (std::remove_if (p.zones.begin(), p.zones.end(), [] (const SampleZone& z) { return z.sample == nullptr || z.sample->numFrames() < 2; }),
                       p.zones.end());
        for (auto& z : p.zones)
        {
            z.rootKey = std::clamp (z.rootKey, 0, 127);
            z.tune = std::clamp (finiteOr (z.tune, 0.0f), -48.0f, 48.0f);
            z.keyTrack = std::clamp (finiteOr (z.keyTrack, 1.0f), 0.0f, 1.0f);
            z.loKey = std::clamp (z.loKey, 0, 127);
            z.hiKey = std::clamp (z.hiKey, z.loKey, 127);
            z.loVel = std::clamp (z.loVel, 1, 127);
            z.hiVel = std::clamp (z.hiVel, z.loVel, 127);
            z.gain = std::clamp (finiteOr (z.gain, 1.0f), 0.0f, 4.0f);
            z.pan = std::clamp (finiteOr (z.pan, 0.0f), -1.0f, 1.0f);
            const int frames = z.sample->numFrames();
            z.loopStart = std::clamp (z.loopStart, 0, frames - 1);
            z.loopEnd = std::clamp (z.loopEnd, 0, frames);
            if (z.loopEnd - z.loopStart < 2)
                z.loop = LoopMode::Off;
        }
        return p;
    }
};

// A program from slices of one sample (the sample editor's markers): slice n plays on key `firstKey` + n at its own pitch, one key each.
// `starts` are the frames where slices begin (sorted, the first should be 0); each slice ends where the next begins. Slices shorter than
// two frames are left out; at most 128 - firstKey slices.
// `settings` (0.37.0) are for the slices in order (after `starts` is sorted, with 0 added and repeats removed). Slice i plays on
// key firstKey + i; slices past key 127 are left out.
inline SamplerProgram makeSliceProgram (const std::shared_ptr<const SampleData>& whole, std::vector<int> starts, int firstKey = 60, std::string name = "Slices",
                                        const std::vector<SliceSettings>& settings = {})
{
    SamplerProgram p;
    p.name = std::move (name);
    p.source = whole != nullptr ? whole->source : std::string();
    if (whole == nullptr || whole->numFrames() < 2)
        return p;
    const int frames = whole->numFrames();
    firstKey = std::clamp (firstKey, 0, 127);
    starts.erase (std::remove_if (starts.begin(), starts.end(), [frames] (int f) { return f < 0 || f >= frames; }), starts.end());
    starts.push_back (0);
    std::sort (starts.begin(), starts.end());
    starts.erase (std::unique (starts.begin(), starts.end()), starts.end());
    p.sliceStarts = starts;
    p.sliceFirstKey = firstKey;
    for (const auto& s : settings)
        p.sliceSettings.push_back (s.sanitised());
    if (p.sliceSettings.size() > starts.size())
        p.sliceSettings.resize (starts.size());
    while (! p.sliceSettings.empty() && p.sliceSettings.back() == SliceSettings {})
        p.sliceSettings.pop_back(); // only what differs is kept (and saved)
    for (size_t i = 0; i < starts.size() && firstKey + static_cast<int> (i) <= 127; ++i)
    {
        const int a = starts[i];
        const int b = i + 1 < starts.size() ? starts[i + 1] : frames;
        if (b - a < 2)
            continue;
        auto part = std::make_shared<SampleData>();
        part->left.assign (whole->left.begin() + a, whole->left.begin() + b);
        part->right.assign (whole->right.begin() + a, whole->right.begin() + b);
        part->sampleRate = whole->sampleRate;
        const SliceSettings s = i < p.sliceSettings.size() ? p.sliceSettings[i] : SliceSettings {};
        if (s.reverse)
        {
            std::reverse (part->left.begin(), part->left.end());
            std::reverse (part->right.begin(), part->right.end());
        }
        part->name = whole->name + " " + std::to_string (i + 1);
        part->source = whole->source;
        SampleZone z;
        z.sample = std::move (part);
        z.rootKey = z.loKey = z.hiKey = firstKey + static_cast<int> (i);
        z.tune = s.tune;
        z.gain = s.gain;
        z.pan = s.pan;
        p.zones.push_back (std::move (z));
    }
    return p;
}
} // namespace sls
