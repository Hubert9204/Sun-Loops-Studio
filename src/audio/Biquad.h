#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace sls
{
// Biquad coefficients, normalised (a0 = 1), and the RBJ cookbook designs; w0 = 2 pi x frequency / sample rate.
struct BiquadCoefs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static BiquadCoefs normalised (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        return { nb0 / na0, nb1 / na0, nb2 / na0, na1 / na0, na2 / na0 };
    }
    static BiquadCoefs lowPass (double w0, double q) noexcept
    {
        const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        return normalised ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }
    static BiquadCoefs highPass (double w0, double q) noexcept
    {
        const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        return normalised ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }
    static BiquadCoefs bandPass (double w0, double q) noexcept // 0 dB at the centre
    {
        const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        return normalised (alpha, 0, -alpha, 1 + alpha, -2 * c, 1 - alpha);
    }
};

// A biquad (transposed direct form II) with one state per channel (0.51.0: the effects, the drum synthesizer and the loudness
// meter share it).
template <size_t Channels>
struct Biquad : BiquadCoefs
{
    std::array<double, Channels> z1 {}, z2 {};

    Biquad() = default;
    explicit Biquad (const BiquadCoefs& c) noexcept : BiquadCoefs (c) {}
    void setCoefs (const BiquadCoefs& c) noexcept { static_cast<BiquadCoefs&> (*this) = c; }
    void clearState() noexcept
    {
        z1.fill (0.0);
        z2.fill (0.0);
    }
    double tick (size_t ch, double x) noexcept
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
};
} // namespace sls
