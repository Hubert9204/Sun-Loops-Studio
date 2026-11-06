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

double pitchToHz (double pitch) noexcept
{
    return 440.0 * std::pow (2.0, (pitch - 69.0) / 12.0);
}

const MixerTrack kDefaultTrack {};
} // namespace

void Player::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    releaseStep = static_cast<float> (1.0 / (0.080 * sampleRate)); // 80 ms release
    liveLimitSamples = static_cast<std::uint64_t> (kMaxLiveSeconds * sampleRate);

    for (auto& v : voices)
        v = Voice {};
    for (auto& v : clipVoices)
        v = ClipVoice {};
    nextClip = 0;

    nextEvent.fill (0);
    lastClickBeat = -1;
    freeBeatSamples = 1.0e18; // due at once
    freeBeatCount = 0;
    clickPos = -1;
    smoothedGain.fill (kUnsetGain);
    trackGain.fill (kUnsetGain);
    peaks.fill (0.0f);
    trackPeaks.fill (0.0f);
    trackMeanSquare.fill (0.0f);
    rmsCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.3 * sampleRate))); // ~300 ms window

    for (auto& chain : effects)
        for (auto& unit : chain)
            unit.prepare (sampleRate);
    for (auto& unit : masterEffects)
        unit.prepare (sampleRate);

    tickPos = 0.0;
    projectRevision = ~std::uint64_t { 0 };
    ageCounter = 0;
    wasPlaying = false;
}

void Player::releaseVoices (bool includeSamplers) noexcept
{
    for (auto& v : voices)
        if ((v.state == State::Attack || v.state == State::Sustain) && (includeSamplers || ! v.sampler) && ! v.live)
            v.state = State::Release; // notes held by hand are ended by liveNoteOff, not by the transport
}

void Player::rewind() noexcept
{
    releaseVoices (releaseSamplesOnStop);
    releaseClips();
    tickPos = 0.0;
    nextEvent.fill (0);
    nextClip = 0;
    lastClickBeat = -1;
}

void Player::startClip (const AudioEvent& e) noexcept
{
    if (e.data == nullptr || e.data->numFrames() < 2)
        return;

    ClipVoice* target = nullptr;
    for (auto& v : clipVoices)
        if (! v.active) { target = &v; break; }
    if (target == nullptr)
    {
        target = &clipVoices[0]; // all busy: replace the oldest
        for (auto& v : clipVoices)
            if (v.age < target->age)
                target = &v;
    }

    ClipVoice v;
    v.active = true;
    v.data = e.data.get();
    v.step = e.data->sampleRate / sampleRate;
    v.pos = std::max (0.0, e.offsetSeconds) * e.data->sampleRate;
    v.endTick = e.endTick;
    v.track = std::clamp (e.mixerTrack, 0, kMaxTracks - 1);
    v.gain = e.gain;
    v.age = ++ageCounter;
    *target = v;
}

void Player::releaseClips() noexcept
{
    for (auto& v : clipVoices)
        if (v.active)
            v.releasing = true;
}

