#include "sunriff/RiffFx.h"

namespace sunriff
{
const juce::StringArray& fxNames()
{
    static const juce::StringArray names { "Dry", "AMP: Phaser", "AMP: Tremolo", "AMP: WahWah", "AMP: WahWah LFO", "AMP: Ringmod",
                                           "AMP: Fuzz Box", "AMP: Harmonizer", "AMP: Talkbox", "MST: Chorus", "MST: Flanger",
                                           "MST: Phaser", "MST: Leslie", "MST: Tremolo", "MST: DubDelay", "MST: Multitap" };
    return names;
}

std::pair<juce::String, juce::String> fxParamNames (int type)
{
    switch (type)
    {
        case FxPhaser: case FxStereoPhaser: return { "LFO speed", "Feedback" };
        case FxTremolo: case FxStereoTremolo: return { "Rate (sync)", "Depth" };
        case FxWah: return { "Attack", "Depth" };
        case FxWahLfo: return { "LFO speed", "Depth" };
        case FxRingmod: return { "Harmonic", "Mix" };
        case FxFuzz: return { "Fuzz", "Digital" };
        case FxHarmonizer: return { "Octave up", "Octave down" };
        case FxTalkbox: return { "LFO speed", "Formant" };
        case FxChorus: case FxFlanger: return { "LFO speed", "Depth" };
        case FxLeslie: return { "Rotation", "Depth" };
        case FxDubDelay: return { "Time", "Feedback" };
        case FxMultitap: return { "Time (sync)", "Feedback" };
        default: return { {}, {} };
    }
}

const juce::StringArray& fxDivisionNames()
{
    static const juce::StringArray names { "1 bar", "1/2", "1/4.", "1/4", "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    return names;
}

double fxDivisionBeats (float param, int& index) noexcept
{
    static constexpr double beats[] { 4.0, 2.0, 1.5, 1.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    index = std::clamp (static_cast<int> (param * 9.999f), 0, 9);
    return beats[index];
}

void FxUnit::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    int size = 1;
    while (size < static_cast<int> (rate * 4.5)) // 4 s of delay (a bar at 60 BPM) and a little more
        size <<= 1;
    delayL.assign (static_cast<size_t> (size), 0.0f);
    delayR.assign (static_cast<size_t> (size), 0.0f);
    delayMask = size - 1;
    upDc.setup (rate, 20.0);
    upHp.setCutoff (rate, 120.0);
    downLp.setCutoff (rate, 900.0);
    inputLp.setCutoff (rate, 700.0);
    downEnv.setCutoff (rate, 30.0);
    fbFilterL.setCutoff (rate, 3000.0);
    fbFilterR.setCutoff (rate, 3000.0);
    leslieLow.setCutoff (rate, 800.0);
    reset();
}

void FxUnit::reset() noexcept
{
    for (auto& set : phaseStages)
        for (auto& s : set)
            s.reset();
    phaseFeedback = {};
    wah.reset();
    formant1.reset();
    formant2.reset();
    follower = 0.0f;
    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    writePos = 0;
}

void FxUnit::configure (const FxParams& p) noexcept
{
    if (p.type != params.type)
    {
        params = p;
        reset();
    }
    params = p;
    const float p1 = std::clamp (p.p1, 0.0f, 1.0f), p2 = std::clamp (p.p2, 0.0f, 1.0f);
    depth = p2;
    switch (p.type)
    {
        case FxPhaser: case FxStereoPhaser: case FxWahLfo: case FxChorus:
            lfoHz = 0.05f * std::pow (120.0f, p1); // 0.05 .. 6 Hz
            feedbackAmount = 0.85f * p2;
            break;
        case FxFlanger: lfoHz = 0.03f * std::pow (80.0f, p1); break;
        case FxTalkbox: lfoHz = 0.1f * std::pow (60.0f, p1); break;
        case FxTremolo: case FxStereoTremolo:
        {
            int index = 0;
            lfoHz = static_cast<float> (p.bpm / 60.0 / fxDivisionBeats (p1, index));
            break;
        }
        case FxWah:
            followerAttack = 1.0f - std::exp (-1.0f / ((0.001f + 0.08f * (1.0f - p1)) * static_cast<float> (rate)));
            followerRelease = 1.0f - std::exp (-1.0f / (0.15f * static_cast<float> (rate)));
            break;
        case FxRingmod:
        {
            static constexpr double harmonics[] { 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0 };
            ringHz = noteHz * harmonics[std::clamp (static_cast<int> (p1 * 9.999f), 0, 9)];
            break;
        }
        case FxLeslie: leslieTarget = p1 < 0.5f ? 0.8f : 6.7f; break; // chorale and tremolo speeds
        default: break;
    }
}

void FxUnit::noteOn (double hz) noexcept
{
    noteHz = hz;
    if (params.type == FxTremolo || params.type == FxStereoTremolo || params.type == FxWahLfo)
        lfo.phase = 0.0;
    if (params.type == FxRingmod)
        configure (params);
}

float FxUnit::phaser (float x, int which, float lfoValue) noexcept
{
    // Six first-order allpasses swept between 200 Hz and 2.4 kHz.
    const double f = 200.0 * std::pow (12.0, 0.5 * (lfoValue + 1.0));
    const double t = std::tan (3.14159265358979 * f / rate);
    const float c = static_cast<float> ((t - 1.0) / (t + 1.0));
    auto& stages = phaseStages[static_cast<size_t> (which)];
    float y = x + phaseFeedback[static_cast<size_t> (which)] * feedbackAmount;
    for (auto& s : stages)
    {
        s.c = c;
        y = s.process (y);
    }
    phaseFeedback[static_cast<size_t> (which)] = std::clamp (y, -2.0f, 2.0f);
    return 0.5f * (x + y);
}

float FxUnit::readDelay (const std::vector<float>& buf, int write, float delay) const noexcept
{
    delay = std::clamp (delay, 1.0f, static_cast<float> (delayMask - 2));
    const int d = static_cast<int> (delay);
    const float f = delay - static_cast<float> (d);
    const float a = buf[static_cast<size_t> ((write - d) & delayMask)], b = buf[static_cast<size_t> ((write - d - 1) & delayMask)];
    return a + (b - a) * f;
}

float FxUnit::pre (float x) noexcept
{
    switch (params.type)
    {
        case FxPhaser: return phaser (x, 0, lfo.triangle (lfoHz, rate));
        case FxTremolo:
            return x * (1.0f - depth * 0.5f * (1.0f + lfo.sine (lfoHz, rate)));
        case FxWah: case FxWahLfo:
        {
            float sweep;
            if (params.type == FxWah)
            {
                const float a = std::min (1.0f, std::abs (x) * 6.0f);
                follower += (a > follower ? followerAttack : followerRelease) * (a - follower);
                sweep = follower;
            }
            else
                sweep = 0.5f * (1.0f + lfo.sine (lfoHz, rate));
            if (++filterCounter >= 8)
            {
                filterCounter = 0;
                wah.set (rate, Biquad::LowPass, 350.0 * std::pow (8.0, static_cast<double> (sweep * depth)), 5.0);
            }
            return wah.process (x) * 0.8f;
        }
        case FxRingmod:
        {
            const float m = ringOsc.sine (ringHz, rate);
            return x * (1.0f - depth) + x * m * depth * 1.4f;
        }
        case FxFuzz:
        {
            const float p1 = std::clamp (params.p1, 0.0f, 1.0f);
            float y = x * (1.0f + p1 * p1 * 150.0f);
            y = std::clamp (y, -1.0f, 1.0f);
            y = y * (1.5f - 0.5f * y * y); // a rounded top on the clipped wave
            // digital: fewer bits and a lower sample rate
            if (depth > 0.01f)
            {
                const float levels = std::pow (2.0f, 12.0f - 10.0f * depth);
                y = std::round (y * levels) / levels;
                const int hold = 1 + static_cast<int> (depth * 15.0f);
                if (++holdCount >= hold)
                {
                    holdCount = 0;
                    holdSample = y;
                }
                y = holdSample;
            }
            return y * 0.35f;
        }
        case FxHarmonizer:
        {
            // An octave up: the rectified wave (twice the frequency). An octave down: a flip-flop on the low-passed wave.
            const float up = upHp.highpass (upDc.process (std::abs (x)));
            const float l = inputLp.lowpass (x);
            if (lastLp <= 0.0f && l > 0.0f)
                flip = -flip;
            lastLp = l;
            const float env = downEnv.lowpass (std::abs (x));
            const float down = downLp.lowpass (flip * env * 1.6f);
            return x + std::clamp (params.p1, 0.0f, 1.0f) * 1.5f * up + depth * 1.5f * down;
        }
        case FxTalkbox:
        {
            if (++filterCounter >= 8)
            {
                filterCounter = 0;
                // A, E, I, O, U and back: the LFO walks along the vowels; Formant moves them up or down.
                static constexpr float f1[] { 730, 530, 270, 570, 300, 730 }, f2[] { 1090, 1840, 2290, 840, 870, 1090 };
                const float pos = 0.5f * (1.0f + lfo.sine (lfoHz, rate / 8.0)) * 4.999f;
                const int i = static_cast<int> (pos);
                const float t = pos - static_cast<float> (i);
                const float shift = std::pow (2.0f, (depth - 0.5f) * 1.4f);
                formant1.set (rate, Biquad::BandPass, (f1[i] + (f1[i + 1] - f1[i]) * t) * shift, 6.0);
                formant2.set (rate, Biquad::BandPass, (f2[i] + (f2[i + 1] - f2[i]) * t) * shift, 8.0);
            }
            return 2.2f * (formant1.process (x) + 0.7f * formant2.process (x));
        }
        default: return x;
    }
}

void FxUnit::post (float x, float& left, float& right) noexcept
{
    left = right = x;
    const auto ms = static_cast<float> (rate / 1000.0);
    switch (params.type)
    {
        case FxChorus: case FxFlanger:
        {
            const float a = lfo.sine (lfoHz, rate);
            lfo2.phase = lfo.phase + 0.25 - std::floor (lfo.phase + 0.25);
            const float b = std::sin (2.0f * kPi * static_cast<float> (lfo2.phase));
            float dl, dr, fb = 0.0f;
            if (params.type == FxChorus)
            {
                dl = (12.0f + 6.0f * depth * a) * ms;
                dr = (12.0f + 6.0f * depth * b) * ms;
            }
            else
            {
                dl = (0.6f + 4.0f * depth * (0.5f + 0.5f * a)) * ms;
                dr = (0.6f + 4.0f * depth * (0.5f + 0.5f * b)) * ms;
                fb = 0.65f;
            }
            const float wl = readDelay (delayL, writePos, dl), wr = readDelay (delayR, writePos, dr);
            delayL[static_cast<size_t> (writePos)] = x + wl * fb;
            delayR[static_cast<size_t> (writePos)] = x + wr * fb;
            writePos = (writePos + 1) & delayMask;
            left = 0.7f * (x + wl);
            right = 0.7f * (x + wr);
            return;
        }
        case FxStereoPhaser:
        {
            const float a = lfo.triangle (lfoHz, rate);
            const float b = 1.0f - 4.0f * std::abs (static_cast<float> (lfo.phase + 0.5 - std::floor (lfo.phase + 0.5)) - 0.5f);
            left = phaser (x, 0, a);
            right = phaser (x, 1, b);
            return;
        }
        case FxStereoTremolo:
        {
            const float g = 1.0f - depth * 0.5f * (1.0f + lfo.sine (lfoHz, rate));
            left = right = x * g;
            return;
        }
        case FxLeslie:
        {
            // A horn (highs) and a drum (lows), each turning; the horn speeds up and slows down with inertia.
            leslieSpeed += (leslieTarget - leslieSpeed) * (leslieTarget > leslieSpeed ? 2.0e-5f : 1.0e-5f) * (48000.0f / static_cast<float> (rate));
            const float low = leslieLow.lowpass (x), high = x - low;
            const float horn = lfo.sine (leslieSpeed, rate);
            const float drum = std::sin (2.0f * kPi * static_cast<float> (lfo2.phase));
            lfo2.advance (leslieSpeed * 0.85, rate);
            const float d = (1.0f + 0.5f * depth * horn) * 0.5f * ms; // the doppler of the horn
            delayL[static_cast<size_t> (writePos)] = high;
            writePos = (writePos + 1) & delayMask;
            const float moved = readDelay (delayL, writePos, d + 2.0f);
            const float am = 1.0f - 0.45f * depth * (1.0f - horn) * 0.5f;
            const float lowAm = 1.0f - 0.25f * depth * (1.0f - drum) * 0.5f;
            const float pan = 0.5f + 0.4f * depth * horn;
            left = low * lowAm + moved * am * (1.0f - pan) * 1.4f;
            right = low * lowAm * 1.0f + moved * am * pan * 1.4f;
            return;
        }
        case FxDubDelay: case FxMultitap:
        {
            float time;
            if (params.type == FxDubDelay)
                time = (60.0f + 640.0f * std::clamp (params.p1, 0.0f, 1.0f)) * ms; // 60 .. 700 ms
            else
            {
                int index = 0;
                time = static_cast<float> (fxDivisionBeats (params.p1, index) * 60.0 / std::max (20.0, params.bpm) * rate);
                time = std::min (time, static_cast<float> (delayMask - 8) / 4.0f); // four taps must fit
            }
            const float fb = 0.85f * depth;
            if (params.type == FxDubDelay)
            {
                // Left after `time`, right after twice that; each repeat crosses over (ping-pong).
                const float l = readDelay (delayL, writePos, time), r = readDelay (delayR, writePos, time);
                delayL[static_cast<size_t> (writePos)] = x + fbFilterR.lowpass (r) * fb;
                delayR[static_cast<size_t> (writePos)] = fbFilterL.lowpass (l);
                writePos = (writePos + 1) & delayMask;
                left = x + 0.6f * l;
                right = x + 0.6f * r;
            }
            else
            {
                // Four taps a division apart, alternating left and right, the last one fed back through a filter.
                const float t1 = readDelay (delayL, writePos, time), t2 = readDelay (delayL, writePos, 2.0f * time),
                            t3 = readDelay (delayL, writePos, 3.0f * time), t4 = readDelay (delayL, writePos, 4.0f * time);
                delayL[static_cast<size_t> (writePos)] = x + fbFilterL.lowpass (t4) * fb;
                writePos = (writePos + 1) & delayMask;
                left = x + 0.55f * t1 + 0.3f * t3;
                right = x + 0.45f * t2 + 0.25f * t4;
            }
            return;
        }
        default: return;
    }
}
} // namespace sunriff
