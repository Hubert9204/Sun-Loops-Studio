#include "audio/Player.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace sls
{
namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr float kSynthGain = 0.25f;
constexpr float kSampleGain = 0.5f;
constexpr float kWaveNorm = 1.0f / 1.35f;
constexpr float kGainSmoothing = 0.002f; // per sample, about 10 ms at 48 kHz
constexpr float kUnsetGain = -1.0f;

double pitchToHz (int pitch) noexcept
{
    return 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
}
} // namespace

void Player::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    releaseStep = static_cast<float> (1.0 / (0.080 * sampleRate)); // 80 ms release

    for (auto& v : voices)
        v = Voice {};

    nextEvent.fill (0);
    smoothedGain.fill (kUnsetGain);
    peaks.fill (0.0f);

    tickPos = 0.0;
    projectRevision = ~std::uint64_t { 0 };
    ageCounter = 0;
    wasPlaying = false;
}

void Player::releaseVoices (bool includeSamplers) noexcept
{
    for (auto& v : voices)
        if ((v.state == State::Attack || v.state == State::Sustain) && (includeSamplers || ! v.sampler))
            v.state = State::Release;
}

void Player::onProjectChanged (const Project& project) noexcept
{
    // First note of each channel that has not started yet at the current position.
    for (size_t ch = 0; ch < project.channels.size() && ch < static_cast<size_t> (kMaxChannels); ++ch)
    {
        const auto& notes = project.channels[ch].notes;
        const auto it = std::lower_bound (notes.begin(), notes.end(), tickPos,
                                          [] (const Note& n, double t) { return static_cast<double> (n.startTick) < t; });
        nextEvent[ch] = static_cast<size_t> (it - notes.begin());
    }

    // Sample data belongs to the snapshot and may be freed once we have moved on, so a sampler
    // voice may only survive if the new snapshot still owns the very same sample.
    for (auto& v : voices)
    {
        if (v.state == State::Idle || ! v.sampler)
            continue;

        const bool stillValid = static_cast<size_t> (v.channel) < project.channels.size()
                             && project.channels[static_cast<size_t> (v.channel)].sample.get() == v.sample;
        if (! stillValid)
            v = Voice {};
    }
}

void Player::noteOn (const Channel& channel, int channelIndex, const Note& n, int loopLength) noexcept
{
    const bool isSampler = channel.instrument == Instrument::Sampler;
    if (isSampler && (channel.sample == nullptr || channel.sample->numFrames() < 2))
        return;

    Voice* target = nullptr;

    for (auto& v : voices)
        if (v.state == State::Idle) { target = &v; break; }

    if (target == nullptr)
    {
        // Steal: oldest releasing voice first, otherwise the oldest voice.
        for (auto& v : voices)
            if (v.state == State::Release && (target == nullptr || v.age < target->age))
                target = &v;
    }
    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.age < target->age)
                target = &v;
    }

    Voice v;
    v.state = State::Attack;
    v.sampler = isSampler;
    v.channel = channelIndex;
    v.velocity = n.velocity;
    v.env = 0.0f;
    v.age = ++ageCounter;

    if (isSampler)
    {
        v.endTick = INT_MAX; // one-shot: plays to the end of the sample
        v.sample = channel.sample.get();
        v.pos = 0.0;
        v.step = std::pow (2.0, (n.pitch - channel.rootPitch) / 12.0) * v.sample->sampleRate / sampleRate;
        v.attackStep = static_cast<float> (1.0 / (0.0005 * sampleRate)); // 0.5 ms
    }
    else
    {
        v.endTick = std::min (n.startTick + n.lengthTicks, loopLength);
        v.phase = 0.0;
        v.increment = kTwoPi * pitchToHz (n.pitch) / sampleRate;
        v.attackStep = static_cast<float> (1.0 / (0.005 * sampleRate)); // 5 ms
    }

    *target = v;
}

