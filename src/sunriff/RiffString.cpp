#include "sunriff/RiffString.h"

namespace sunriff
{
namespace
{
enum class Pluck { Noise, Pick, Finger, Slap };

struct TypeModel
{
    Pluck pluck;
    float brightness;  // the loop filter's second tap (0 bright .. 0.5 the classic Karplus-Strong average)
    float stiffness;   // the allpass coefficient (more negative: stiffer, more inharmonic)
    float widthMs;     // the pick's contact time
    float pluckPos;    // where the string is plucked (fraction of its length from the bridge)
    float gain, sustain, slapBias;
};

const TypeModel& model (StringType t)
{
    static const TypeModel models[] = {
        { Pluck::Noise, 0.50f, 0.00f, 0.0f, 0.00f, 0.9f, 1.0f, 0.0f },   // Noise: the classic algorithm
        { Pluck::Pick, 0.12f, -0.10f, 0.6f, 0.13f, 1.0f, 1.0f, 0.0f },   // 6String1: a round electric string
        { Pluck::Pick, 0.06f, -0.22f, 0.35f, 0.10f, 1.0f, 1.25f, 0.0f }, // 6String2: brighter, steel, longer
        { Pluck::Pick, 0.22f, -0.06f, 1.1f, 0.18f, 1.05f, 0.85f, 0.0f }, // 6String3: warmer, flat-wound
        { Pluck::Slap, 0.10f, -0.38f, 0.55f, 0.07f, 1.5f, 0.8f, 0.45f },// Slap: slap bass
        { Pluck::Pick, 0.15f, -0.30f, 0.5f, 0.12f, 1.1f, 1.3f, 0.1f },   // EBass: a hard-played electric bass
        { Pluck::Finger, 0.30f, -0.14f, 2.6f, 0.22f, 0.65f, 1.6f, 0.0f }, // Fretless
    };
    return models[std::clamp (static_cast<int> (t), 0, 6)];
}
} // namespace

void StringVoice::prepare (double sampleRate) noexcept
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    fretFilter.setCutoff (rate, 2500.0);
    active = false;
    line.clear();
    heard.clear();
}

