#include "audio/BuiltinSamples.h"

#include <algorithm>
#include <cmath>

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

std::shared_ptr<const SampleData> finish (std::vector<float> mono, const char* name)
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
    return finish (std::move (s), "Kick");
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
    return finish (std::move (s), "Snare");
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
    return finish (std::move (s), "Hat");
}
} // namespace sls