void Player::render (const Project& project, double bpm, bool playing,
                     float* const* out, int numChannels, int numSamples) noexcept
{
    bpm = std::clamp (bpm, 20.0, 400.0);
    const double ticksPerSample = bpm * kTicksPerBeat / (60.0 * sampleRate);
    const int loopLength = std::max (1, project.lengthTicks);
    const size_t numMixerChannels = std::min (project.channels.size(), static_cast<size_t> (kMaxChannels));

    if (project.revision != projectRevision)
    {
        projectRevision = project.revision;
        onProjectChanged (project);
    }

    if (! playing && wasPlaying)
    {
        releaseVoices (releaseSamplesOnStop);
        tickPos = 0.0;
        nextEvent.fill (0);
    }
    wasPlaying = playing;

    // Per-block mixer state: target gain and pan gains for every channel.
    bool anySolo = false;
    for (size_t ch = 0; ch < numMixerChannels; ++ch)
        anySolo = anySolo || project.channels[ch].solo;

    std::array<float, kMaxChannels> targetGain {};
    std::array<float, kMaxChannels> panLeft {};
    std::array<float, kMaxChannels> panRight {};

    for (size_t ch = 0; ch < numMixerChannels; ++ch)
    {
        const auto& c = project.channels[ch];
        const bool audible = anySolo ? c.solo : ! c.mute;
        targetGain[ch] = audible ? c.volume : 0.0f;

        const double angle = (static_cast<double> (c.pan) + 1.0) * kTwoPi / 8.0; // 0..pi/2
        panLeft[ch] = static_cast<float> (std::cos (angle));
        panRight[ch] = static_cast<float> (std::sin (angle));

        if (smoothedGain[ch] < 0.0f)
            smoothedGain[ch] = targetGain[ch]; // no fade-in on the very first block
    }

    peaks.fill (0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        if (playing)
        {
            for (size_t ch = 0; ch < numMixerChannels; ++ch)
            {
                const auto& notes = project.channels[ch].notes;
                auto& next = nextEvent[ch];
                while (next < notes.size() && static_cast<double> (notes[next].startTick) <= tickPos)
                {
                    noteOn (project.channels[ch], static_cast<int> (ch), notes[next], loopLength);
                    ++next;
                }
            }

            for (auto& v : voices)
                if ((v.state == State::Attack || v.state == State::Sustain)
                    && tickPos >= static_cast<double> (v.endTick))
                    v.state = State::Release;
        }

        std::array<float, kMaxChannels> busL {};
        std::array<float, kMaxChannels> busR {};

        for (auto& v : voices)
        {
            if (v.state == State::Idle)
                continue;

            float l = 0.0f, r = 0.0f;

            if (v.sampler)
            {
                const int idx = static_cast<int> (v.pos);
                if (idx + 1 >= v.sample->numFrames())
                {
                    v = Voice {};
                    continue;
                }
                const float frac = static_cast<float> (v.pos - idx);
                const float* sl = v.sample->left.data();
                const float* sr = v.sample->right.data();
                const float gain = v.env * v.velocity * kSampleGain;
                l = (sl[idx] + frac * (sl[idx + 1] - sl[idx])) * gain;
                r = (sr[idx] + frac * (sr[idx + 1] - sr[idx])) * gain;
                v.pos += v.step;
            }
            else
            {
                const double p = v.phase;
                const float wave = static_cast<float> (std::sin (p) + 0.3 * std::sin (2.0 * p) + 0.12 * std::sin (3.0 * p));
                l = r = wave * kWaveNorm * v.env * v.velocity * kSynthGain;
                v.phase += v.increment;
                if (v.phase >= kTwoPi)
                    v.phase -= kTwoPi;
            }

            busL[static_cast<size_t> (v.channel)] += l;
            busR[static_cast<size_t> (v.channel)] += r;

            if (v.state == State::Attack)
            {
                v.env += v.attackStep;
                if (v.env >= 1.0f) { v.env = 1.0f; v.state = State::Sustain; }
            }
            else if (v.state == State::Release)
            {
                v.env -= releaseStep;
                if (v.env <= 0.0f)
                    v = Voice {};
            }
        }

        float mixL = 0.0f, mixR = 0.0f;
        for (size_t ch = 0; ch < numMixerChannels; ++ch)
        {
            smoothedGain[ch] += (targetGain[ch] - smoothedGain[ch]) * kGainSmoothing;
            const float l = busL[ch] * smoothedGain[ch] * panLeft[ch];
            const float r = busR[ch] * smoothedGain[ch] * panRight[ch];
            peaks[ch] = std::max ({ peaks[ch], std::fabs (l), std::fabs (r) });
            mixL += l;
            mixR += r;
        }

        const float outL = std::tanh (mixL);
        const float outR = std::tanh (mixR);
        for (int ch = 0; ch < numChannels; ++ch)
            if (out[ch] != nullptr)
                out[ch][i] += (ch & 1) == 0 ? outL : outR;

        if (playing)
        {
            tickPos += ticksPerSample;
            if (tickPos >= static_cast<double> (loopLength))
            {
                tickPos = std::fmod (tickPos, static_cast<double> (loopLength));
                nextEvent.fill (0);
                releaseVoices (false); // synth notes end at the loop boundary, samples ring on
            }
        }
    }
}
} // namespace sls
