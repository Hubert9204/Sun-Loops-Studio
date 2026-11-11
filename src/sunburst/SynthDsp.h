#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "sunburst/SunBurstParams.h"

// Small building blocks of SunBurst's sound: envelopes, LFOs, filters, a random generator. No allocation, audio-thread safe.
namespace sunburst
{
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 6.283185307179586;

inline double noteToHz (double note) noexcept { return 440.0 * std::exp2 ((note - 69.0) / 12.0); }

// xorshift: fast, good enough for noise and random modulation.
struct Random
{
    std::uint32_t state = 0x9e3779b9u;
    void seed (std::uint32_t s) noexcept { state = s != 0 ? s : 0x9e3779b9u; }
    std::uint32_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float bipolar() noexcept { return static_cast<float> (next() >> 8) * (2.0f / 16777216.0f) - 1.0f; } // -1..1
    float unipolar() noexcept { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }      // 0..1
};

// PolyBLEP correction for a step at phase 0 (`t` phase 0..1, `dt` increment).
inline float polyBlep (double t, double dt) noexcept
{
    if (t < dt)
    {
        const double x = t / dt;
        return static_cast<float> (x + x - x * x - 1.0);
    }
    if (t > 1.0 - dt)
    {
        const double x = (t - 1.0) / dt;
        return static_cast<float> (x * x + x + x + 1.0);
    }
    return 0.0f;
}

// ADSR: linear attack, exponential decay and release. Triggering a sounding envelope attacks from where it is (no click).
class Envelope
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void setSampleRate (double sr) noexcept { rate = sr; }
    void set (float attack, float decay, float sustain, float release) noexcept
    {
        attackStep = 1.0f / std::max (1.0f, attack * static_cast<float> (rate));
        decayCoef = coefFor (decay);
        sustainLevel = std::clamp (sustain, 0.0f, 1.0f);
        releaseCoef = coefFor (release);
    }
    void trigger() noexcept { stage = Stage::Attack; }
    void release() noexcept
    {
        if (stage != Stage::Idle)
            stage = Stage::Release;
    }
    void reset() noexcept
    {
        stage = Stage::Idle;
        value = 0.0f;
    }
    bool isActive() const noexcept { return stage != Stage::Idle; }
    bool isReleasing() const noexcept { return stage == Stage::Release; }
    float current() const noexcept { return value; }

    float next() noexcept
    {
        switch (stage)
        {
            case Stage::Attack:
                value += attackStep;
                if (value >= 1.0f)
                {
                    value = 1.0f;
                    stage = Stage::Decay;
                }
                break;
            case Stage::Decay:
                value = sustainLevel + (value - sustainLevel) * decayCoef;
                if (value - sustainLevel < 1.0e-4f)
                {
                    value = sustainLevel;
                    stage = Stage::Sustain;
                }
                break;
            case Stage::Sustain:
                value = sustainLevel;
                break;
            case Stage::Release:
                value *= releaseCoef;
                if (value < 1.0e-5f)
                {
                    value = 0.0f;
                    stage = Stage::Idle;
                }
                break;
            case Stage::Idle:
                break;
        }
        return value;
    }

private:
    float coefFor (float seconds) const noexcept
    {
        const double samples = std::max (1.0, static_cast<double> (seconds) * rate / 6.9); // -60 dB after `seconds`
        return static_cast<float> (std::exp (-1.0 / samples));
    }

    double rate = 48000.0;
    Stage stage = Stage::Idle;
    float value = 0.0f, attackStep = 0.01f, decayCoef = 0.999f, sustainLevel = 1.0f, releaseCoef = 0.999f;
};

// A low-frequency oscillator. `advance` moves it by `seconds` and returns its value (-1..1, or 0..1 when unipolar).
class Lfo
{
public:
    void reset (float startPhase, std::uint32_t seed) noexcept
    {
        phase = startPhase;
        random.seed (seed);
        held = random.bipolar();
        nextHeld = random.bipolar();
    }
    void setPhase (double p) noexcept
    {
        if (p < phase - 0.5) // wrapped: new random values
            newCycle();
        phase = p;
    }

    float advance (double hz, double seconds, LfoShape shape) noexcept
    {
        phase += hz * seconds;
        while (phase >= 1.0)
        {
            phase -= 1.0;
            newCycle();
        }
        return value (shape);
    }