float Player::nextClickSample() noexcept
{
    if (clickPos < 0)
        return 0.0f;

    const double seconds = clickPos / sampleRate;
    constexpr double kLength = 0.045;
    if (seconds >= kLength)
    {
        clickPos = -1;
        return 0.0f;
    }

    const double frequency = clickAccent ? 1800.0 : 1100.0;
    const double envelope = std::exp (-seconds * 90.0); // about 11 ms time constant
    const float amplitude = clickAccent ? 1.0f : 0.65f;
    ++clickPos;
    return static_cast<float> (std::sin (kTwoPi * frequency * seconds) * envelope) * amplitude;
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

    // Audio clips: the next one that has not started yet, and a playing clip may only go on if the new
    // snapshot still owns the very same audio (it belongs to the snapshot and may be freed).
    {
        const auto& events = project.audioEvents;
        const auto it = std::lower_bound (events.begin(), events.end(), tickPos,
                                          [] (const AudioEvent& e, double t) { return static_cast<double> (e.startTick) < t; });
        nextClip = static_cast<size_t> (it - events.begin());

        for (auto& v : clipVoices)
        {
            if (! v.active)
                continue;
            const bool stillValid = std::any_of (events.begin(), events.end(),
                                                 [&v] (const AudioEvent& e) { return e.data.get() == v.data; });
            if (! stillValid)
                v = ClipVoice {};
        }
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

void Player::liveNoteOn (const Project& project, int channel, int pitch, float velocity) noexcept
{
    if (channel < 0 || channel >= kMaxChannels || static_cast<size_t> (channel) >= project.channels.size())
        return;

    Note n;
    n.pitch = std::clamp (pitch, 0, 127);
    n.startTick = 0;
    n.lengthTicks = 0;
    n.velocity = std::isfinite (velocity) ? std::clamp (velocity, 0.0f, 1.0f) : 0.8f;
    noteOn (project.channels[static_cast<size_t> (channel)], channel, n, 1, true);
}

void Player::liveNoteOff (int channel, int pitch) noexcept
{
    for (auto& v : voices)
        if (v.live && v.channel == channel && v.livePitch == pitch
            && (v.state == State::Attack || v.state == State::Sustain))
            v.state = State::Release;
}

void Player::noteOn (const Channel& channel, int channelIndex, const Note& n, int loopLength, bool live) noexcept
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
    v.live = live && ! isSampler; // a sampler note is a one-shot either way
    v.livePitch = n.pitch;
    v.velocity = n.velocity;
    v.env = 0.0f;
    v.age = ++ageCounter;

    if (isSampler)
    {
        v.endTick = INT_MAX; // one-shot: plays to the end of the sample
        v.sample = channel.sample.get();
        v.pos = 0.0;
        v.step = std::pow (2.0, (n.pitch - channel.rootPitch + static_cast<double> (channel.pitch)) / 12.0)
               * v.sample->sampleRate / sampleRate;
        v.attackStep = static_cast<float> (1.0 / (0.0005 * sampleRate)); // 0.5 ms
    }
    else
    {
        v.endTick = live ? INT_MAX : std::min (n.startTick + n.lengthTicks, loopLength);
        v.phase = 0.0;
        v.increment = kTwoPi * pitchToHz (n.pitch + static_cast<double> (channel.pitch)) / sampleRate;
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
        releaseClips();
        tickPos = 0.0;
        nextEvent.fill (0);
        nextClip = 0;
    }
    wasPlaying = playing;

    // Per-block channel state: target gain and pan gains for every channel.
    bool anySolo = false;
    for (size_t ch = 0; ch < numMixerChannels; ++ch)
        anySolo = anySolo || project.channels[ch].solo;

    std::array<float, kMaxChannels> targetGain {};
    std::array<float, kMaxChannels> panLeft {};
    std::array<float, kMaxChannels> panRight {};
    std::array<size_t, kMaxChannels> routedTo {};

    for (size_t ch = 0; ch < numMixerChannels; ++ch)
    {
        const auto& c = project.channels[ch];
        const bool audible = anySolo ? c.solo : ! c.mute;
        targetGain[ch] = audible ? c.volume : 0.0f;
        routedTo[ch] = static_cast<size_t> (std::clamp (c.mixerTrack, 0, kMaxTracks - 1));

        const double angle = (static_cast<double> (c.pan) + 1.0) * kTwoPi / 8.0; // 0..pi/2
        panLeft[ch] = static_cast<float> (std::cos (angle));
        panRight[ch] = static_cast<float> (std::sin (angle));

        if (smoothedGain[ch] < 0.0f)
            smoothedGain[ch] = targetGain[ch]; // no fade-in on the very first block
    }

    // Per-block track state. Track pan is a balance control: the centre is unity, the far side fades out.
    const auto trackAt = [&project] (size_t t) -> const MixerTrack&
    {
        return t < project.tracks.size() ? project.tracks[t] : kDefaultTrack;
    };

    bool anyTrackSolo = false;
    for (size_t t = 0; t < static_cast<size_t> (kMaxTracks); ++t)
        anyTrackSolo = anyTrackSolo || trackAt (t).solo;

    std::array<float, kMaxTracks> trackTarget {};
    std::array<float, kMaxTracks> balanceLeft {};
    std::array<float, kMaxTracks> balanceRight {};

    for (size_t t = 0; t < static_cast<size_t> (kMaxTracks); ++t)
    {
        const auto& tr = trackAt (t);
        const bool audible = anyTrackSolo ? tr.solo : ! tr.mute;
        trackTarget[t] = audible ? std::clamp (tr.volume, 0.0f, 1.5f) : 0.0f;

        const float pan = std::clamp (tr.pan, -1.0f, 1.0f);
        balanceLeft[t] = pan > 0.0f ? 1.0f - pan : 1.0f;
        balanceRight[t] = pan < 0.0f ? 1.0f + pan : 1.0f;

        if (trackGain[t] < 0.0f)
            trackGain[t] = trackTarget[t];
    }

    peaks.fill (0.0f);
    trackPeaks.fill (0.0f);

    for (int done = 0; done < numSamples; done += kChunk)
    {
        const int n = std::min (kChunk, numSamples - done);

        for (size_t t = 0; t < static_cast<size_t> (kMaxTracks); ++t)
        {
            std::fill_n (trackL[t].begin(), n, 0.0f);
            std::fill_n (trackR[t].begin(), n, 0.0f);
        }

        // 1. Sequencer, voices and channel strips: everything lands on the mixer tracks.
        std::array<float, kChunk> click {};
        const double samplesPerBeat = 60.0 * sampleRate / std::max (bpm, 1.0);

        for (int i = 0; i < n; ++i)
        {
            if (metronomeOn)
            {
                if (playing)
                {
                    const int beat = static_cast<int> (tickPos / kTicksPerBeat);
                    if (beat != lastClickBeat)
                    {
                        // switched on in the middle of a beat: wait for the next one instead of clicking off the beat
                        const bool joinedMidBeat = lastClickBeat < 0 && tickPos > 0.5;
                        lastClickBeat = beat;
                        if (! joinedMidBeat)
                            startClick (beat % 4 == 0);
                    }
                    freeBeatSamples = 1.0e18; // when it stops, the free-running click starts at once
                    freeBeatCount = 0;
                }
                else
                {
                    lastClickBeat = -1;
                    if (freeBeatSamples >= samplesPerBeat)
                    {
                        freeBeatSamples -= freeBeatSamples > 1.0e17 ? freeBeatSamples : samplesPerBeat;
                        startClick (freeBeatCount % 4 == 0);
                        ++freeBeatCount;
                    }
                    freeBeatSamples += 1.0;
                }
                click[static_cast<size_t> (i)] = nextClickSample() * metronomeVolume * 0.6f;
            }
            else
            {
                lastClickBeat = -1;
                freeBeatSamples = 1.0e18;
                freeBeatCount = 0;
                clickPos = -1;
            }

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

                while (nextClip < project.audioEvents.size()
                       && static_cast<double> (project.audioEvents[nextClip].startTick) <= tickPos)
                {
                    startClip (project.audioEvents[nextClip]);
                    ++nextClip;
                }
                for (auto& v : clipVoices)
                    if (v.active && ! v.releasing && tickPos >= static_cast<double> (v.endTick))
                        v.releasing = true;
            }

            for (auto& v : clipVoices)
            {
                if (! v.active)
                    continue;

                const int idx = static_cast<int> (v.pos);
                if (idx + 1 >= v.data->numFrames())
                {
                    v = ClipVoice {};
                    continue;
                }
                const float frac = static_cast<float> (v.pos - idx);
                const float* sl = v.data->left.data();
                const float* sr = v.data->right.data();
                const float gain = v.env * v.gain;
                trackL[static_cast<size_t> (v.track)][static_cast<size_t> (i)] += (sl[idx] + frac * (sl[idx + 1] - sl[idx])) * gain;
                trackR[static_cast<size_t> (v.track)][static_cast<size_t> (i)] += (sr[idx] + frac * (sr[idx + 1] - sr[idx])) * gain;
                v.pos += v.step;

                if (v.releasing)
                {
                    v.env -= releaseStep;
                    if (v.env <= 0.0f)
                        v = ClipVoice {};
                }
                else if (v.env < 1.0f)
                    v.env = std::min (1.0f, v.env + static_cast<float> (1.0 / (0.002 * sampleRate))); // 2 ms de-click
            }

            std::array<float, kMaxChannels> busL {};
            std::array<float, kMaxChannels> busR {};

            for (auto& v : voices)
            {
                if (v.state == State::Idle)
                    continue;

                float l = 0.0f, r = 0.0f;

                // Safety net: a held note whose "off" never arrived does not sound for ever.
                if (v.live && ! v.sampler && v.state != State::Release && ++v.liveSamples >= liveLimitSamples)
                    v.state = State::Release;

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

            for (size_t ch = 0; ch < numMixerChannels; ++ch)
            {
                smoothedGain[ch] += (targetGain[ch] - smoothedGain[ch]) * kGainSmoothing;
                const float l = busL[ch] * smoothedGain[ch] * panLeft[ch];
                const float r = busR[ch] * smoothedGain[ch] * panRight[ch];
                peaks[ch] = std::max ({ peaks[ch], std::fabs (l), std::fabs (r) });
                trackL[routedTo[ch]][static_cast<size_t> (i)] += l;
                trackR[routedTo[ch]][static_cast<size_t> (i)] += r;
            }

            if (playing)
            {
                tickPos += ticksPerSample;
                if (tickPos >= static_cast<double> (loopLength))
                {
                    tickPos = std::fmod (tickPos, static_cast<double> (loopLength));
                    nextEvent.fill (0);
                    nextClip = 0;
                    releaseVoices (false); // synth notes end at the loop boundary, samples ring on
                    releaseClips();        // audio clips are cut there
                }
            }
        }

        // 2. Each track: insert effects (pre-fader), then fader, balance, mute/solo, metering, master bus.
        std::array<float, kChunk> mixL {}, mixR {};

        for (size_t t = 0; t < static_cast<size_t> (kMaxTracks); ++t)
        {
            float* l = trackL[t].data();
            float* r = trackR[t].data();
            const auto& tr = trackAt (t);

            for (size_t s = 0; s < static_cast<size_t> (kSlotsPerTrack); ++s)
                effects[t][s].process (tr.slots[s], l, r, n);

            for (int i = 0; i < n; ++i)
            {
                trackGain[t] += (trackTarget[t] - trackGain[t]) * kGainSmoothing;
                const float outL = l[i] * trackGain[t] * balanceLeft[t];
                const float outR = r[i] * trackGain[t] * balanceRight[t];

                trackPeaks[t] = std::max ({ trackPeaks[t], std::fabs (outL), std::fabs (outR) });
                trackMeanSquare[t] += (0.5f * (outL * outL + outR * outR) - trackMeanSquare[t]) * rmsCoef;

                mixL[static_cast<size_t> (i)] += outL;
                mixR[static_cast<size_t> (i)] += outR;
            }
            if (trackMeanSquare[t] < 1.0e-20f)
                trackMeanSquare[t] = 0.0f;
        }

        // 3. Master bus: insert effects on the sum of all tracks, then a soft limiter, added to the output.
        for (size_t s = 0; s < static_cast<size_t> (kSlotsPerTrack); ++s)
            masterEffects[s].process (project.masterSlots[s], mixL.data(), mixR.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const float outL = std::tanh (mixL[static_cast<size_t> (i)]);
            const float outR = std::tanh (mixR[static_cast<size_t> (i)]);
            const float clickSample = click[static_cast<size_t> (i)];
            for (int ch = 0; ch < numChannels; ++ch)
                if (out[ch] != nullptr)
                    out[ch][done + i] += ((ch & 1) == 0 ? outL : outR) + clickSample;
        }
    }
}
} // namespace sls
