#include "audio/DrumSynth.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>

namespace sls
{
namespace
{
constexpr double kSr = 44100.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

using Buffer = std::vector<double>;

Buffer silence (double seconds)
{
    return Buffer (static_cast<size_t> (std::max (1.0, seconds * kSr)), 0.0);
}

double timeOf (size_t i) noexcept { return static_cast<double> (i) / kSr; }

// Deterministic noise in [-1, 1] (xorshift32): the same sound on every computer.
struct Rng
{
    std::uint32_t state;
    explicit Rng (std::uint32_t seed) noexcept : state (seed != 0 ? seed : 0x9e3779b9u) {}
    double next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double> (state) / 2147483647.5 - 1.0;
    }
    double uniform() noexcept { return 0.5 * (next() + 1.0); } // [0, 1]
};

// RBJ biquads (transposed direct form II).
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    double tick (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void process (Buffer& b) noexcept
    {
        for (auto& s : b)
            s = tick (s);
    }

    static Biquad make (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        Biquad q;
        q.b0 = b0 / a0;
        q.b1 = b1 / a0;
        q.b2 = b2 / a0;
        q.a1 = a1 / a0;
        q.a2 = a2 / a0;
        return q;
    }
    static double omega (double hz) noexcept { return kTwoPi * std::clamp (hz, 10.0, 0.45 * kSr) / kSr; }
    static Biquad lowPass (double hz, double q)
    {
        const double w = omega (hz), c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return make ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }
    static Biquad highPass (double hz, double q)
    {
        const double w = omega (hz), c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return make ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }
    static Biquad bandPass (double hz, double q) // 0 dB at the centre
    {
        const double w = omega (hz), c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return make (alpha, 0, -alpha, 1 + alpha, -2 * c, 1 - alpha);
    }
};

void filter (Buffer& b, Biquad f) { f.process (b); }

// A sine whose frequency glides from `start` to `end` (exponentially, time constant `glide`) with an exponential decay `decay`.
void addTone (Buffer& b, double start, double end, double glide, double decay, double amp, double phase = 0.0, double attack = 0.0)
{
    double ph = phase;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = timeOf (i);
        const double f = end + (start - end) * std::exp (-t / std::max (glide, 1e-4));
        const double env = std::exp (-t / decay) * (attack > 0 ? 1.0 - std::exp (-t / attack) : 1.0);
        b[i] += amp * env * std::sin (ph);
        ph += kTwoPi * f / kSr;
    }
}

// A band-limited square wave (odd harmonics below 19 kHz), amplitude 1, added with the envelope `env(t)`.
void addSquare (Buffer& b, double f, double amp, const std::function<double (double)>& env)
{
    for (int k = 1; k * f < 19000.0; k += 2)
    {
        const double a = amp * 4.0 / (kPi * k);
        // a rotating phasor instead of std::sin for every sample and harmonic
        const double w = kTwoPi * k * f / kSr;
        double re = 1.0, im = 0.0;
        const double cr = std::cos (w), ci = std::sin (w);
        for (size_t i = 0; i < b.size(); ++i)
        {
            b[i] += a * im * env (timeOf (i));
            const double nr = re * cr - im * ci;
            im = re * ci + im * cr;
            re = nr;
        }
    }
}

// Noise with an envelope.
void addNoise (Buffer& b, Rng& rng, double amp, const std::function<double (double)>& env)
{
    for (size_t i = 0; i < b.size(); ++i)
        b[i] += amp * rng.next() * env (timeOf (i));
}

Buffer noiseBuffer (size_t n, Rng& rng, double amp, const std::function<double (double)>& env)
{
    Buffer b (n, 0.0);
    addNoise (b, rng, amp, env);
    return b;
}

void mix (Buffer& into, const Buffer& from, double gain = 1.0)
{
    for (size_t i = 0; i < into.size() && i < from.size(); ++i)
        into[i] += gain * from[i];
}

// Soft saturation with the level kept: tanh(drive * x) / tanh(drive).
void saturate (Buffer& b, double drive)
{
    if (drive <= 1e-3)
        return;
    const double norm = 1.0 / std::tanh (drive);
    double peak = 0.0;
    for (const double s : b)
        peak = std::max (peak, std::fabs (s));
    if (peak <= 0.0)
        return;
    for (auto& s : b)
        s = std::tanh (drive * s / peak) * norm * peak;
}

double expEnv (double t, double tau) noexcept { return std::exp (-t / tau); }