    float value (LfoShape shape) const noexcept
    {
        const auto p = static_cast<float> (phase);
        switch (shape)
        {
            case LfoShape::Sine: return std::sin (static_cast<float> (kTwoPi) * p);
            case LfoShape::Triangle: return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
            case LfoShape::SawUp: return 2.0f * p - 1.0f;
            case LfoShape::SawDown: return 1.0f - 2.0f * p;
            case LfoShape::Square: return p < 0.5f ? 1.0f : -1.0f;
            case LfoShape::SampleHold: return held;
            case LfoShape::SmoothRandom:
            {
                const float s = 0.5f - 0.5f * std::cos (static_cast<float> (kPi) * p);
                return held + s * (nextHeld - held);
            }
        }
        return 0.0f;
    }

private:
    void newCycle() noexcept
    {
        held = nextHeld;
        nextHeld = random.bipolar();
    }

    double phase = 0.0;
    Random random;
    float held = 0.0f, nextHeld = 0.0f;
};

// Zero-delay-feedback state-variable filter (Zavalishin's TPT form): stable under fast modulation.
class Svf
{
public:
    void reset() noexcept { s1 = s2 = 0.0f; }
    void set (double cutoffHz, double sampleRate, float damping) noexcept
    {
        const double fc = std::clamp (cutoffHz, 10.0, sampleRate * 0.49);
        g = static_cast<float> (std::tan (kPi * fc / sampleRate));
        k = damping;
        a1 = 1.0f / (1.0f + g * (g + k));
    }
    struct Out { float lp, bp, hp; };
    Out process (float x) noexcept
    {
        const float hp = (x - (k + g) * s1 - s2) * a1;
        const float bp = g * hp + s1;
        const float lp = g * bp + s2;
        s1 = 2.0f * bp - s1;
        s2 = 2.0f * lp - s2;
        return { lp, bp, hp };
    }

private:
    float g = 0.1f, k = 1.4f, a1 = 1.0f, s1 = 0.0f, s2 = 0.0f;
};

// The voice filter: 12 or 24 dB/oct, eight types, with drive. Resonance 0..1 (near self-oscillation at 1).
class MultiFilter
{
public:
    void reset() noexcept
    {
        a.reset();
        b.reset();
    }
    void set (FilterType t, double cutoff, double sampleRate, float resonance, float driveAmount) noexcept
    {
        type = t;
        const float res = std::clamp (resonance, 0.0f, 1.0f);
        const float k = 2.0f - 1.96f * res;
        const bool steep = t == FilterType::LP24 || t == FilterType::HP24 || t == FilterType::BP24;
        a.set (cutoff, sampleRate, steep ? 1.85f - 0.6f * res : k);
        b.set (cutoff, sampleRate, steep ? k : 1.0f);
        twoStages = steep;
        drive = 1.0f + driveAmount * driveAmount * 12.0f;
        makeup = 1.0f / std::sqrt (drive);
        bpGain = k;
    }
    float process (float x) noexcept
    {
        if (drive > 1.001f)
            x = std::tanh (x * drive) * makeup * 1.4f;
        auto o = a.process (x);
        float y = pick (o, x);
        if (twoStages)
        {
            o = b.process (y);
            y = pick (o, y);
        }
        return y;
    }

private:
    float pick (const Svf::Out& o, float x) const noexcept
    {
        switch (type)
        {
            case FilterType::LP12:
            case FilterType::LP24: return o.lp;
            case FilterType::HP12:
            case FilterType::HP24: return o.hp;
            case FilterType::BP12:
            case FilterType::BP24: return o.bp * bpGain;
            case FilterType::Notch: return x - bpGain * o.bp;
            case FilterType::Peak: return o.lp - o.hp;
        }
        return o.lp;
    }

    Svf a, b;
    FilterType type = FilterType::LP24;
    bool twoStages = true;
    float drive = 1.0f, makeup = 1.0f, bpGain = 1.0f;
};

// RBJ biquad (EQ bands). Coefficients are computed without allocation.
class Biquad
{
public:
    void reset() noexcept { z1 = z2 = 0.0f; }
    void lowShelf (double sr, double hz, double db) noexcept { shelf (sr, hz, db, true); }
    void highShelf (double sr, double hz, double db) noexcept { shelf (sr, hz, db, false); }
    void peak (double sr, double hz, double db, double q) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = kTwoPi * hz / sr, alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
        setNormalised (1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
    }
    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    void shelf (double sr, double hz, double db, bool low) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = kTwoPi * hz / sr, c = std::cos (w), s = std::sin (w);
        const double alpha = s / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (A) * alpha;
        if (low)
            setNormalised (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                           (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
        else
            setNormalised (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                           (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
    void setNormalised (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = static_cast<float> (nb0 / na0);
        b1 = static_cast<float> (nb1 / na0);
        b2 = static_cast<float> (nb2 / na0);
        a1 = static_cast<float> (na1 / na0);
        a2 = static_cast<float> (na2 / na0);
    }
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
};
} // namespace sunburst
