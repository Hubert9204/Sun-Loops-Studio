#include "sunburst/Fx.h"

#include <array>

namespace sunburst
{
namespace
{
constexpr std::array<double, 8> kLineMs { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1 };
constexpr std::array<double, 2> kDiffuserMs { 4.77, 3.59 };
constexpr double kMaxDelaySeconds = 2.5;
constexpr double kChorusMs = 40.0;

inline float softLimit (float x) noexcept
{
    const float a = std::abs (x);
    if (a <= 0.8f)
        return x;
    const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
    return x < 0.0f ? -y : y;
}
} // namespace

void FxRack::prepare (double sampleRate)
{
    rate = sampleRate;
    chorusL.assign (static_cast<size_t> (kChorusMs * 0.001 * rate) + 4, 0.0f);
    chorusR.assign (chorusL.size(), 0.0f);
    delayL.assign (static_cast<size_t> (kMaxDelaySeconds * rate) + 4, 0.0f);
    delayR.assign (delayL.size(), 0.0f);
    predelay.assign (static_cast<size_t> (0.21 * rate) + 4, 0.0f);
    for (size_t d = 0; d < diffusers.size(); ++d)
        diffusers[d].assign (static_cast<size_t> (kDiffuserMs[d] * 0.001 * rate) + 1, 0.0f);
    for (size_t l = 0; l < lines.size(); ++l)
        lines[l].assign (static_cast<size_t> (kLineMs[l] * 0.001 * rate * 1.6) + 4, 0.0f); // room for the largest size
    reset();
}

void FxRack::reset() noexcept
{
    for (auto* v : { &chorusL, &chorusR, &delayL, &delayR, &predelay })
        std::fill (v->begin(), v->end(), 0.0f);
    for (auto& d : diffusers)
        std::fill (d.begin(), d.end(), 0.0f);
    for (auto& l : lines)
        std::fill (l.begin(), l.end(), 0.0f);
    lineDamp.fill (0.0f);
    for (auto* b : { &lowL, &lowR, &midL, &midR, &highL, &highR })
        b->reset();
    fxFilterL.reset();
    fxFilterR.reset();
    crushCounter = 0;
    crushHoldL = crushHoldR = toneL = toneR = 0.0f;
    delaySmoothed = 0.0;
}

float FxRack::readDelay (const std::vector<float>& buffer, int write, double delaySamples) noexcept
{
    const auto size = static_cast<int> (buffer.size());
    double pos = write - delaySamples;
    while (pos < 0.0)
        pos += size;
    const int i = static_cast<int> (pos);
    const float f = static_cast<float> (pos - i);
    const float a = buffer[static_cast<size_t> (i % size)], b = buffer[static_cast<size_t> ((i + 1) % size)];
    return a + f * (b - a);
}

void FxRack::process (float* left, float* right, int n, const FxSettings& s) noexcept
{
    // Distortion
    if (s.distOn && s.distMix > 0.0f)
    {
        const float drive = std::clamp (s.distDrive, 0.0f, 1.0f);
        const float gain = 1.0f + drive * drive * 30.0f;
        const float makeup = 1.0f / (1.0f + drive * 1.5f);
        const float bits = std::exp2 (16.0f - drive * 13.0f);
        const int hold = 1 + static_cast<int> (drive * 24.0f);
        const auto shape = [&s, gain, makeup, bits] (float x) noexcept
        {
            switch (s.distType)
            {
                case DistType::Soft: return std::tanh (x * gain) * makeup * 1.3f;
                case DistType::Hard: return std::clamp (x * gain, -1.0f, 1.0f) * makeup;
                case DistType::Tube: return (std::tanh (x * gain + 0.35f) - std::tanh (0.35f)) * makeup * 1.4f;
                case DistType::Fold: return std::sin (x * gain * 1.5707963f) * makeup * 1.2f;
                case DistType::Crush: return std::round (x * bits) / bits;
            }
            return x;
        };
        for (int i = 0; i < n; ++i)
        {
            float wl = shape (left[i]), wr = shape (right[i]);
            if (s.distType == DistType::Crush)
            {
                if (crushCounter++ % hold == 0)
                {
                    crushHoldL = wl;
                    crushHoldR = wr;
                }
                wl = crushHoldL;
                wr = crushHoldR;
            }
            left[i] += (wl - left[i]) * s.distMix;
            right[i] += (wr - right[i]) * s.distMix;
        }
    }

    // EQ
    if (s.eqOn)
    {
        lowL.lowShelf (rate, 110.0, s.eqLow);
        lowR.lowShelf (rate, 110.0, s.eqLow);
        midL.peak (rate, s.eqMidFreq, s.eqMid, 0.8);
        midR.peak (rate, s.eqMidFreq, s.eqMid, 0.8);
        highL.highShelf (rate, 7500.0, s.eqHigh);
        highR.highShelf (rate, 7500.0, s.eqHigh);
        for (int i = 0; i < n; ++i)
        {
            left[i] = highL.process (midL.process (lowL.process (left[i])));
            right[i] = highR.process (midR.process (lowR.process (right[i])));
        }
    }

    // Filter
    if (s.filterOn)
    {
        const auto type = s.filterType == 1 ? FilterType::HP12 : s.filterType == 2 ? FilterType::BP12 : FilterType::LP12;
        fxFilterL.set (type, s.filterCutoff, rate, s.filterResonance, 0.0f);
        fxFilterR.set (type, s.filterCutoff, rate, s.filterResonance, 0.0f);
        for (int i = 0; i < n; ++i)
        {
            left[i] = fxFilterL.process (left[i]);
            right[i] = fxFilterR.process (right[i]);
        }
    }

    // Chorus: three modulated taps per side, the sides in opposite phase.
    if (s.chorusOn && s.chorusMix > 0.0f)
    {
        const double inc = s.chorusRate / rate;
        const double baseMs = 11.0, depthMs = 1.0 + s.chorusDepth * 9.0;
        const auto size = static_cast<int> (chorusL.size());
        for (int i = 0; i < n; ++i)
        {
            chorusL[static_cast<size_t> (chorusWrite)] = left[i];
            chorusR[static_cast<size_t> (chorusWrite)] = right[i];
            float wl = 0.0f, wr = 0.0f;
            // The taps sit a third of a cycle apart, the right side half a cycle from the left: one sine and one cosine give all six
            // (sin (a + 120 deg) = -sin a / 2 + cos a x sqrt 3 / 2, and the right side is the negative).
            const double sa = std::sin (kTwoPi * chorusPhase), ca = std::cos (kTwoPi * chorusPhase);
            const std::array<double, 3> taps { sa, -0.5 * sa + 0.8660254037844386 * ca, -0.5 * sa - 0.8660254037844386 * ca };
            for (const double sine : taps)
            {
                const double ml = (baseMs + depthMs * 0.5 * (1.0 + sine)) * 0.001 * rate;
                const double mr = (baseMs + depthMs * 0.5 * (1.0 - sine)) * 0.001 * rate;
                wl += readDelay (chorusL, chorusWrite, ml);
                wr += readDelay (chorusR, chorusWrite, mr);
            }
            chorusPhase += inc;
            if (chorusPhase >= 1.0)
                chorusPhase -= 1.0;
            if (++chorusWrite == size)
                chorusWrite = 0;
            left[i] += (wl / 3.0f - left[i]) * s.chorusMix * 0.5f + (wl / 3.0f) * s.chorusMix * 0.5f;
            right[i] += (wr / 3.0f - right[i]) * s.chorusMix * 0.5f + (wr / 3.0f) * s.chorusMix * 0.5f;
        }
    }

    // Delay (stereo or ping-pong), with a tone control in the feedback.
    if (s.delayOn && s.delayMix > 0.0f)
    {
        const double target = std::clamp (s.delaySeconds, 0.001, kMaxDelaySeconds - 0.01) * rate;
        if (delaySmoothed <= 0.0)
            delaySmoothed = target;
        const float toneCoef = std::exp (-static_cast<float> (kTwoPi) * (800.0f + s.delayTone * s.delayTone * 17000.0f) / static_cast<float> (rate));
        const float fb = std::clamp (s.delayFeedback, 0.0f, 0.95f);
        const auto size = static_cast<int> (delayL.size());
        for (int i = 0; i < n; ++i)
        {
            delaySmoothed += (target - delaySmoothed) * 0.0005;
            const float dl = readDelay (delayL, delayWrite, delaySmoothed);
            const float dr = readDelay (delayR, delayWrite, delaySmoothed);
            toneL = dl + (toneL - dl) * toneCoef;
            toneR = dr + (toneR - dr) * toneCoef;
            if (s.delayPingPong)
            {
                delayL[static_cast<size_t> (delayWrite)] = 0.5f * (left[i] + right[i]) + toneR * fb;
                delayR[static_cast<size_t> (delayWrite)] = toneL * fb;
            }
            else
            {
                delayL[static_cast<size_t> (delayWrite)] = left[i] + toneL * fb;
                delayR[static_cast<size_t> (delayWrite)] = right[i] + toneR * fb;
            }
            if (++delayWrite == size)
                delayWrite = 0;
            left[i] += toneL * s.delayMix;
            right[i] += toneR * s.delayMix;
        }
    }

    // Reverb: pre-delay, two diffusers, then 8 delay lines mixed by a Hadamard matrix with damping in each line.
    if (s.reverbOn && s.reverbMix > 0.0f)
    {
        const double sizeScale = 0.55 + s.reverbSize * 1.0;
        const double rt60 = 0.3 + s.reverbSize * s.reverbSize * 9.0;
        std::array<float, 8> gains {};
        for (size_t l = 0; l < lines.size(); ++l)
        {
            lineLength[l] = std::min (static_cast<int> (lines[l].size()) - 1, static_cast<int> (kLineMs[l] * 0.001 * rate * sizeScale));
            gains[l] = static_cast<float> (std::pow (10.0, -3.0 * lineLength[l] / (rt60 * rate)));
        }
        const float damp = 0.05f + s.reverbDamping * 0.75f;
        const auto preSize = static_cast<int> (predelay.size());
        const int preSamples = std::clamp (static_cast<int> (s.reverbPredelayMs * 0.001f * static_cast<float> (rate)), 0, preSize - 2);
        const float width = std::clamp (s.reverbWidth, 0.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            predelay[static_cast<size_t> (predelayWrite)] = 0.5f * (left[i] + right[i]);
            int readPos = predelayWrite - preSamples;
            if (readPos < 0)
                readPos += preSize;
            float x = predelay[static_cast<size_t> (readPos)];
            if (++predelayWrite == preSize)
                predelayWrite = 0;
            for (size_t d = 0; d < diffusers.size(); ++d)
            {
                auto& buf = diffusers[d];
                auto& pos = diffuserPos[d];
                const float delayed = buf[static_cast<size_t> (pos)];
                const float in = x + delayed * 0.6f;
                buf[static_cast<size_t> (pos)] = in;
                x = delayed - in * 0.6f;
                if (++pos == static_cast<int> (buf.size()))
                    pos = 0;
            }
            std::array<float, 8> out {};
            for (size_t l = 0; l < lines.size(); ++l)
            {
                int r = linePos[l] - lineLength[l];
                if (r < 0)
                    r += static_cast<int> (lines[l].size());
                const float v = lines[l][static_cast<size_t> (r)];
                lineDamp[l] = v + (lineDamp[l] - v) * damp;
                out[l] = lineDamp[l] * gains[l];
            }
            // Hadamard 8 (normalised): three butterfly stages.
            std::array<float, 8> h = out;
            for (size_t step = 1; step < 8; step <<= 1)
                for (size_t a = 0; a < 8; a += 2 * step)
                    for (size_t b = a; b < a + step; ++b)
                    {
                        const float u = h[b], v = h[b + step];
                        h[b] = u + v;
                        h[b + step] = u - v;
                    }
            for (size_t l = 0; l < lines.size(); ++l)
            {
                lines[l][static_cast<size_t> (linePos[l])] = h[l] * 0.35355339f + x * ((l & 1u) ? -1.0f : 1.0f);
                if (++linePos[l] == static_cast<int> (lines[l].size()))
                    linePos[l] = 0;
            }
            const float wetL = (out[0] + out[2] + out[4] + out[6]) * 0.5f;
            const float wetR = (out[1] + out[3] + out[5] + out[7]) * 0.5f;
            const float mid = 0.5f * (wetL + wetR), side = 0.5f * (wetL - wetR) * width;
            left[i] += (mid + side) * s.reverbMix;
            right[i] += (mid - side) * s.reverbMix;
        }
    }

    for (int i = 0; i < n; ++i)
    {
        left[i] = softLimit (left[i] * s.masterGain);
        right[i] = softLimit (right[i] * s.masterGain);
    }
}
} // namespace sunburst