// A struck mode: amplitude, frequency, decay; the strike excites it after `delay` seconds.
struct Mode
{
    double freq, amp, decay, delay = 0.0;
};

void addModes (Buffer& b, const std::vector<Mode>& modes, double bend = 0.0, double bendTime = 0.03)
{
    for (const auto& m : modes)
    {
        if (m.freq >= 0.45 * kSr)
            continue;
        double ph = 0.0;
        for (size_t i = 0; i < b.size(); ++i)
        {
            const double t = timeOf (i) - m.delay;
            if (t < 0.0)
                continue;
            const double f = m.freq * (1.0 + bend * std::exp (-t / bendTime));
            b[i] += m.amp * std::exp (-t / m.decay) * std::sin (ph);
            ph += kTwoPi * f / kSr;
        }
    }
}

// --- The sounds --------------------------------------------------------------------------------------------------------------
// `v` is the strength of the hit: 0.3, 0.55, 0.78 and 1 for the four layers. `rng` is seeded per kit, piece and layer.

struct Style
{
    double kickBase, kickStart, kickGlide, kickDecay, kickClick, kickDrive, kickLength;
    double snareTone1, snareTone2, snareToneDecay, snareNoiseDecay, snareNoiseLow, snareNoiseHigh, snareNoise;
    double hatDecayClosed, hatDecayOpen, hatCymbalDecay, hatNoise; // hatNoise: how much plain noise is mixed with the metal
    double tomGlide, tomDecay, tomNoise;
};

constexpr Style kBoom { 49.0, 2.6, 0.030, 0.42, 0.25, 0.6, 1.4,
                        238.0, 476.0, 0.045, 0.085, 1800.0, 9000.0, 0.65,
                        0.035, 0.30, 1.1, 0.10,
                        0.060, 0.26, 0.10 };
constexpr Style kPunch { 52.0, 5.0, 0.011, 0.20, 0.9, 1.4, 0.75,
                         186.0, 332.0, 0.060, 0.14, 1200.0, 7500.0, 0.95,
                         0.045, 0.38, 1.4, 0.55,
                         0.040, 0.22, 0.35 };
constexpr Style kModern { 44.0, 3.4, 0.040, 0.85, 0.7, 2.4, 2.4,
                          200.0, 290.0, 0.050, 0.16, 1500.0, 11000.0, 1.0,
                          0.022, 0.26, 1.3, 0.35,
                          0.050, 0.30, 0.20 };

Buffer kick (const Style& s, double v, Rng& rng)
{
    auto b = silence (s.kickLength * (0.75 + 0.25 * v));
    const double start = s.kickBase * (1.0 + (s.kickStart - 1.0) * (0.55 + 0.45 * v));
    addTone (b, start, s.kickBase, s.kickGlide, s.kickDecay * (0.85 + 0.15 * v), 1.0, 0.0, 0.0005);
    // the beater / click: a short burst of high-passed noise and a 1.6 kHz blip, stronger on harder hits
    auto click = noiseBuffer (b.size(), rng, s.kickClick * v, [] (double t) { return expEnv (t, 0.0025); });
    filter (click, Biquad::highPass (1500.0, 0.7));
    mix (b, click);
    addTone (b, 1600.0, 1200.0, 0.002, 0.003, 0.25 * s.kickClick * v);
    saturate (b, s.kickDrive * (0.6 + 0.4 * v));
    filter (b, Biquad::highPass (22.0, 0.7)); // no DC, no sub-sonic rumble
    return b;
}

Buffer snare (const Style& s, double v, Rng& rng)
{
    auto b = silence (0.45);
    const double bend = 1.0 + 0.08 * v;
    addTone (b, s.snareTone1 * bend, s.snareTone1, 0.012, s.snareToneDecay, 0.7);
    addTone (b, s.snareTone2 * bend, s.snareTone2, 0.010, s.snareToneDecay * 0.7, 0.45);
    const double tau = s.snareNoiseDecay * (0.7 + 0.3 * v);
    auto noise = noiseBuffer (b.size(), rng, s.snareNoise * (0.7 + 0.5 * v), [tau] (double t) { return expEnv (t, tau) * (1.0 - expEnv (t, 0.0008)); });
    filter (noise, Biquad::highPass (s.snareNoiseLow, 0.7));
    filter (noise, Biquad::lowPass (s.snareNoiseHigh * (0.6 + 0.4 * v), 0.7));
    mix (b, noise);
    saturate (b, 0.6 * v);
    return b;
}

