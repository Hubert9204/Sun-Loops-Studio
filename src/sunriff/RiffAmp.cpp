#include "sunriff/RiffAmp.h"

namespace sunriff
{
void Amp::prepare (double sampleRate) noexcept
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    configured = false;
    for (auto& d : dc)
        d.setup (rate, 10.0);
    set (settings);
    reset();
}

void Amp::reset() noexcept
{
    for (auto* b : { &presenceShelf, &lowShelf, &midPeak, &highShelf })
        b->reset();
    for (int i = 0; i < 3; ++i)
    {
        stageHp[static_cast<size_t> (i)].reset();
        stageLp[static_cast<size_t> (i)].reset();
        stageBand[static_cast<size_t> (i)].reset();
        sat[static_cast<size_t> (i)].reset();
        dc[static_cast<size_t> (i)].reset();
    }
    for (auto& c : cabinet)
        c.reset();
}

void Amp::set (const AmpSettings& s) noexcept
{
    if (configured && s == settings)
        return;
    settings = s;
    configured = true;
    presenceShelf.set (rate, Biquad::HighShelf, 3200.0, 0.7, 12.0 * s.presence);
    lowShelf.set (rate, Biquad::LowShelf, 200.0, 0.7, 12.0 * s.low);
    midPeak.set (rate, Biquad::Peak, 1000.0, 0.8, 12.0 * s.mid);
    highShelf.set (rate, Biquad::HighShelf, 2500.0, 0.7, 12.0 * s.high);

    // The three stages: the gain spread over them (most in the first two), tightening and smoothing filters between them.
    const float total = 1.0f + s.drive * s.drive * 600.0f;
    gains = { std::pow (total, 0.42f), std::pow (total, 0.36f), std::pow (total, 0.22f) };
    static constexpr double hp[] { 30.0, 140.0, 70.0 }, lp[] { 12000.0, 8500.0, 6500.0 };
    const double band = 300.0 * std::pow (10.0, 0.5 * (s.presence + 1.0)); // Bandpass: 300 Hz .. 3 kHz
    for (size_t i = 0; i < 3; ++i)
    {
        stageHp[i].set (rate, Biquad::HighPass, hp[i], 0.707);
        stageLp[i].set (rate, Biquad::LowPass, lp[i], 0.707);
        stageBand[i].set (rate, Biquad::BandPass, band * (1.0 + 0.25 * static_cast<double> (i)), 0.8);
    }
    // Louder drive, quieter output: about the same loudness over the knob's travel.
    makeup = 0.55f / std::pow (1.0f + s.drive * 2.0f, 0.5f);
    dryGain = 0.7f + 1.6f * s.drive;

    // Cabinets: speaker and box as filters (high pass, resonances, the cone's steep top end).
    cabinetCount = 0;
    auto add = [this] (int type, double f, double q, double g = 0.0) { cabinet[static_cast<size_t> (cabinetCount++)].set (rate, type, f, q, g); };
    switch (s.cabinet)
    {
        case 1: // British: closed 4x12, tight low resonance, aggressive upper mids, a steep roll-off
            add (Biquad::HighPass, 75.0, 0.8); add (Biquad::Peak, 110.0, 1.2, 4.0); add (Biquad::Peak, 420.0, 1.0, -3.0);
            add (Biquad::Peak, 2200.0, 1.4, 4.5); add (Biquad::LowPass, 5200.0, 0.9); add (Biquad::LowPass, 5800.0, 0.7);
            break;
        case 2: // Combo: open-back 1x12, lighter lows, smooth
            add (Biquad::HighPass, 100.0, 0.7); add (Biquad::Peak, 200.0, 0.9, 2.0); add (Biquad::Peak, 1500.0, 1.0, 2.0);
            add (Biquad::LowPass, 6500.0, 0.7); add (Biquad::LowPass, 7500.0, 0.6);
            break;
        case 3: // StaX: heavy stack, deep lows, scooped mids, a big high boost
            add (Biquad::HighPass, 65.0, 0.8); add (Biquad::Peak, 100.0, 1.0, 5.0); add (Biquad::Peak, 750.0, 0.8, -5.0);
            add (Biquad::HighShelf, 3000.0, 0.7, 7.0); add (Biquad::LowPass, 7500.0, 1.1); add (Biquad::LowPass, 8500.0, 0.7);
            break;
        default: break; // Dry
    }
}

float Amp::stage (int i, float x) noexcept
{
    const auto k = static_cast<size_t> (i);
    x = stageHp[k].process (x);
    // The first stage is slightly asymmetric (even harmonics, as a single-ended triode).
    const float bias = i == 0 ? 0.15f : 0.0f;
    x = sat[k].process (x * gains[k] + bias) - std::tanh (bias);
    x = dc[k].process (x);
    return stageLp[k].process (x);
}

float Amp::eq (float x) noexcept
{
    return highShelf.process (midPeak.process (lowShelf.process (x)));
}

float Amp::cab (float x) noexcept
{
    for (int i = 0; i < cabinetCount; ++i)
        x = cabinet[static_cast<size_t> (i)].process (x);
    return x;
}

float Amp::process (float x) noexcept
{
    switch (settings.type)
    {
        case 0: // Dry
            return cab (eq (x * dryGain));
        case 2: // EQ, then distortion
            x = presenceShelf.process (eq (x));
            for (int i = 0; i < 3; ++i)
                x = stage (i, x);
            return cab (x * makeup);
        case 3: // Bandpass
            for (int i = 0; i < 3; ++i)
            {
                const auto k = static_cast<size_t> (i);
                x = stageBand[k].process (x) * 2.0f;
                x = sat[k].process (x * gains[k]);
            }
            return cab (eq (x * makeup));
        default: // Tube
            x = presenceShelf.process (x);
            for (int i = 0; i < 3; ++i)
                x = stage (i, x);
            return cab (eq (x * makeup));
    }
}
} // namespace sunriff
