#include "audio/BuiltinSamples.h"

#include "audio/DrumSynth.h"
#include "audio/SoundLibrary.h"

#include <algorithm>
#include <cmath>

#include <juce_audio_formats/juce_audio_formats.h>

#include "SunLoopsSoundData.h"

namespace sls
{
namespace
{
constexpr double kSr = 44100.0;
constexpr double kTwoPi = 6.283185307179586476925286766559;

// Deterministic white noise in [-1, 1].
struct Noise
{
    unsigned state = 0x1234abcdu;
    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float> (static_cast<int> (state >> 8) - (1 << 23)) / static_cast<float> (1 << 23);
    }
};

std::shared_ptr<const SampleData> finish (std::vector<float> mono, const char* name, const char* id)
{
    float peak = 0.0f;
    for (float s : mono)
        peak = std::max (peak, std::fabs (s));
    const float scale = peak > 0.0f ? 0.9f / peak : 1.0f;
    for (auto& s : mono)
        s *= scale;

    auto data = std::make_shared<SampleData>();
    data->left = mono;
    data->right = std::move (mono);
    data->sampleRate = kSr;
    data->name = name;
    data->source = std::string ("builtin:") + id;
    return data;
}
} // namespace

std::shared_ptr<const SampleData> makeKickSample()
{
    const int n = static_cast<int> (0.40 * kSr);
    std::vector<float> s (static_cast<size_t> (n));
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        const double freq = 48.0 + 120.0 * std::exp (-t * 28.0); // pitch drops from ~170 Hz to 48 Hz
        phase += kTwoPi * freq / kSr;
        s[static_cast<size_t> (i)] = static_cast<float> (std::sin (phase) * std::exp (-t * 7.5));
    }
    return finish (std::move (s), "Kick", "kick");
}

std::shared_ptr<const SampleData> makeSnareSample()
{
    const int n = static_cast<int> (0.25 * kSr);
    std::vector<float> s (static_cast<size_t> (n));
    Noise noise;
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        phase += kTwoPi * 190.0 / kSr;
        const double body = std::sin (phase) * std::exp (-t * 28.0) * 0.6;
        const double hiss = noise.next() * std::exp (-t * 18.0);
        s[static_cast<size_t> (i)] = static_cast<float> (body + hiss);
    }
    return finish (std::move (s), "Snare", "snare");
}

std::shared_ptr<const SampleData> makeHatSample()
{
    const int n = static_cast<int> (0.10 * kSr);
    std::vector<float> s (static_cast<size_t> (n));
    Noise noise;
    float previous = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        const float white = noise.next();
        const float highPassed = white - previous; // crude high-pass
        previous = white;
        s[static_cast<size_t> (i)] = static_cast<float> (highPassed * std::exp (-t * 55.0));
    }
    return finish (std::move (s), "Hat", "hat");
}

const std::vector<std::string>& builtinSampleIds()
{
    static const std::vector<std::string> ids { "kick", "snare", "hat" };
    return ids;
}

std::shared_ptr<const SampleData> makeBuiltinSample (const std::string& id)
{
    if (id == "kick")  return makeKickSample();
    if (id == "snare") return makeSnareSample();
    if (id == "hat")   return makeHatSample();
    if (isLibraryId (id)) return makeLibrarySample (id);
    if (isGeneratedId (id)) return makeGeneratedSample (id);
    if (id == kStartupSampleId) return makeStartupSample();
    return nullptr;
}

std::string removedBuiltinMessage (const std::string& id)
{
    if (id.rfind ("cyber:", 0) == 0)
        return "The Cyber-Hub sounds were removed in version 0.22.0: \"" + id.substr (6) + "\" is silent (load another sound).";
    return {};
}

std::shared_ptr<const SampleData> makeStartupSample()
{
    int size = 0;
    const char* data = SunLoopsSoundData::getNamedResource ("intro_wav", size);
    if (data == nullptr || size <= 0)
        return nullptr;
    std::unique_ptr<juce::AudioFormatReader> reader (
        juce::WavAudioFormat().createReaderFor (new juce::MemoryInputStream (data, static_cast<size_t> (size), false), true));
    if (reader == nullptr || reader->numChannels < 1 || reader->lengthInSamples < 2)
        return nullptr;
    const int frames = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> buffer (2, frames);
    buffer.clear();
    if (! reader->read (&buffer, 0, frames, 0, true, true))
        return nullptr;
    if (reader->numChannels == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, frames);
    auto sample = std::make_shared<SampleData>();
    sample->left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames);
    sample->right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + frames);
    sample->sampleRate = reader->sampleRate;
    sample->name = "Sun Loops intro";
    sample->source = std::string ("builtin:") + kStartupSampleId;
    return sample;
}
} // namespace sls