Buffer clap (double v, Rng& rng, double tail)
{
    auto b = silence (0.2 + 2.5 * tail);
    // three to four quick bursts (several hands) and a diffuse tail
    const int bursts = v > 0.5 ? 4 : 3;
    std::vector<double> at;
    at.reserve (static_cast<size_t> (bursts));
    for (int k = 0; k < bursts; ++k)
        at.push_back (0.0105 * k + 0.0015 * rng.next());
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = timeOf (i);
        double env = 0.0;
        for (const double a : at)
            if (t >= a)
                env = std::max (env, expEnv (t - a, 0.0032));
        if (t >= at.back())
            env = std::max (env, 0.55 * expEnv (t - at.back(), tail));
        b[i] = rng.next() * env;
    }
    filter (b, Biquad::bandPass (1050.0 + 400.0 * v, 1.4));
    filter (b, Biquad::highPass (500.0, 0.7));
    return b;
}

// The six square oscillators of the classic analog hats and cymbal.
constexpr double kMetal[] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };

Buffer metal (double seconds, double decay, double v, Rng& rng, double noiseAmount, double low, double high, double attack = 0.0)
{
    auto b = silence (seconds);
    auto env = [decay, attack] (double t) { return expEnv (t, decay) * (attack > 0 ? 1.0 - expEnv (t, attack) : 1.0); };
    for (const double f : kMetal)
        addSquare (b, f * (1.0 + 0.002 * rng.next()), 0.25, env);
    addNoise (b, rng, noiseAmount, env);
    filter (b, Biquad::bandPass (high, 0.9));
    filter (b, Biquad::highPass (low * (1.15 - 0.15 * v), 0.7));
    filter (b, Biquad::highPass (low, 0.7));
    filter (b, Biquad::lowPass (9000.0 + 8000.0 * v, 0.7));
    return b;
}

Buffer closedHat (const Style& s, double v, Rng& rng) { return metal (0.25, s.hatDecayClosed * (0.8 + 0.3 * v), v, rng, s.hatNoise, 6500.0, 8500.0); }
Buffer pedalHat (const Style& s, double v, Rng& rng) { return metal (0.2, s.hatDecayClosed * 0.75, v, rng, s.hatNoise * 0.6, 5200.0, 7000.0, 0.0015); }
Buffer openHat (const Style& s, double v, Rng& rng) { return metal (1.2, s.hatDecayOpen * (0.8 + 0.3 * v), v, rng, s.hatNoise, 6000.0, 8000.0); }

Buffer cymbal (const Style& s, double v, Rng& rng)
{
    auto low = metal (3.5, s.hatCymbalDecay * 0.6, v, rng, s.hatNoise, 2800.0, 3400.0);
    auto high = metal (3.5, s.hatCymbalDecay * (0.9 + 0.2 * v), v, rng, s.hatNoise + 0.2, 6000.0, 9500.0);
    mix (high, low, 0.6);
    return high;
}

Buffer ride (const Style& s, double v, Rng& rng)
{
    auto b = metal (2.6, s.hatCymbalDecay * 0.8, v, rng, s.hatNoise * 0.5, 4500.0, 7600.0);
    // the ping of the stick and the bell
    addModes (b, { { 2950.0, 0.08 * (0.6 + 0.4 * v), 0.7 }, { 5170.0, 0.05, 0.45 }, { 7300.0, 0.03, 0.35 } });
    return b;
}

Buffer tom (const Style& s, double pitch, double v, Rng& rng)
{
    auto b = silence (0.9);
    addTone (b, pitch * (1.25 + 0.35 * v), pitch, s.tomGlide, s.tomDecay * (0.85 + 0.15 * v), 1.0, 0.0, 0.0005);
    addTone (b, pitch * 1.5 * (1.2 + 0.3 * v), pitch * 1.5, s.tomGlide, s.tomDecay * 0.4, 0.25);
    auto hit = noiseBuffer (b.size(), rng, s.tomNoise * (0.4 + 0.6 * v), [] (double t) { return expEnv (t, 0.012); });
    filter (hit, Biquad::bandPass (pitch * 8.0, 0.8));
    mix (b, hit);
    saturate (b, 0.4 * v);
    return b;
}