void StringVoice::start (int newNote, float velocity, const StringSettings& s, std::uint32_t seed) noexcept
{
    settings = s;
    const auto& m = model (s.type);
    noise.state = seed | 1u;
    hold = s.hold;
    note = newNote;
    pitch = glideTarget = newNote;
    glideLeft = 0;
    const float vel = std::clamp (velocity, 0.0f, 1.0f);
    const float v = 1.0f - s.velSens + s.velSens * vel; // what velocity does, as much as Vel asks

    if (active)
    {
        // A string plucked again while it rings: the pick stops most of the old vibration first.
        for (auto& x : line.data)
            x *= 0.2f;
        for (auto& x : heard.data)
            x *= 0.2f;
    }
    else
    {
        line.clear();
        heard.clear();
        stiffness.reset();
        prevY = 0.0f;
    }
    stiffness.c = m.stiffness;

    // Decay: Damping (with velocity, by Damping Vel) sets the ring time; higher notes ring shorter, as on a guitar.
    const float f0 = static_cast<float> (noteToHz (newNote));
    const float damping = std::clamp (s.damping - s.dampVel * (vel - 0.5f) * 0.6f, 0.0f, 1.0f);
    t60 = 0.12f * std::pow (100.0f, 1.0f - damping) * m.sustain * std::clamp (std::pow (110.0f / f0, 0.35f), 0.35f, 2.0f);
    loopB = std::clamp (m.brightness + 0.18f * damping - 0.08f * (v - 0.5f), 0.0f, 0.5f);
    releaseT60 = 0.07f;

    // The excitation: the shape of the pluck, combed by where it happens on the string.
    const float per = static_cast<float> (rate) / f0;
    const float amp = 0.45f * m.gain * (0.12f + 0.88f * std::pow (v, 1.4f));
    excitePos = 0;
    if (m.pluck == Pluck::Noise)
    {
        exciteLength = std::clamp (static_cast<int> (per), 2, static_cast<int> (excite.size()));
        OnePole lp;
        lp.setCutoff (rate, 800.0 + 9000.0 * v);
        float mean = 0.0f;
        for (int i = 0; i < exciteLength; ++i)
            mean += (excite[static_cast<size_t> (i)] = lp.lowpass (noise.next()) * 2.0f * amp);
        mean /= static_cast<float> (exciteLength);
        for (int i = 0; i < exciteLength; ++i)
            excite[static_cast<size_t> (i)] -= mean;
    }
    else
    {
        // A harder pick (and a harder hit) is a shorter contact: more high harmonics.
        const float width = std::max (2.0f, m.widthMs * 0.001f * static_cast<float> (rate) * (1.6f - v));
        const int w = static_cast<int> (width);
        const int pos = std::max (1, static_cast<int> (m.pluckPos * per));
        exciteLength = std::min (w + pos + 1, static_cast<int> (excite.size()));
        for (int i = 0; i < exciteLength; ++i)
        {
            const auto pulse = [&] (int k) { return k >= 0 && k < w ? std::sin (kPi * (static_cast<float> (k) + 0.5f) / width) : 0.0f; };
            excite[static_cast<size_t> (i)] = amp * (pulse (i) - pulse (i - pos)) + 0.02f * amp * noise.next();
        }
        if (m.pluck == Pluck::Slap) // the thumb's click
            for (int i = 0; i < std::min (exciteLength, 24); ++i)
                excite[static_cast<size_t> (i)] += amp * 0.35f * noise.next() * (1.0f - static_cast<float> (i) / 24.0f);
    }

    // Slap: the string hits the frets (a nonlinearity for the first 150 ms), stronger with velocity.
    slapAmount = std::clamp (s.slap * vel * vel + m.slapBias * s.slap * 2.0f, 0.0f, 1.0f);
    buzzThreshold = slapAmount > 0.0f ? amp * (1.0f - 0.8f * slapAmount) + 1.0e-3f : 10.0f;
    buzzTime = slapAmount > 0.0f ? 0.15f * static_cast<float> (rate) : 0.0f;

    // Fret noise: the finger and the pick on the wound string.
    fretEnv = s.fret * (0.3f + 0.7f * v) * 0.12f;
    fretDecay = static_cast<float> (std::exp (-1.0 / (0.025 * rate)));

    // The pickup: from just by the bridge to by the neck. A humbucker (Double) hears two points of the string.
    const float place = 0.04f + 0.30f * std::clamp (s.pickup, 0.0f, 1.0f);
    pickupA = s.coil == Coil::None ? 0.0f : place;
    pickupB = s.coil == Coil::Double ? place + 0.07f : -1.0f;

    active = true;
    released = false;
    keyDown = true;
    energy = amp;
    quietTime = 0.0f;
    killGain = 1.0f;
    killStep = 0.0f;
    lastBend = 1.0e9f;
    updatePitch (0.0f);
}

void StringVoice::glideTo (int newNote, int samples) noexcept
{
    note = newNote;
    glideTarget = newNote;
    if (samples <= 0)
    {
        pitch = newNote;
        glideLeft = 0;
    }
    else
    {
        glideStep = (glideTarget - pitch) / samples;
        glideLeft = samples;
    }
    keyDown = true;
    if (released)
    {
        released = false;
        loopB = model (settings.type).brightness;
    }
    lastBend = 1.0e9f; // recompute
}

void StringVoice::release() noexcept
{
    keyDown = false;
    if (hold || ! active)
        return;
    released = true;
    loopB = std::max (loopB, 0.35f); // the fingers lift off: the string is muted, and dull
    fretEnv += settings.fret * 0.02f;
    updateLoop (releaseT60);
}

