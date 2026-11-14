#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// SunRiff (0.68.0): the small DSP building blocks of the guitar. Plain C++, no allocation.
namespace sunriff
{
constexpr float kPi = 3.14159265358979323846f;

// Exact equality of two settings (they change only when a knob moves), without -Wfloat-equal's warning.
inline bool same (float a, float b) noexcept { return ! (a < b) && ! (b < a); }

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }
inline double noteToHz (double note) noexcept { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

// A small, fast random generator (white noise in -1..1).
struct Noise
{
    std::uint32_t state = 0x12345678u;
    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (static_cast<std::int32_t> (state)) * (1.0f / 2147483648.0f);
    }
};

// RBJ biquad, transposed direct form II.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0.0f; }
    void identity() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    void set (double rate, int type, double freq, double q, double gainDb = 0.0) noexcept
    {
        freq = std::clamp (freq, 10.0, rate * 0.49);
        const double w = 2.0 * 3.14159265358979323846 * freq / rate, cw = std::cos (w), sw = std::sin (w);
        const double alpha = sw / (2.0 * std::max (q, 0.05));
        const double A = std::pow (10.0, gainDb / 40.0);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (type)
        {
            case 0: // low pass
                B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case 1: // high pass
                B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case 2: // band pass (0 dB peak)
                B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case 3: // peak
                B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
            case 4: // low shelf
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
                A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case 5: // high shelf
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
                A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
            default: identity(); return;
        }
        b0 = static_cast<float> (B0 / A0); b1 = static_cast<float> (B1 / A0); b2 = static_cast<float> (B2 / A0);
        a1 = static_cast<float> (A1 / A0); a2 = static_cast<float> (A2 / A0);
    }
    enum { LowPass = 0, HighPass = 1, BandPass = 2, Peak = 3, LowShelf = 4, HighShelf = 5 };
};

struct DcBlocker
{
    float x1 = 0, y1 = 0, r = 0.995f;
    void setup (double rate, double hz = 15.0) noexcept { r = static_cast<float> (std::exp (-2.0 * 3.14159265358979 * hz / rate)); }
    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void reset() noexcept { x1 = y1 = 0; }
};

struct OnePole
{
    float a = 0, y = 0;
    void setCutoff (double rate, double hz) noexcept { a = static_cast<float> (std::exp (-2.0 * 3.14159265358979 * std::clamp (hz, 1.0, rate * 0.49) / rate)); }
    float lowpass (float x) noexcept { return y = x + a * (y - x); }
    float highpass (float x) noexcept { return x - lowpass (x); }
    void reset() noexcept { y = 0; }
};

// Soft saturation with first-order antiderivative anti-aliasing (ADAA): tanh without most of its aliasing, sample by sample.
struct AdaaTanh
{
    float x1 = 0.0f;
    static float f1 (float x) noexcept // the antiderivative of tanh: log(cosh(x)), safe for large |x|
    {
        const float a = std::abs (x);
        return a > 15.0f ? a - 0.69314718f : std::log (std::cosh (x));
    }
    float process (float x) noexcept
    {
        const float d = x - x1;
        const float y = std::abs (d) < 1.0e-4f ? std::tanh (0.5f * (x + x1)) : (f1 (x) - f1 (x1)) / d;
        x1 = x;
        return y;
    }
    void reset() noexcept { x1 = 0.0f; }
};

// A delay line with cubic (Lagrange) interpolation. The buffer's length is a power of two.
template <int Size>
struct DelayLine
{
    static_assert ((Size & (Size - 1)) == 0, "a power of two");
    std::array<float, static_cast<size_t> (Size)> data {};
    int write = 0;

    void push (float x) noexcept
    {
        data[static_cast<size_t> (write)] = x;
        write = (write + 1) & (Size - 1);
    }
    // The sample pushed `delay` samples ago (delay >= 1; 1 is the last one pushed).
    float read (float delay) const noexcept
    {
        delay = std::clamp (delay, 1.0f, static_cast<float> (Size - 4));
        const int d = static_cast<int> (delay);
        const float f = delay - static_cast<float> (d);
        const auto at = [this] (int k) { return data[static_cast<size_t> ((write - k) & (Size - 1))]; };
        const float xm1 = at (d - 1 < 1 ? 1 : d - 1), x0 = at (d), x1 = at (d + 1), x2 = at (d + 2);
        // Lagrange, 3rd order, on the points d-1, d, d+1, d+2
        const float c0 = x0;
        const float c1 = x1 - xm1 / 3.0f - x0 / 2.0f - x2 / 6.0f;
        const float c2 = 0.5f * (xm1 + x1) - x0;
        const float c3 = (x2 - xm1) / 6.0f + 0.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
    float tap (int delay) const noexcept { return data[static_cast<size_t> ((write - std::max (1, delay)) & (Size - 1))]; }
    void clear() noexcept { data.fill (0.0f); }
};

// A simple LFO (sine and triangle), phase 0..1.
struct Lfo
{
    double phase = 0.0;
    float sine (double hz, double rate) noexcept
    {
        const float v = std::sin (2.0f * kPi * static_cast<float> (phase));
        advance (hz, rate);
        return v;
    }
    float triangle (double hz, double rate) noexcept
    {
        const float v = 1.0f - 4.0f * std::abs (static_cast<float> (phase) - 0.5f);
        advance (hz, rate);
        return v;
    }
    void advance (double hz, double rate) noexcept
    {
        phase += hz / rate;
        phase -= std::floor (phase);
    }
};

// A first-order allpass section (for phasers and the string's stiffness).
struct Allpass1
{
    float c = 0, x1 = 0, y1 = 0;
    float process (float x) noexcept
    {
        const float y = c * x + x1 - c * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void reset() noexcept { x1 = y1 = 0; }
    // Phase delay (in samples) of a first-order allpass with coefficient c at angular frequency w.
    static double phaseDelay (double c, double w) noexcept
    {
        // H = (c + z^-1) / (1 + c z^-1)
        const double re = c + std::cos (w), im = -std::sin (w);
        const double re2 = 1.0 + c * std::cos (w), im2 = -c * std::sin (w);
        const double phase = std::atan2 (im, re) - std::atan2 (im2, re2);
        return -phase / w;
    }
};
} // namespace sunriff