Buffer rim (double v, Rng& rng)
{
    auto b = silence (0.12);
    addTone (b, 1700.0, 1700.0, 1.0, 0.010, 0.8);
    addTone (b, 460.0, 455.0, 1.0, 0.016, 0.6);
    auto click = noiseBuffer (b.size(), rng, 0.5 * v, [] (double t) { return expEnv (t, 0.0015); });
    filter (click, Biquad::highPass (3000.0, 0.7));
    mix (b, click);
    filter (b, Biquad::bandPass (1250.0, 0.8));
    saturate (b, 1.5);
    return b;
}

Buffer analogCowbell (double v, Rng& rng)
{
    auto b = silence (0.6);
    auto env = [v] (double t) { return 0.62 * expEnv (t, 0.012) + 0.38 * expEnv (t, 0.10 + 0.04 * v); };
    addSquare (b, 540.0 * (1.0 + 0.001 * rng.next()), 0.5, env);
    addSquare (b, 800.0, 0.5, env);
    filter (b, Biquad::bandPass (2640.0, 1.3));
    filter (b, Biquad::bandPass (1700.0 + 900.0 * v, 0.7)); // a harder hit lets more of the upper partials through
    auto click = noiseBuffer (b.size(), rng, 0.3 * v, [] (double t) { return expEnv (t, 0.0012); });
    filter (click, Biquad::highPass (4000.0, 0.7));
    mix (b, click);
    return b;
}

Buffer clave (double v, Rng&)
{
    auto b = silence (0.15);
    addTone (b, 2500.0, 2500.0, 1.0, 0.016 + 0.006 * v, 1.0);
    addTone (b, 2500.0 * 2.75, 2500.0 * 2.75, 1.0, 0.006, 0.15 * v);
    return b;
}

Buffer shaker (double v, Rng& rng, double length = 0.16)
{
    auto b = silence (length + 0.05);
    // many small grains, then a quick fade: a shake forward
    const double attack = 0.012, decay = 0.035 + 0.02 * v;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = timeOf (i);
        const double env = (1.0 - expEnv (t, attack)) * expEnv (t, decay);
        const double grain = rng.uniform() < 0.35 ? rng.next() : 0.15 * rng.next();
        b[i] = grain * env;
    }
    filter (b, Biquad::highPass (4200.0, 0.7));
    filter (b, Biquad::bandPass (7500.0 + 2500.0 * v, 0.6));
    return b;
}

Buffer analogConga (double pitch, double v, Rng& rng)
{
    auto b = silence (0.5);
    addTone (b, pitch * (1.05 + 0.08 * v), pitch, 0.02, 0.10 + 0.03 * v, 1.0, 0.0, 0.0005);
    auto hit = noiseBuffer (b.size(), rng, 0.15 * v, [] (double t) { return expEnv (t, 0.004); });
    filter (hit, Biquad::bandPass (pitch * 6.0, 1.0));
    mix (b, hit);
    return b;
}

Buffer subBoom (double v, Rng& rng)
{
    // the long distorted "808" sub of modern styles: a kick that rings like a bass note (tuned to C1)
    Style s = kModern;
    s.kickBase = 32.70;
    s.kickDecay = 1.3;
    s.kickLength = 3.0;
    s.kickDrive = 3.5;
    return kick (s, v, rng);
}

// --- Hand percussion (modal synthesis) ---------------------------------------------------------------------------------------
// Ratios of the modes of an ideal circular membrane (Bessel zeros relative to the first).
constexpr double kMembrane[] = { 1.0, 1.594, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, 3.600, 3.652 };

// A hand drum: the head's modes (shaped so that the lowest dominates, as the shell and the hand damp the others), a pitch that
// drops as the head relaxes, and the slap of the hand. `mute` shortens everything (a muted or closed stroke).
Buffer handDrum (double pitch, double v, Rng& rng, double decay, double bright, bool mute)
{
    auto b = silence (mute ? 0.3 : 0.9);
    std::vector<Mode> modes;
    // a hand drum's head is tuned so that its lowest modes are nearly harmonic: pull the ideal ratios towards 1, 2, 3
    for (size_t k = 0; k < std::size (kMembrane); ++k)
    {
        const double r = k == 0 ? 1.0 : 0.6 * kMembrane[k] + 0.4 * std::round (kMembrane[k] * 1.3);
        const double amp = std::pow (0.55 + 0.25 * bright * v, static_cast<double> (k)) * (1.0 + 0.1 * rng.next());
        const double tau = (mute ? 0.35 : 1.0) * decay / std::pow (r, 1.3);
        modes.push_back ({ pitch * r * (1.0 + 0.003 * rng.next()), amp, tau });
    }
    addModes (b, modes, 0.035 * v, 0.04);
    auto slap = noiseBuffer (b.size(), rng, (mute ? 0.5 : 0.25) * (0.4 + 0.6 * v), [] (double t) { return expEnv (t, 0.005); });
    filter (slap, Biquad::bandPass (pitch * 7.0 * (0.8 + 0.4 * bright), 0.9));
    mix (b, slap);
    return b;
}

