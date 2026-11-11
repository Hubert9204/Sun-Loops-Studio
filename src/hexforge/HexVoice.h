#pragma once

#include "hexforge/HexParams.h"
#include "hexforge/HexWaves.h"
#include "sunburst/SynthDsp.h"

namespace hexforge
{
using sunburst::Envelope;
using sunburst::Random;

// The analog-modelled filter: a four-pole transistor ladder in the manner of Moog's, its fourth stage fed back to the input
// through a tanh, so high resonance rings and sings and a hot input saturates. Low-pass 24 / 12, high-pass 24 / 12 (the ladder's
// binomial mix of its stages) and band-pass.
class Ladder
{
public:
    void reset() noexcept
    {
        s.fill (0.0f);
        last = 0.0f;
    }
    void set (LadderMode m, double cutoff, double sampleRate, float resonance, float driveAmount) noexcept
    {
        mode = m;
        const double fc = std::clamp (cutoff, 10.0, sampleRate * 0.45);
        const auto g = static_cast<float> (std::tan (sunburst::kPi * fc / sampleRate));
        G = g / (1.0f + g);
        k = 4.1f * std::clamp (resonance, 0.0f, 1.0f);
        drive = 1.0f + driveAmount * driveAmount * 12.0f;
        makeup = (1.0f + 0.5f * k) / std::sqrt (drive);
    }
    float process (float x) noexcept
    {
        const float in = std::tanh ((x - k * last) * drive * 0.7f);
        float u = in;
        std::array<float, 4> y {};
        for (size_t i = 0; i < 4; ++i)
        {
            const float v = (u - s[i]) * G;
            y[i] = v + s[i];
            s[i] = y[i] + v;
            u = y[i];
        }
        last = y[3];
        switch (mode)
        {
            case LadderMode::LP24: return y[3] * makeup * 1.4f;
            case LadderMode::LP12: return y[1] * makeup * 1.4f;
            case LadderMode::HP24: return (in - 4.0f * y[0] + 6.0f * y[1] - 4.0f * y[2] + y[3]) * makeup * 1.4f;
            case LadderMode::HP12: return (in - 2.0f * y[0] + y[1]) * makeup * 1.4f;
            case LadderMode::BP: return (y[1] - y[3]) * makeup * 2.5f;
        }
        return y[3];
    }

private:
    LadderMode mode = LadderMode::LP24;
    std::array<float, 4> s {};
    float last = 0.0f, G = 0.1f, k = 0.0f, drive = 1.0f, makeup = 1.0f;
};

struct OpPatch
{
    int wave = 0;
    double ratio = 1.0;
    float fine = 0.0f, level = 1.0f, out = 0.0f, pan = 0.0f, velocity = 0.3f;
    std::array<float, 4> env { 0.002f, 0.5f, 1.0f, 0.3f };
};

struct SlotPatch
{
    Source source = Source::None;
    Dest dest = Dest::None;
    float amount = 0.0f;
};

struct Patch
{
    std::array<OpPatch, kNumOps> op {};
    std::array<std::array<float, kNumOps>, kNumOps> fm {}; // fm[target][source]
    float fmDepth = 1.0f;
    int unison = 1, octave = 0;
    float detune = 0.2f, spread = 0.7f, dropAmount = 0.0f, dropTime = 0.08f;
    bool filterOn = true;
    LadderMode mode = LadderMode::LP24;
    float cutoff = 6000.0f, resonance = 0.2f, drive = 0.2f, filterEnvAmount = 0.0f, filterKey = 0.5f, filterVelocity = 0.0f, filterLfoAmount = 0.0f;
    std::array<float, 4> filterEnv {}, ampEnv {};
    float ampVelocity = 0.4f, glide = 0.0f, bendRange = 2.0f;
    std::array<SlotPatch, kMatrixSlots> matrix {};
    int matrixCount = 0;
    std::array<const float*, 2> spectral {}; // Spectral A and B: level 0 of their current tables
};

struct Controls
{
    double rate = 96000.0; // the voices run at twice the output rate
    float modWheel = 0.0f, aftertouch = 0.0f, bend = 0.0f, filterLfo = 0.0f, lfo2 = 0.0f;
    std::array<float, kNumMacros> macro {};
};

class Voice
{
public:
    void prepare (double oversampledRate) noexcept;
    void start (int note, float velocity, double fromNote, bool legato, std::uint32_t seed, const Patch& patch) noexcept;
    void release() noexcept;
    void kill() noexcept;
    // Adds `numSamples` samples at the oversampled rate.
    void render (const Patch& patch, const Controls& controls, float* left, float* right, int numSamples) noexcept;

    bool isActive() const noexcept { return amp.isActive(); }
    bool isReleasing() const noexcept { return amp.isReleasing(); }
    float level() const noexcept { return amp.current(); }

    int note = -1;
    float velocity = 0.0f;
    std::uint64_t age = 0;
    bool keyDown = false;

private:
    void renderBlock (const Patch& patch, const Controls& controls, float* left, float* right, int n) noexcept;

    double rate = 96000.0;
    double targetNote = 60.0, currentNote = 60.0, drop = 0.0;
    Envelope amp, filter;
    std::array<Envelope, kNumOps> opEnv {};
    std::array<std::array<double, kNumOps>, kMaxUnison> phase {};
    std::array<std::array<float, kNumOps>, kMaxUnison> previous {};
    Ladder ladderL, ladderR;
    Random random;
    float randomValue = 0.0f;
};
} // namespace hexforge
