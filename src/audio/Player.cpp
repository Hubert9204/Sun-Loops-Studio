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
    for (auto& g : sendGain)
        g.fill (kUnsetGain);
    sendGate.fill (kUnsetGain);
    auxGain.fill (kUnsetGain);
    auxPeaks.fill (0.0f);
    auxMeanSquare.fill (0.0f);
    peaks.fill (0.0f);
    trackPeaks.fill (0.0f);
    trackMeanSquare.fill (0.0f);
    rmsCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.3 * sampleRate))); // ~300 ms window

    for (auto& chain : effects)
        for (auto& unit : chain)
            unit.prepare (sampleRate);
    for (auto& unit : masterEffects)
        unit.prepare (sampleRate);
    for (auto& chain : auxEffects)
        for (auto& unit : chain)
            unit.prepare (sampleRate);

    tickPos = 0.0;
    startPosition = 0.0;
    projectRevision = ~std::uint64_t { 0 };
    ageCounter = 0;
    wasPlaying = false;
}

void Player::releaseVoices (bool includeSamplers) noexcept
{
    for (auto& v : voices)
        if ((v.state == State::Attack || v.state == State::Sustain) && (includeSamplers || ! v.sampler || v.held) && ! v.live)
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

void Player::startClip (const AudioEvent& e, double extraSeconds) noexcept
{
    if (e.data == nullptr || e.data->numFrames() < 2)
        return;
    const double startFrame = (std::max (0.0, e.offsetSeconds) + std::max (0.0, extraSeconds)) * e.data->sampleRate;
    if (startFrame + 1.0 >= static_cast<double> (e.data->numFrames()))
        return; // the clip's sound has already ended at that point

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
    v.pos = startFrame;
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

void Player::locate (const Project& project, double tick) noexcept
{
    // First note of each channel that has not started yet at `tick`.
    for (size_t ch = 0; ch < project.channels.size() && ch < static_cast<size_t> (kMaxChannels); ++ch)
    {
        const auto& notes = project.channels[ch].notes;
        const auto it = std::lower_bound (notes.begin(), notes.end(), tick,
                                          [] (const Note& n, double t) { return static_cast<double> (n.startTick) < t; });
        nextEvent[ch] = static_cast<size_t> (it - notes.begin());
    }
    const auto& events = project.audioEvents;
    const auto it = std::lower_bound (events.begin(), events.end(), tick,
                                      [] (const AudioEvent& e, double t) { return static_cast<double> (e.startTick) < t; });
    nextClip = static_cast<size_t> (it - events.begin());
}

void Player::startClipsInside (const Project& project, double tick, double ticksPerSecond) noexcept
{
    if (ticksPerSecond <= 0.0)
        return;
    for (const auto& e : project.audioEvents)
        if (static_cast<double> (e.startTick) < tick && tick < static_cast<double> (e.endTick))
            startClip (e, (tick - e.startTick) / ticksPerSecond);
}

void Player::jumpTo (const Project& project, double tick, double ticksPerSecond) noexcept
{
    releaseVoices (false); // synth notes end at the jump, samples ring on (as at the loop boundary)
    releaseClips();        // audio clips are cut there
    tickPos = tick;
    locate (project, std::floor (tick)); // a note exactly on the target tick still plays, although the position overshoots it a little
    startClipsInside (project, std::floor (tick), ticksPerSecond);
}

void Player::seek (const Project& project, double tick, double bpm) noexcept
{
    const int length = std::max (1, project.lengthTicks);
    if (! std::isfinite (tick) || tick < 0.0 || tick >= static_cast<double> (length))
        tick = 0.0;
    startPosition = tick;
    releaseVoices (true);
    releaseClips();
    tickPos = tick;
    locate (project, tick);
    lastClickBeat = -1;
    if (wasPlaying)
        startClipsInside (project, tick, std::clamp (bpm, 20.0, 400.0) * kTicksPerBeat / 60.0);
}

void Player::onProjectChanged (const Project& project) noexcept
{
    locate (project, tickPos);

    // Audio clips: a playing clip may only go on if the new snapshot still owns the very same audio (it belongs to the
    // snapshot and may be freed).
    {
        const auto& events = project.audioEvents;

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

        bool stillValid = false;
        if (static_cast<size_t> (v.channel) < project.channels.size())
        {
            const auto& ch = project.channels[static_cast<size_t> (v.channel)];
            stillValid = v.pad >= 0 ? (ch.kit != nullptr && ch.kit->owns (v.sample))
                                    : (ch.sample.get() == v.sample || (ch.program != nullptr && ch.program->owns (v.sample)));
        }
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

Player::Voice& Player::takeVoice() noexcept
{
    for (auto& v : voices)
        if (v.state == State::Idle)
            return v;

    // Steal: oldest releasing voice first, otherwise the oldest voice.
    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.state == State::Release && (target == nullptr || v.age < target->age))
            target = &v;
    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.age < target->age)
                target = &v;
    }
    return *target;
}

// A note on a drum channel: the pad it names, the layer its velocity picks, the pad's choke group cut first.
void Player::drumNoteOn (const Channel& channel, int channelIndex, const Note& n) noexcept
{
    if (channel.kit == nullptr)
        return;
    const DrumKit& kit = *channel.kit;
    const int padIndex = DrumKit::padForPitch (n.pitch);
    if (padIndex < 0 || ! kit.padAudible (padIndex, kit.anySolo()))
        return;
    const DrumPad& pad = kit.pads[static_cast<size_t> (padIndex)];
    const auto* layer = pad.layerFor (std::clamp (static_cast<int> (std::lround (n.velocity * 127.0f)), 1, 127));
    if (layer == nullptr)
        return;

    if (pad.chokeGroup > 0)
        for (auto& other : voices)
            if (other.pad >= 0 && other.channel == channelIndex && other.choke == pad.chokeGroup
                && (other.state == State::Attack || other.state == State::Sustain))
                other.state = State::Release;

    Voice& v = takeVoice();
    v = Voice {};
    v.state = State::Attack;
    v.sampler = true;
    v.channel = channelIndex;
    v.livePitch = n.pitch;
    v.velocity = n.velocity;
    v.age = ++ageCounter;
    v.endTick = INT_MAX; // one-shot, like the sampler
    v.sample = layer->sample.get();
    v.step = std::pow (2.0, (static_cast<double> (pad.pitch) + static_cast<double> (channel.pitch)) / 12.0) * v.sample->sampleRate / sampleRate;
    v.attackStep = static_cast<float> (1.0 / (0.0005 * sampleRate));
    v.pad = padIndex;
    v.choke = pad.chokeGroup;
    const double angle = (static_cast<double> (pad.pan) + 1.0) * kTwoPi / 8.0;
    v.padGain = pad.volume;
    v.padLeft = static_cast<float> (std::cos (angle) * 1.4142135623730951); // 1 at the centre, like a channel's pan
    v.padRight = static_cast<float> (std::sin (angle) * 1.4142135623730951);
}

void Player::noteOn (const Channel& channel, int channelIndex, const Note& n, int loopLength, bool live) noexcept
{
    if (channel.instrument == Instrument::Drums)
    {
        drumNoteOn (channel, channelIndex, n);
        return;
    }
    const bool isSampler = channel.instrument == Instrument::Sampler;
    const bool hasProgram = isSampler && channel.program != nullptr && ! channel.program->zones.empty();
    if (isSampler && ! hasProgram && (channel.sample == nullptr || channel.sample->numFrames() < 2))
        return;

    // Legato and slide: the note sounding in this channel goes on instead of a new voice starting.
    if (! live && n.link != NoteLink::None)
    {
        Voice* held = nullptr;
        for (auto& v : voices)
            if (v.channel == channelIndex && ! v.live && v.sampler == isSampler
                && (v.state == State::Attack || v.state == State::Sustain)
                && (! isSampler || hasProgram || v.sample == channel.sample.get())
                && (held == nullptr || v.age > held->age))
                held = &v;

        if (held != nullptr)
        {
            double& rate = isSampler ? held->step : held->increment;
            const double settled = held->glideLeft > 0 ? held->glideTarget : rate;
            const double newRate = hasProgram
                ? settled * std::pow (2.0, (n.pitch - held->livePitch) / 12.0) // a zone keeps playing, retuned by the step between the notes
                : isSampler
                ? std::pow (2.0, (n.pitch - channel.rootPitch + static_cast<double> (channel.pitch)) / 12.0) * held->sample->sampleRate / sampleRate
                : kTwoPi * pitchToHz (n.pitch + static_cast<double> (channel.pitch)) / sampleRate;
            held->livePitch = n.pitch;
            if (! isSampler || held->held)
                held->endTick = std::min (n.startTick + n.lengthTicks, loopLength);

            if (n.link == NoteLink::Slide && rate > 0.0 && newRate > 0.0)
            {
                const int glideTicks = std::min (n.lengthTicks, kTicksPerBeat / 2);
                held->glideLeft = std::max (1, static_cast<int> (std::lround (glideTicks / std::max (ticksPerSampleNow, 1.0e-9))));
                held->glideRatio = std::pow (newRate / rate, 1.0 / held->glideLeft);
                held->glideTarget = newRate;
            }
            else
            {
                rate = newRate;
                held->glideLeft = 0;
            }
            return;
        }
    }

    if (isSampler)
    {
        if (hasProgram)
        {
            // Every zone that answers to the key and the velocity plays (at most four: a stereo pair, layers).
            const int velocity = std::clamp (static_cast<int> (std::lround (n.velocity * 127.0f)), 1, 127);
            int started = 0;
            for (const auto& z : channel.program->zones)
            {
                if (started >= 4 || ! z.answers (n.pitch, velocity))
                    continue;
                const bool loops = z.loop != LoopMode::Off && z.loopEnd > z.loopStart + 1;
                samplerVoiceOn (channel, channelIndex, n, loopLength, live, z.sample.get(), z.rootKey, z.tune, z.keyTrack, z.gain, z.pan,
                                loops ? z.loopStart : 0.0, loops ? z.loopEnd : 0.0);
                ++started;
            }
            return;
        }
        const auto& set = channel.sampler;
        const double frames = channel.sample->numFrames();
        const bool loops = set.loop != LoopMode::Off && (set.loopEnd - set.loopStart) * frames > 2.0;
        samplerVoiceOn (channel, channelIndex, n, loopLength, live, channel.sample.get(), channel.rootPitch, 0.0, 1.0, 1.0f, 0.0f,
                        loops ? std::floor (set.loopStart * frames) : 0.0, loops ? std::floor (set.loopEnd * frames) : 0.0);
        return;
    }

    Voice* target = &takeVoice();

    Voice v;
    v.state = State::Attack;
    v.channel = channelIndex;
    v.live = live;
    v.livePitch = n.pitch;
    v.velocity = n.velocity;
    v.env = 0.0f;
    v.age = ++ageCounter;
    v.endTick = live ? INT_MAX : std::min (n.startTick + n.lengthTicks, loopLength);
    v.phase = 0.0;
    v.increment = kTwoPi * pitchToHz (n.pitch + static_cast<double> (channel.pitch)) / sampleRate;
    v.attackStep = static_cast<float> (1.0 / (0.005 * sampleRate)); // 5 ms

    *target = v;
}

void Player::samplerVoiceOn (const Channel& channel, int channelIndex, const Note& n, int loopLength, bool live, const SampleData* sample,
                             double rootKey, double tune, double keyTrack, float gain, float pan, double loopStart, double loopEnd) noexcept
{
    const auto& set = channel.sampler;
    const bool loops = loopEnd > loopStart + 1.0;
    const bool held = set.envelope || loops;

    Voice* target = &takeVoice();
    Voice v;
    v.state = State::Attack;
    v.sampler = true;
    v.channel = channelIndex;
    v.held = held;
    v.live = live && held; // a one-shot sampler note plays to its end either way
    v.livePitch = n.pitch;
    v.velocity = n.velocity;
    v.env = 0.0f;
    v.age = ++ageCounter;
    v.endTick = held && ! live ? std::min (n.startTick + n.lengthTicks, loopLength) : INT_MAX;
    v.sample = sample;
    v.pos = 0.0;
    v.step = std::pow (2.0, ((n.pitch - rootKey) * keyTrack + tune + static_cast<double> (channel.pitch)) / 12.0) * sample->sampleRate / sampleRate;
    if (loops)
    {
        v.loopStart = loopStart;
        v.loopEnd = std::min (loopEnd, static_cast<double> (sample->numFrames()));
    }

    if (set.envelope)
    {
        v.attackStep = static_cast<float> (1.0 / (std::max (0.0005, static_cast<double> (set.attack)) * sampleRate));
        v.sustainLevel = set.sustain;
        v.decayStep = static_cast<float> ((1.0 - set.sustain) / (std::max (0.001, static_cast<double> (set.decay)) * sampleRate));
        v.relStep = static_cast<float> (1.0 / (std::max (0.002, static_cast<double> (set.release)) * sampleRate));
    }
    else
        v.attackStep = static_cast<float> (1.0 / (0.0005 * sampleRate)); // 0.5 ms

    if (std::abs (gain - 1.0f) > 1.0e-6f || std::abs (pan) > 1.0e-6f)
    {
        const double angle = (static_cast<double> (pan) + 1.0) * kTwoPi / 8.0;
        v.padGain = gain;
        v.padLeft = static_cast<float> (std::cos (angle) * 1.4142135623730951);
        v.padRight = static_cast<float> (std::sin (angle) * 1.4142135623730951);
    }

    if (set.filter != SamplerFilter::Off)
    {
        // Topology-preserving state-variable filter (Simper): fixed for the note.
        const double fc = std::min (SamplerSettings::cutoffHz (set.cutoff), 0.45 * sampleRate);
        const double g = std::tan (kTwoPi * 0.5 * fc / sampleRate);
        const double k = 2.0 - 1.94 * static_cast<double> (set.resonance); // Q from 0.5 to about 16
        const double a1 = 1.0 / (1.0 + g * (g + k));
        v.filter = set.filter;
        v.fk = static_cast<float> (k);
        v.fa1 = static_cast<float> (a1);
        v.fa2 = static_cast<float> (g * a1);
        v.fa3 = static_cast<float> (g * g * a1);
    }
    *target = v;
}

namespace
{
// Sets every automated parameter to its value at song position `tick`. Parameters without a clip that has started are left as
// they are. Channel and track gains go through `setChannel` / `setTrack` (which know the current volume and pan), effect
// parameters are written into the slot copies.
template <typename SetChannel, typename SetTrack>
void applyAutomation (const Project& project, double tick, size_t numChannels, SetChannel& setChannel, SetTrack& setTrack,
                      const std::array<float, kMaxChannels>& channelVolume, const std::array<float, kMaxChannels>& channelPan,
                      const std::array<float, kMaxTracks>& trackVolume, const std::array<float, kMaxTracks>& trackPan,
                      std::array<std::array<EffectSlot, kSlotsPerTrack>, kMaxTracks>& trackSlots,
                      std::array<EffectSlot, kSlotsPerTrack>& masterSlots) noexcept
{
    const auto& events = project.automation;
    size_t first = 0;
    while (first < events.size())
    {
        const auto target = events[first].target;
        size_t last = first + 1;
        while (last < events.size() && events[last].target.key() == target.key())
            ++last;

        float v = 0.0f;
        if (automationValueFor (events, first, last, tick, v))
        {
            const auto idx = static_cast<size_t> (std::max (0, target.index));
            switch (target.kind)
            {
                case AutomationKind::ChannelVolume:
                    if (idx < numChannels)
                        setChannel (idx, v, channelPan[idx]);
                    break;
                case AutomationKind::ChannelPan:
                    if (idx < numChannels)
                        setChannel (idx, channelVolume[idx], v);
                    break;
                case AutomationKind::TrackVolume:
                    if (idx < static_cast<size_t> (kMaxTracks))
                        setTrack (idx, v, trackPan[idx]);
                    break;
                case AutomationKind::TrackPan:
                    if (idx < static_cast<size_t> (kMaxTracks))
                        setTrack (idx, trackVolume[idx], v);
                    break;
                case AutomationKind::TrackEffect:
                    if (idx < trackSlots.size())
                        trackSlots[idx][static_cast<size_t> (target.slot)].params[static_cast<size_t> (target.param)] = v;
                    break;
                case AutomationKind::MasterEffect:
                    masterSlots[static_cast<size_t> (target.slot)].params[static_cast<size_t> (target.param)] = v;
                    break;
            }
        }
        first = last;
    }
}
} // namespace

void Player::render (const Project& project, double bpm, bool playing,
                     float* const* out, int numChannels, int numSamples) noexcept
{
    bpm = std::clamp (bpm, 20.0, 400.0);
    const double ticksPerSample = bpm * kTicksPerBeat / (60.0 * sampleRate);
    ticksPerSampleNow = ticksPerSample;
    const int loopLength = std::max (1, project.lengthTicks);
    const size_t numMixerChannels = std::min (project.channels.size(), static_cast<size_t> (kMaxChannels));

    if (project.revision != projectRevision)
    {
        projectRevision = project.revision;
        onProjectChanged (project);
    }

    const double ticksPerSecond = ticksPerSample * sampleRate;
    if (! playing && wasPlaying)
    {
        releaseVoices (releaseSamplesOnStop);
        releaseClips();
        tickPos = startPosition < static_cast<double> (loopLength) ? startPosition : 0.0; // back to where playback was started from
        locate (project, tickPos);
    }
    else if (playing && ! wasPlaying && tickPos > 0.0)
        startClipsInside (project, tickPos, ticksPerSecond); // started from the middle of the song: clips already going there sound too
    wasPlaying = playing;
    const bool loopRegion = project.loopStartTick >= 0 && project.loopEndTick > project.loopStartTick
                            && project.loopEndTick <= loopLength;

    // Per-block channel state: target gain and pan gains for every channel.
    bool anySolo = false;
    for (size_t ch = 0; ch < numMixerChannels; ++ch)
        anySolo = anySolo || project.channels[ch].solo;

    std::array<float, kMaxChannels> targetGain {};
    std::array<float, kMaxChannels> panLeft {};
    std::array<float, kMaxChannels> panRight {};
    std::array<size_t, kMaxChannels> routedTo {};
    std::array<bool, kMaxChannels> channelAudible {};

    // A channel's gain and pan for the coming samples (the project's own, or what automation says at this moment).
    std::array<float, kMaxChannels> channelVolume {}, channelPan {};
    const auto setChannel = [&] (size_t ch, float volume, float pan)
    {
        channelVolume[ch] = volume;
        channelPan[ch] = pan;
        targetGain[ch] = channelAudible[ch] ? std::clamp (volume, 0.0f, 1.5f) : 0.0f;
        const double angle = (static_cast<double> (std::clamp (pan, -1.0f, 1.0f)) + 1.0) * kTwoPi / 8.0; // 0..pi/2
        panLeft[ch] = static_cast<float> (std::cos (angle));
        panRight[ch] = static_cast<float> (std::sin (angle));
    };

    for (size_t ch = 0; ch < numMixerChannels; ++ch)
    {
        const auto& c = project.channels[ch];
        channelAudible[ch] = anySolo ? c.solo : ! c.mute;
        routedTo[ch] = static_cast<size_t> (std::clamp (c.mixerTrack, 0, kMaxTracks - 1));
        setChannel (ch, c.volume, c.pan);

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
    std::array<bool, kMaxTracks> trackAudible {};

    // VCA groups: a track's fader is scaled by the level of every group it belongs to (a muted group silences its members).
    std::array<float, kMaxTracks> trackVca {};
    for (size_t t = 0; t < static_cast<size_t> (kMaxTracks); ++t)
    {
        float v = 1.0f;
        for (int g = 0; g < kVcaGroups; ++g)
            if ((trackAt (t).vcaMask >> g) & 1u)
                v *= project.console.vcaMute[static_cast<size_t> (g)] ? 0.0f : project.console.vcaLevel[static_cast<size_t> (g)];
        trackVca[t] = v;
    }

    std::array<float, kMaxTracks> trackVolume {}, trackPan {};
    const auto setTrack = [&] (size_t t, float volume, float pan)
    {
        trackVolume[t] = volume;
        trackPan[t] = pan;
        trackTarget[t] = trackAudible[t] ? std::clamp (volume, 0.0f, 1.5f) * trackVca[t] : 0.0f;
        const float p = std::clamp (pan, -1.0f, 1.0f);
        balanceLeft[t] = p > 0.0f ? 1.0f - p : 1.0f;
        balanceRight[t] = p < 0.0f ? 1.0f + p : 1.0f;
    };

    for (size_t t = 0; t < static_cast<size_t> (kMaxTracks); ++t)
    {
        const auto& tr = trackAt (t);
        trackAudible[t] = anyTrackSolo ? tr.solo : ! tr.mute;
        setTrack (t, tr.volume, tr.pan);

        if (trackGain[t] < 0.0f)
            trackGain[t] = trackTarget[t];
        if (sendGate[t] < 0.0f)
            sendGate[t] = trackAudible[t] ? 1.0f : 0.0f;
        for (size_t a = 0; a < static_cast<size_t> (kAuxBuses); ++a)
            if (sendGain[t][a] < 0.0f)
                sendGain[t][a] = tr.sends[a];
    }

    // Aux buses: fader, balance, mute.
    std::array<float, kAuxBuses> auxTarget {}, auxBalanceLeft {}, auxBalanceRight {};
    for (size_t a = 0; a < static_cast<size_t> (kAuxBuses); ++a)
    {
        const auto& bus = project.console.aux[a];
        auxTarget[a] = bus.mute ? 0.0f : std::clamp (bus.volume, 0.0f, 1.5f);
        const float p = std::clamp (bus.pan, -1.0f, 1.0f);
        auxBalanceLeft[a] = p > 0.0f ? 1.0f - p : 1.0f;
        auxBalanceRight[a] = p < 0.0f ? 1.0f + p : 1.0f;
        if (auxGain[a] < 0.0f)
            auxGain[a] = auxTarget[a];
    }

    // Automation (Song mode): effect parameters are changed in copies of the slots, so the project itself is never touched.
    const bool automated = playing && ! project.automation.empty();
    std::array<std::array<EffectSlot, kSlotsPerTrack>, kMaxTracks> trackSlotsNow {};
    std::array<EffectSlot, kSlotsPerTrack> masterSlotsNow {};
    if (automated)
    {
        for (size_t t = 0; t < static_cast<size_t> (kMaxTracks); ++t)
            trackSlotsNow[t] = trackAt (t).slots;
        masterSlotsNow = project.masterSlots;
    }

    peaks.fill (0.0f);
    trackPeaks.fill (0.0f);
    auxPeaks.fill (0.0f);

    for (int done = 0; done < numSamples; done += kChunk)
    {
        const int n = std::min (kChunk, numSamples - done);

        if (automated)
            applyAutomation (project, tickPos, numMixerChannels, setChannel, setTrack, channelVolume, channelPan, trackVolume, trackPan,
                             trackSlotsNow, masterSlotsNow);

        for (size_t t = 0; t < static_cast<size_t> (kMaxTracks); ++t)
        {
            std::fill_n (trackL[t].begin(), n, 0.0f);
            std::fill_n (trackR[t].begin(), n, 0.0f);
        }
        for (size_t a = 0; a < static_cast<size_t> (kAuxBuses); ++a)
        {
            std::fill_n (auxL[a].begin(), n, 0.0f);
            std::fill_n (auxR[a].begin(), n, 0.0f);
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
                        const bool joinedMidBeat = lastClickBeat < 0 && std::fmod (tickPos, static_cast<double> (kTicksPerBeat)) > 0.5;
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
                    const bool looping = v.loopEnd > v.loopStart + 1.0;
                    if (looping && v.pos >= v.loopEnd)
                        v.pos = v.loopStart + std::fmod (v.pos - v.loopStart, v.loopEnd - v.loopStart);
                    const int idx = static_cast<int> (v.pos);
                    const int frames = v.sample->numFrames();
                    if (idx + 1 >= frames && ! (looping && idx < frames))
                    {
                        v = Voice {};
                        continue;
                    }
                    // The sample after this one: at the end of a loop it is the loop's first sample.
                    const int next = looping && idx + 1 >= static_cast<int> (v.loopEnd) ? static_cast<int> (v.loopStart) : idx + 1;
                    const float frac = static_cast<float> (v.pos - idx);
                    const float* sl = v.sample->left.data();
                    const float* sr = v.sample->right.data();
                    float xl = sl[idx] + frac * (sl[next] - sl[idx]);
                    float xr = sr[idx] + frac * (sr[next] - sr[idx]);
                    if (v.filter != SamplerFilter::Off)
                    {
                        const auto svf = [&v] (float x, float& ic1, float& ic2)
                        {
                            const float v3 = x - ic2;
                            const float v1 = v.fa1 * ic1 + v.fa2 * v3;
                            const float v2 = ic2 + v.fa2 * ic1 + v.fa3 * v3;
                            ic1 = 2.0f * v1 - ic1;
                            ic2 = 2.0f * v2 - ic2;
                            return v.filter == SamplerFilter::LowPass ? v2 : v.filter == SamplerFilter::BandPass ? v1 : x - v.fk * v1 - v2;
                        };
                        xl = svf (xl, v.s1L, v.s2L);
                        xr = svf (xr, v.s1R, v.s2R);
                    }
                    const float gain = v.env * v.velocity * kSampleGain * v.padGain;
                    l = xl * gain * v.padLeft;
                    r = xr * gain * v.padRight;
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

                if (v.glideLeft > 0) // a pitch slide in progress: move towards the target pitch, in equal steps of semitones
                {
                    double& rate = v.sampler ? v.step : v.increment;
                    if (--v.glideLeft == 0)
                        rate = v.glideTarget;
                    else
                        rate *= v.glideRatio;
                }

                busL[static_cast<size_t> (v.channel)] += l;
                busR[static_cast<size_t> (v.channel)] += r;

                if (v.state == State::Attack)
                {
                    v.env += v.attackStep;
                    if (v.env >= 1.0f) { v.env = 1.0f; v.state = State::Sustain; }
                }
                else if (v.state == State::Sustain)
                {
                    if (v.env > v.sustainLevel) // the decay of a sampler envelope
                    {
                        v.env = std::max (v.sustainLevel, v.env - v.decayStep);
                        if (v.env <= 0.0f)
                            v = Voice {}; // decayed to a sustain level of zero: nothing more to hear
                    }
                }
                else if (v.state == State::Release)
                {
                    v.env -= v.relStep > 0.0f ? v.relStep : releaseStep;
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
                const double before = tickPos;
                tickPos += ticksPerSample;
                const auto regionEnd = static_cast<double> (project.loopEndTick);
                if (loopRegion && before < regionEnd && tickPos >= regionEnd)
                    jumpTo (project, project.loopStartTick + (tickPos - regionEnd), ticksPerSecond); // the loop region repeats
                else if (tickPos >= static_cast<double> (loopLength))
                {
                    if (loopRegion)
                        jumpTo (project, project.loopStartTick, ticksPerSecond); // started after the region: the end of the song goes back to it
                    else
                    {
                        tickPos = std::fmod (tickPos, static_cast<double> (loopLength));
                        nextEvent.fill (0);
                        nextClip = 0;
                        releaseVoices (false); // synth notes end at the loop boundary, samples ring on
                        releaseClips();        // audio clips are cut there
                    }
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
                effects[t][s].process (automated ? trackSlotsNow[t][s] : tr.slots[s], l, r, n);

            const float gateTarget = trackAudible[t] ? 1.0f : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                trackGain[t] += (trackTarget[t] - trackGain[t]) * kGainSmoothing;
                const float outL = l[i] * trackGain[t] * balanceLeft[t];
                const float outR = r[i] * trackGain[t] * balanceRight[t];

                // Sends to the aux buses: after the fader (or before it, if the track says so); a muted track sends nothing.
                sendGate[t] += (gateTarget - sendGate[t]) * kGainSmoothing;
                for (size_t a = 0; a < static_cast<size_t> (kAuxBuses); ++a)
                {
                    sendGain[t][a] += (tr.sends[a] - sendGain[t][a]) * kGainSmoothing;
                    const float level = sendGain[t][a] * sendGate[t];
                    auxL[a][static_cast<size_t> (i)] += (tr.sendsPreFader ? l[i] : outL) * level;
                    auxR[a][static_cast<size_t> (i)] += (tr.sendsPreFader ? r[i] : outR) * level;
                }

                trackPeaks[t] = std::max ({ trackPeaks[t], std::fabs (outL), std::fabs (outR) });
                trackMeanSquare[t] += (0.5f * (outL * outL + outR * outR) - trackMeanSquare[t]) * rmsCoef;

                mixL[static_cast<size_t> (i)] += outL;
                mixR[static_cast<size_t> (i)] += outR;
            }
            if (trackMeanSquare[t] < 1.0e-20f)
                trackMeanSquare[t] = 0.0f;
        }

        // 2b. Aux buses: effects, fader and balance, then the master bus.
        for (size_t a = 0; a < static_cast<size_t> (kAuxBuses); ++a)
        {
            const auto& bus = project.console.aux[a];
            float* l = auxL[a].data();
            float* r = auxR[a].data();
            for (size_t s = 0; s < static_cast<size_t> (kSlotsPerTrack); ++s)
                auxEffects[a][s].process (bus.slots[s], l, r, n);

            for (int i = 0; i < n; ++i)
            {
                auxGain[a] += (auxTarget[a] - auxGain[a]) * kGainSmoothing;
                const float outL = l[i] * auxGain[a] * auxBalanceLeft[a];
                const float outR = r[i] * auxGain[a] * auxBalanceRight[a];
                auxPeaks[a] = std::max ({ auxPeaks[a], std::fabs (outL), std::fabs (outR) });
                auxMeanSquare[a] += (0.5f * (outL * outL + outR * outR) - auxMeanSquare[a]) * rmsCoef;
                mixL[static_cast<size_t> (i)] += outL;
                mixR[static_cast<size_t> (i)] += outR;
            }
            if (auxMeanSquare[a] < 1.0e-20f)
                auxMeanSquare[a] = 0.0f;
        }

        // 3. Master bus: insert effects on the sum of all tracks, then a soft limiter, added to the output.
        for (size_t s = 0; s < static_cast<size_t> (kSlotsPerTrack); ++s)
            masterEffects[s].process (automated ? masterSlotsNow[s] : project.masterSlots[s], mixL.data(), mixR.data(), n);

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