Buffer timbale (double pitch, double v, Rng& rng)
{
    auto b = handDrum (pitch, v, rng, 0.35, 1.0, false);
    // the steel shell rings, the stick clicks
    addModes (b, { { pitch * 4.71, 0.10, 0.25 }, { pitch * 6.93, 0.07, 0.18 }, { 3100.0, 0.05 * v, 0.30 } });
    auto stick = noiseBuffer (b.size(), rng, 0.6 * v, [] (double t) { return expEnv (t, 0.0018); });
    filter (stick, Biquad::highPass (2500.0, 0.7));
    mix (b, stick);
    return b;
}

// A bell (agogo, cowbell): inharmonic partials of a bent plate.
Buffer bell (double pitch, double v, Rng& rng, const std::vector<double>& ratios, double decay)
{
    auto b = silence (decay * 4.0 + 0.1);
    std::vector<Mode> modes;
    for (size_t k = 0; k < ratios.size(); ++k)
        modes.push_back ({ pitch * ratios[k], std::pow (0.45 + 0.25 * v, static_cast<double> (k)), decay / (1.0 + 0.6 * static_cast<double> (k)) });
    addModes (b, modes);
    auto stick = noiseBuffer (b.size(), rng, 0.35 * v, [] (double t) { return expEnv (t, 0.0015); });
    filter (stick, Biquad::bandPass (pitch * 3.0, 0.8));
    mix (b, stick);
    return b;
}

Buffer woodBlock (double pitch, double v, Rng& rng)
{
    auto b = silence (0.2);
    addModes (b, { { pitch, 1.0, 0.045 }, { pitch * 2.57, 0.45 * v, 0.020 }, { pitch * 4.21, 0.25 * v, 0.010 } });
    auto stick = noiseBuffer (b.size(), rng, 0.4 * v, [] (double t) { return expEnv (t, 0.001); });
    filter (stick, Biquad::bandPass (pitch * 2.0, 0.7));
    mix (b, stick);
    return b;
}

Buffer realClaves (double v, Rng& rng)
{
    auto b = silence (0.3);
    addModes (b, { { 2480.0, 1.0, 0.075 }, { 2480.0 * 2.71, 0.3 * v, 0.03 }, { 2480.0 * 5.2, 0.1 * v, 0.015 } });
    auto click = noiseBuffer (b.size(), rng, 0.3 * v, [] (double t) { return expEnv (t, 0.0008); });
    filter (click, Biquad::highPass (2000.0, 0.7));
    mix (b, click);
    return b;
}

Buffer tambourine (double v, Rng& rng)
{
    auto b = silence (0.7);
    // eight pairs of jingles struck within a few milliseconds of each other, each ringing at a few high, inharmonic partials
    const int jingles = 8;
    std::vector<Mode> modes;
    for (int j = 0; j < jingles; ++j)
    {
        const double base = 5200.0 * (1.0 + 0.08 * rng.next());
        const double at = 0.012 * rng.uniform() * (1.2 - 0.4 * v);
        for (const double r : { 1.0, 1.31, 1.66, 2.1, 2.38 })
            modes.push_back ({ base * r, (0.3 + 0.7 * rng.uniform()) / jingles, 0.06 + 0.10 * rng.uniform(), at });
    }
    addModes (b, modes);
    auto rattle = noiseBuffer (b.size(), rng, 0.25 * v, [] (double t) { return expEnv (t, 0.03); });
    filter (rattle, Biquad::highPass (6000.0, 0.7));
    mix (b, rattle);
    // the skin under the hand
    addTone (b, 240.0, 220.0, 0.01, 0.03, 0.15 * v);
    return b;
}