void StringVoice::kill() noexcept
{
    if (active)
        killStep = 1.0f / std::max (1.0f, 0.003f * static_cast<float> (rate));
}

void StringVoice::updateLoop (float t) noexcept
{
    const double f0 = rate / std::max (2.0f, period);
    const double w = 2.0 * 3.14159265358979 * f0 / rate;
    const double perPeriod = std::exp (-3.0 * std::log (10.0) / (std::max (0.01f, t) * f0));
    const double re = (1.0 - loopB) + loopB * std::cos (w), im = loopB * std::sin (w);
    const double firGain = std::sqrt (re * re + im * im);
    loopGain = static_cast<float> (std::min (0.99995, perPeriod / std::max (1.0e-3, firGain)));
}

void StringVoice::updatePitch (float bend) noexcept
{
    lastBend = bend;
    const double f0 = noteToHz (pitch + bend);
    const double per = std::clamp (rate / f0, 4.0, static_cast<double> (kDelay - 16));
    const double w = 2.0 * 3.14159265358979 * f0 / rate;
    const double firDelay = std::atan2 (loopB * std::sin (w), (1.0 - loopB) + loopB * std::cos (w)) / w;
    const double apDelay = Allpass1::phaseDelay (stiffness.c, w);
    period = static_cast<float> (per);
    delay = static_cast<float> (std::max (2.0, per - firDelay - apDelay));
    pickupATaps = pickupA * period;
    pickupBTaps = pickupB > 0.0f ? pickupB * period : -1.0f;
    updateLoop (released ? releaseT60 : t60);
}

void StringVoice::render (float* out, const float* feedback, float feedbackGain, int n, float bend) noexcept
{
    if (! active)
        return;
    bool repitch = std::abs (bend - lastBend) > 1.0e-4f;
    if (glideLeft > 0)
    {
        const int k = std::min (n, glideLeft);
        pitch += glideStep * k;
        glideLeft -= k;
        if (glideLeft == 0)
            pitch = glideTarget;
        repitch = true;
    }
    if (repitch)
        updatePitch (bend);

    for (int i = 0; i < n; ++i)
    {
        float in = 0.0f;
        if (excitePos < exciteLength)
            in = excite[static_cast<size_t> (excitePos++)];
        // The amp's sound reaches the pickup only through the string (as on a real guitar): it is pushed into the loop, but the
        // pickup hears it a round trip later, shaped by the string's resonances. A muted string does not feed back.
        const float fb = feedback != nullptr && ! released ? feedback[i] * feedbackGain : 0.0f;
        const float y = line.read (delay);
        float s = loopGain * ((1.0f - loopB) * y + loopB * prevY);
        prevY = y;
        s = stiffness.process (s);
        if (buzzTime > 0.0f)
        {
            buzzTime -= 1.0f;
            const float a = std::abs (s);
            if (a > buzzThreshold)
                s = std::copysign (buzzThreshold + (a - buzzThreshold) * 0.3f, s);
        }
        s += in;
        line.push (s + fb);
        heard.push (s);

        float p = s;
        if (pickupATaps >= 1.0f)
        {
            p = s - heard.read (pickupATaps);
            if (pickupBTaps > 0.0f)
                p = 0.5f * (p + s - heard.read (pickupBTaps));
        }
        if (fretEnv > 1.0e-5f)
        {
            p += fretFilter.highpass (noise.next()) * fretEnv;
            fretEnv *= fretDecay;
        }
        if (killStep > 0.0f)
        {
            killGain -= killStep;
            if (killGain <= 0.0f)
            {
                active = false;
                return;
            }
        }
        out[i] += p * killGain;
        energy = std::max (std::abs (s), energy * 0.9995f);
    }
    if (energy < 1.0e-4f && excitePos >= exciteLength)
    {
        quietTime += static_cast<float> (n);
        if (quietTime > 2.0f * period + 64.0f)
            active = false;
    }
    else
        quietTime = 0.0f;
}
} // namespace sunriff