Buffer triangle (double v, Rng& rng)
{
    auto b = silence (3.2);
    // a bent steel bar: nearly harmonic partials that ring for seconds, the high ones brought out by a hard beater
    std::vector<Mode> modes;
    const double f = 1270.0;
    for (int k = 1; k <= 14; ++k)
    {
        const double r = k * (1.0 + 0.0009 * k * k);
        modes.push_back ({ f * r, std::pow (0.78 + 0.12 * v, static_cast<double> (k - 1)) / std::sqrt (static_cast<double> (k)) * (1.0 + 0.2 * rng.next()),
                           2.4 / std::pow (static_cast<double> (k), 0.35) });
    }
    addModes (b, modes);
    return b;
}

// The level of each layer before the drum machine's own velocity scaling: soft layers a little quieter, as recorded hits would be.
constexpr std::array<double, kGeneratedLayers> kStrength { 0.3, 0.55, 0.78, 1.0 };
constexpr std::array<double, kGeneratedLayers> kLevel { 0.62, 0.75, 0.88, 1.0 };

std::shared_ptr<const SampleData> finish (Buffer b, double level, const std::string& name, const std::string& id)
{
    // trim the inaudible tail (below -70 dB of the peak), fade the last 5 ms, normalise
    double peak = 0.0;
    for (const double s : b)
        peak = std::max (peak, std::fabs (s));
    if (peak <= 0.0 || ! std::isfinite (peak))
        return nullptr;
    size_t end = b.size();
    while (end > 64 && std::fabs (b[end - 1]) < peak * 3.2e-4)
        --end;
    b.resize (end);
    const size_t fade = std::min<size_t> (b.size(), static_cast<size_t> (0.005 * kSr));
    for (size_t i = 0; i < fade; ++i)
    {
        const double x = static_cast<double> (i) / static_cast<double> (fade);
        b[b.size() - 1 - i] *= std::sin (0.5 * kPi * x);
    }
    const double gain = 0.89 * level / peak;
    auto data = std::make_shared<SampleData>();
    data->left.resize (b.size());
    for (size_t i = 0; i < b.size(); ++i)
        data->left[i] = static_cast<float> (b[i] * gain);
    data->right = data->left;
    data->sampleRate = kSr;
    data->name = name;
    data->source = "builtin:" + id;
    return data;
}

using Maker = std::function<Buffer (double v, Rng&)>;

const Style& styleOf (const std::string& kit)
{
    if (kit == "punch")
        return kPunch;
    if (kit == "modern")
        return kModern;
    return kBoom;
}

Maker makerFor (const std::string& kit, const std::string& piece)
{
    if (kit == "percussion")
    {
        if (piece == "conga-high")  return [] (double v, Rng& r) { return handDrum (330.0, v, r, 0.32, 0.6, false); };
        if (piece == "conga-mute")  return [] (double v, Rng& r) { return handDrum (330.0, v, r, 0.32, 0.9, true); };
        if (piece == "conga-low")   return [] (double v, Rng& r) { return handDrum (220.0, v, r, 0.40, 0.6, false); };
        if (piece == "bongo-high")  return [] (double v, Rng& r) { return handDrum (520.0, v, r, 0.16, 0.9, false); };
        if (piece == "bongo-low")   return [] (double v, Rng& r) { return handDrum (370.0, v, r, 0.20, 0.8, false); };
        if (piece == "timbale-high") return [] (double v, Rng& r) { return timbale (450.0, v, r); };
        if (piece == "timbale-low") return [] (double v, Rng& r) { return timbale (300.0, v, r); };
        if (piece == "cowbell")     return [] (double v, Rng& r) { return bell (560.0, v, r, { 1.0, 1.505, 2.32, 3.91 }, 0.16); };
        if (piece == "agogo-high")  return [] (double v, Rng& r) { return bell (920.0, v, r, { 1.0, 2.76, 5.40, 8.93 }, 0.38); };
        if (piece == "agogo-low")   return [] (double v, Rng& r) { return bell (690.0, v, r, { 1.0, 2.76, 5.40, 8.93 }, 0.42); };
        if (piece == "block-high")  return [] (double v, Rng& r) { return woodBlock (1150.0, v, r); };
        if (piece == "block-low")   return [] (double v, Rng& r) { return woodBlock (820.0, v, r); };
        if (piece == "claves")      return [] (double v, Rng& r) { return realClaves (v, r); };
        if (piece == "shaker")      return [] (double v, Rng& r) { return shaker (v, r, 0.18); };
        if (piece == "tambourine")  return [] (double v, Rng& r) { return tambourine (v, r); };
        if (piece == "triangle")    return [] (double v, Rng& r) { return triangle (v, r); };
        return {};
    }
    const Style& s = styleOf (kit);
    const double tomBase = kit == "modern" ? 82.0 : kit == "punch" ? 92.0 : 87.0;
    if (piece == "kick")       return [&s] (double v, Rng& r) { return kick (s, v, r); };
    if (piece == "rim")        return [] (double v, Rng& r) { return rim (v, r); };
    if (piece == "snare")      return [&s] (double v, Rng& r) { return snare (s, v, r); };
    if (piece == "clap")       return [kit] (double v, Rng& r) { return clap (v, r, kit == "modern" ? 0.20 : 0.11); };
    if (piece == "snare-2")
        return [&s, kit] (double v, Rng& r)
        {
            // a second snare: the snare and a clap together (modern), or a tighter, higher snare
            if (kit == "modern")
            {
                auto b = snare (s, v, r);
                mix (b, clap (v, r, 0.12), 0.8);
                return b;
            }
            Style tight = s;
            tight.snareTone1 *= 1.18;
            tight.snareTone2 *= 1.18;
            tight.snareNoiseDecay *= 0.7;
            return snare (tight, v, r);
        };
    if (piece == "tom-1")       return [&s, tomBase] (double v, Rng& r) { return tom (s, tomBase, v, r); };
    if (piece == "tom-2")       return [&s, tomBase] (double v, Rng& r) { return tom (s, tomBase * 1.19, v, r); };
    if (piece == "tom-3")       return [&s, tomBase] (double v, Rng& r) { return tom (s, tomBase * 1.41, v, r); };
    if (piece == "tom-4")       return [&s, tomBase] (double v, Rng& r) { return tom (s, tomBase * 1.68, v, r); };
    if (piece == "tom-5")       return [&s, tomBase] (double v, Rng& r) { return tom (s, tomBase * 2.0, v, r); };
    if (piece == "tom-6")       return [&s, tomBase] (double v, Rng& r) { return tom (s, tomBase * 2.38, v, r); };
    if (piece == "closed-hat") return [&s] (double v, Rng& r) { return closedHat (s, v, r); };
    if (piece == "pedal-hat")  return [&s] (double v, Rng& r) { return pedalHat (s, v, r); };
    if (piece == "open-hat")   return [&s] (double v, Rng& r) { return openHat (s, v, r); };
    if (piece == "crash")      return [&s] (double v, Rng& r) { return cymbal (s, v, r); };
    if (piece == "ride")       return [&s] (double v, Rng& r) { return ride (s, v, r); };
    if (piece == "cowbell")    return [] (double v, Rng& r) { return analogCowbell (v, r); };
    if (piece == "clave")      return [] (double v, Rng& r) { return clave (v, r); };
    if (piece == "shaker")     return [] (double v, Rng& r) { return shaker (v, r); };
    if (piece == "conga")      return [] (double v, Rng& r) { return analogConga (370.0, v, r); };
    if (piece == "sub")        return [] (double v, Rng& r) { return subBoom (v, r); };
    return {};
}

std::uint32_t seedFor (const std::string& text, int layer)
{
    std::uint32_t h = 2166136261u; // FNV-1a
    for (const char c : text)
        h = (h ^ static_cast<std::uint8_t> (c)) * 16777619u;
    return (h ^ (static_cast<std::uint32_t> (layer) * 0x9e3779b1u)) | 1u;
}
} // namespace

const std::vector<GeneratedKitInfo>& generatedKits()
{
    static const std::vector<GeneratedKitInfo> kits = []
    {
        // The General MIDI drum order from key 36 (pad 1): kick, side stick, snare, clap, snare 2, low floor tom, closed hat,
        // high floor tom, pedal hat, low tom, open hat, low-mid tom, high-mid tom, crash, high tom, ride. The electronic kits
        // put their extra sounds (cowbell, clave, shaker, conga, sub) where an acoustic kit has more toms or a ride.
        const auto electronic = [] (const char* id, const char* name, const std::array<std::pair<const char*, const char*>, 4>& extra)
        {
            GeneratedKitInfo k;
            k.id = id;
            k.name = name;
            k.pads = { { "Kick", "kick" },
                       { "Rim", "rim" },
                       { "Snare", "snare" },
                       { "Clap", "clap" },
                       { "Snare 2", "snare-2" },
                       { "Low Tom", "tom-1" },
                       { "Closed Hat", "closed-hat", 1, -0.25f },
                       { extra[0].first, extra[0].second },
                       { "Pedal Hat", "pedal-hat", 1, -0.25f },
                       { "Mid Tom", "tom-3" },
                       { "Open Hat", "open-hat", 1, -0.25f },
                       { extra[1].first, extra[1].second },
                       { "High Tom", "tom-5" },
                       { "Crash", "crash", 0, 0.3f },
                       { extra[2].first, extra[2].second },
                       { extra[3].first, extra[3].second } };
            // pan the toms from low (left) to high (right), as seen from the audience
            for (auto& p : k.pads)
                if (p.piece == "tom-1") p.pan = -0.3f;
                else if (p.piece == "tom-3") p.pan = 0.0f;
                else if (p.piece == "tom-5") p.pan = 0.3f;
            return k;
        };
        std::vector<GeneratedKitInfo> list;
        list.push_back (electronic ("boom", "Electronic Boom", { { { "Conga", "conga" }, { "Cowbell", "cowbell" }, { "Clave", "clave" }, { "Shaker", "shaker" } } }));
        list.push_back (electronic ("punch", "Electronic Punch", { { { "Low-mid Tom", "tom-2" }, { "High-mid Tom", "tom-4" }, { "Highest Tom", "tom-6" }, { "Ride", "ride" } } }));
        list.push_back (electronic ("modern", "Electronic Modern", { { { "Sub 808", "sub" }, { "Shaker", "shaker" }, { "Cowbell", "cowbell" }, { "Ride", "ride" } } }));

        GeneratedKitInfo perc;
        perc.id = "percussion";
        perc.name = "Hand Percussion";
        perc.pads = { { "Conga High", "conga-high", 0, 0.2f },   { "Conga Mute", "conga-mute", 0, 0.2f }, { "Conga Low", "conga-low", 0, -0.1f },
                      { "Bongo High", "bongo-high", 0, 0.35f },  { "Bongo Low", "bongo-low", 0, 0.25f },  { "Timbale High", "timbale-high", 0, -0.35f },
                      { "Timbale Low", "timbale-low", 0, -0.3f }, { "Cowbell", "cowbell", 0, -0.2f },     { "Agogo High", "agogo-high", 0, 0.4f },
                      { "Agogo Low", "agogo-low", 0, 0.4f },     { "Block High", "block-high", 0, -0.4f }, { "Block Low", "block-low", 0, -0.4f },
                      { "Claves", "claves", 0, 0.1f },           { "Shaker", "shaker", 0, 0.3f },          { "Tambourine", "tambourine", 0, -0.25f },
                      { "Triangle", "triangle", 0, 0.45f } };
        list.push_back (std::move (perc));
        return list;
    }();
    return kits;
}

bool isGeneratedId (const std::string& id)
{
    return id.rfind ("gen:", 0) == 0;
}

std::shared_ptr<const SampleData> makeGeneratedSample (const std::string& id)
{
    if (! isGeneratedId (id))
        return nullptr;
    // "gen:<kit>/<piece>/<layer>"
    const auto rest = id.substr (4);
    const auto a = rest.find ('/');
    const auto b = rest.rfind ('/');
    if (a == std::string::npos || b == a || b + 2 != rest.size() || rest[b + 1] < '1' || rest[b + 1] > '0' + kGeneratedLayers)
        return nullptr;
    const auto kit = rest.substr (0, a);
    const auto piece = rest.substr (a + 1, b - a - 1);
    const int layer = rest[b + 1] - '0';

    const GeneratedKitInfo* info = nullptr;
    const GeneratedPad* pad = nullptr;
    for (const auto& k : generatedKits())
        if (k.id == kit)
            for (const auto& p : k.pads)
                if (p.piece == piece)
                {
                    info = &k;
                    pad = &p;
                }
    const auto maker = makerFor (kit, piece);
    if (pad == nullptr || maker == nullptr)
        return nullptr;

    static std::mutex lock;
    static std::map<std::string, std::weak_ptr<const SampleData>> cache;
    const std::lock_guard<std::mutex> guard (lock);
    if (const auto found = cache.find (id); found != cache.end())
        if (auto alive = found->second.lock())
            return alive;

    Rng rng (seedFor (kit + "/" + piece, layer));
    const auto index = static_cast<size_t> (layer - 1);
    auto sample = finish (maker (kStrength[index], rng), kLevel[index], info->name + " " + pad->name + " " + std::to_string (layer), id);
    if (sample != nullptr)
        cache[id] = sample;
    return sample;
}
} // namespace sls
