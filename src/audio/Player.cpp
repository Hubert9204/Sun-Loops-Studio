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
constexpr float kSubtractiveGain = 0.35f; // a full saw through an open filter about as loud as the classic synth's fundamental
constexpr float kFmGain = 0.5f;           // a single full sine operator about as loud as the classic synth
constexpr float kAdditiveGain = 0.35f;    // like the subtractive engine: a saw-like spectrum through no filter
constexpr float kPhysicalGain = 0.6f;
const SynthPatch defaultPatch {};
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

Player::Player()
    : trackL (static_cast<size_t> (kMaxTracks)), trackR (static_cast<size_t> (kMaxTracks)),
      keyL (static_cast<size_t> (kMaxTracks)), keyR (static_cast<size_t> (kMaxTracks)),
      keyPtrL (static_cast<size_t> (kMaxTracks), nullptr), keyPtrR (static_cast<size_t> (kMaxTracks), nullptr),
      keyWanted (static_cast<size_t> (kMaxTracks), 0), effects (static_cast<size_t> (kMaxTracks)),
      slotsNow (static_cast<size_t> (kMaxTracks))
{
    memoryStore.reserve (static_cast<size_t> (kMaxEffectMemories));
    freeMemory.reserve (static_cast<size_t> (kMaxEffectMemories));
}

Player::~Player()
{
    EffectMemory* m = nullptr;
    while (incomingMemory.pop (m))
        delete m;
}

bool Player::offerEffectMemory (std::unique_ptr<EffectMemory> memory) noexcept
{
    if (memory == nullptr // (its rate is checked when the audio thread adopts it)
        || memoryCount.load (std::memory_order_relaxed) + static_cast<int> (incomingMemory.size()) >= kMaxEffectMemories)
        return false;
    EffectMemory* raw = memory.release(); // the queue owns it until the audio thread adopts it
    if (! incomingMemory.push (raw))
    {
        delete raw; // (the queue was full)
        return false;
    }
    return true;
}

void Player::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    releaseStep = static_cast<float> (1.0 / (0.080 * sampleRate)); // 80 ms release

    // Physical strings (0.24.0): a delay line per voice, long enough for 20 Hz, and the body's three resonances.
    stringLineLength = 1;
    while (stringLineLength < static_cast<int> (sampleRate / 20.0) + 16)
        stringLineLength *= 2;
    stringLines.assign (static_cast<size_t> (stringLineLength) * kMaxVoices, 0.0f);
    {
        const double body[3][2] = { { 98.0, 3.0 }, { 204.0, 3.5 }, { 410.0, 2.5 } }; // a guitar's air and top-plate resonances (Hz, Q)
        for (size_t i = 0; i < 3; ++i)
        {
            const double w = 2.0 * 3.14159265358979323846 * body[i][0] / sampleRate, alpha = std::sin (w) / (2.0 * body[i][1]);
            const double a0 = 1.0 + alpha;
            bodyCoefs[i] = { static_cast<float> (alpha / a0), 0.0f, static_cast<float> (-alpha / a0), static_cast<float> (-2.0 * std::cos (w) / a0),
                             static_cast<float> ((1.0 - alpha) / a0) };
        }
    }
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

    // Effect memory: the pool starts again for this rate (every unit gives its memory back first; memory still on its way is freed).
    for (auto& chain : effects)
        for (auto& unit : chain)
            unit.attachMemory (nullptr);
    for (auto& chain : auxEffects)
        for (auto& unit : chain)
            unit.attachMemory (nullptr);
    for (auto& unit : masterEffects)
        unit.attachMemory (nullptr);
    {
        EffectMemory* m = nullptr;
        while (incomingMemory.pop (m))
            delete m;
    }
    memoryStore.clear();
    freeMemory.clear();
    for (int i = 0; i < kInitialEffectMemories; ++i)
    {
        auto m = std::make_unique<EffectMemory>();
        m->prepare (sampleRate);
        freeMemory.push_back (m.get());
        memoryStore.push_back (std::move (m));
    }
    memoryCount.store (static_cast<int> (memoryStore.size()), std::memory_order_relaxed);
    memoryShortfall.store (0, std::memory_order_relaxed);
    tracksInUse = 0;

    // The Time Shaper's recordings: made here, lent out while rendering (every unit gives its buffer back first).
    const auto timeFrames = static_cast<size_t> (kTimeBufferSeconds * sampleRate);
    for (auto& b : timeBuffers)
        b.assign (timeFrames, 0.0f);
    timeBufferOwner.fill (nullptr);
    for (auto& chain : effects)
        for (auto& unit : chain)
            unit.attachTimeBuffer (nullptr, nullptr, 0);
    for (auto& chain : auxEffects)
        for (auto& unit : chain)
            unit.attachTimeBuffer (nullptr, nullptr, 0);
    for (auto& unit : masterEffects)
        unit.attachTimeBuffer (nullptr, nullptr, 0);

    tickPos = 0.0;
    startPosition = 0.0;
    projectRevision = ~std::uint64_t { 0 };
    ageCounter = 0;
    wasPlaying = false;
}

template <class Visit>
void Player::forEachEffectSlot (const Project& project, const Visit& visit) noexcept
{
    const size_t visitTracks = std::min (std::max (project.tracks.size(), tracksInUse), static_cast<size_t> (kMaxTracks));
    for (size_t t = 0; t < visitTracks; ++t)
        for (size_t s = 0; s < static_cast<size_t> (kSlotsPerTrack); ++s)
            visit (effects[t][s], (t < project.tracks.size() ? project.tracks[t] : kDefaultTrack).slots[s]);
    for (size_t a = 0; a < static_cast<size_t> (kAuxBuses); ++a)
        for (size_t s = 0; s < static_cast<size_t> (kSlotsPerTrack); ++s)
            visit (auxEffects[a][s], project.console.aux[a].slots[s]);
    for (size_t s = 0; s < static_cast<size_t> (kSlotsPerTrack); ++s)
        visit (masterEffects[s], project.masterSlots[s]);
}

void Player::lendTimeBuffers (const Project& project) noexcept
{
    // Every unit whose slot is a Time Shaper gets a recording while one is free; a unit whose slot is something else gives its back.
    const auto visit = [this] (EffectUnit& unit, const EffectSlot& slot)
    {
        const bool wants = slot.type == EffectType::TimeShaper;
        if (wants == unit.hasTimeBuffer())
            return;
        if (! wants)
        {
            for (auto& owner : timeBufferOwner)
                if (owner == &unit)
                    owner = nullptr;
            unit.attachTimeBuffer (nullptr, nullptr, 0);
            return;
        }
        for (size_t b = 0; b < timeBufferOwner.size(); ++b)
            if (timeBufferOwner[b] == nullptr && ! timeBuffers[2 * b].empty())
            {
                timeBufferOwner[b] = &unit;
                unit.attachTimeBuffer (timeBuffers[2 * b].data(), timeBuffers[2 * b + 1].data(), static_cast<int> (timeBuffers[2 * b].size()));
                return;
            }
    };
    forEachEffectSlot (project, visit); // (tracks beyond the project's give theirs back)

    // How many Time Shapers are left without a recording, for the interface to say so.
    int without = 0;
    forEachEffectSlot (project, [&without] (const EffectUnit& unit, const EffectSlot& slot)
                       { without += slot.type == EffectType::TimeShaper && ! unit.hasTimeBuffer() ? 1 : 0; });
    timeShaperShortfallCount.store (without, std::memory_order_relaxed);
}

void Player::lendEffectMemory (const Project& project) noexcept
{
    // Memory made on another thread joins the pool (the vectors have room for all of it: no allocation here).
    EffectMemory* incoming = nullptr;
    while (incomingMemory.pop (incoming))
    {
        // (offerEffectMemory keeps the total within the capacity, so this never allocates)
        memoryStore.emplace_back (incoming);
        if (std::abs (incoming->preparedRate() - sampleRate) < 0.5) // made for this rate
            freeMemory.push_back (incoming); // made for another rate (the device changed meanwhile): kept, unused, until prepare()
    }
    memoryCount.store (static_cast<int> (memoryStore.size()), std::memory_order_relaxed);

    int shortfall = 0;
    const auto visit = [this, &shortfall] (EffectUnit& unit, const EffectSlot& slot)
    {
        const bool wants = effectNeedsMemory (slot.type);
        EffectMemory* has = unit.attachedMemory();
        if (wants == (has != nullptr))
            return;
        if (! wants)
        {
            unit.attachMemory (nullptr);
            freeMemory.push_back (has);
            return;
        }
        if (freeMemory.empty() && makesOwnMemory && memoryStore.size() < memoryStore.capacity())
        {
            auto m = std::make_unique<EffectMemory>(); // offline rendering only: allowed to allocate
            m->prepare (sampleRate);
            freeMemory.push_back (m.get());
            memoryStore.push_back (std::move (m));
        }
        if (freeMemory.empty())
        {
            ++shortfall;
            return;
        }
        unit.attachMemory (freeMemory.back());
        freeMemory.pop_back();
    };
    forEachEffectSlot (project, visit);
    memoryShortfall.store (shortfall, std::memory_order_relaxed);
    memoryCount.store (static_cast<int> (memoryStore.size()), std::memory_order_relaxed);
}

void Player::releaseVoices (bool includeSamplers) noexcept
{
    releasePluginNotes();
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
    if (e.reverse)
    {
        // The audio backwards: position p of the reversed audio is frame (frames - 1 - p) of the audio.
        v.reverse = true;
        v.pos = static_cast<double> (e.data->numFrames() - 1) - startFrame;
        v.step = -v.step;
    }
    const float pan = std::clamp (e.pan, -1.0f, 1.0f);
    v.balanceLeft = pan > 0.0f ? 1.0f - pan : 1.0f;
    v.balanceRight = pan < 0.0f ? 1.0f + pan : 1.0f;
    v.startTick = e.startTick;
    v.fadeInSeconds = static_cast<float> (e.fadeInSeconds);
    v.fadeOutSeconds = static_cast<float> (e.fadeOutSeconds);
    v.fade = 0.0f; // set from the song position at the first sample
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

    // A sounding plugin note follows its plugin (a channel may have moved); a note whose plugin is gone is forgotten (it may be freed).
    for (int i = numPluginNotes - 1; i >= 0; --i)
    {
        auto& note = pluginNotes[static_cast<size_t> (i)];
        int found = -1;
        for (size_t ch = 0; ch < project.channels.size() && ch < static_cast<size_t> (kMaxChannels) && found < 0; ++ch)
            if (project.channels[ch].plugin.get() == note.plugin)
                found = static_cast<int> (ch);
        if (found >= 0)
            note.channel = found;
        else
            pluginNotes[static_cast<size_t> (i)] = pluginNotes[static_cast<size_t> (--numPluginNotes)];
    }

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
    chunkSample = 0;
    noteOn (project.channels[static_cast<size_t> (channel)], channel, n, 1, true);
}

void Player::liveNoteOff (int channel, int pitch) noexcept
{
    chunkSample = 0;
    for (int i = numPluginNotes - 1; i >= 0; --i)
        if (pluginNotes[static_cast<size_t> (i)].live && pluginNotes[static_cast<size_t> (i)].channel == channel && pluginNotes[static_cast<size_t> (i)].pitch == pitch)
            pluginNoteOff (i);
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
    if (channel.instrument == Instrument::Plugin)
    {
        pluginNoteOn (channel, channelIndex, n, loopLength, live);
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
            // A Slicer program that cuts (0.37.0): the slice still sounding on this channel fades out in 5 ms, as in Slicex.
            if (channel.program->sliceCut)
                for (auto& other : voices)
                    if (other.sampler && other.pad < 0 && other.channel == channelIndex
                        && (other.state == State::Attack || other.state == State::Sustain))
                    {
                        other.state = State::Release;
                        other.relStep = static_cast<float> (1.0 / (0.005 * sampleRate));
                    }
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
    if (channel.synth.engine == SynthEngine::Subtractive)
        subtractiveVoiceOn (v, channel.synth, n.pitch);
    else if (channel.synth.engine == SynthEngine::FM)
        fmVoiceOn (v, channel.synth);
    else if (channel.synth.engine == SynthEngine::Additive)
        additiveVoiceOn (v, channel.synth, n.pitch);
    else if (channel.synth.engine == SynthEngine::Physical)
        physicalVoiceOn (v, channel.synth, static_cast<size_t> (target - voices.data()));

    *target = v;
}

void Player::subtractiveVoiceOn (Voice& v, const SynthPatch& patch, int pitch) noexcept
{
    v.subtractive = true;
    v.ratio1 = patch.osc[0].ratio();
    v.ratio2 = patch.osc[1].ratio();
    // Free-running oscillators would beat differently on every note; a fixed start keeps renders repeatable.
    v.ph1 = 0.0;
    v.ph2 = 0.25;
    v.noise = 22222u + static_cast<std::uint32_t> (v.age * 2654435761u);
    const auto seconds = [this] (float s, double minimum) { return std::max (minimum, static_cast<double> (s)) * sampleRate; };
    v.attackStep = static_cast<float> (1.0 / seconds (patch.amp.attack, 0.0005));
    v.sustainLevel = patch.amp.sustain;
    v.decayStep = static_cast<float> ((1.0 - patch.amp.sustain) / seconds (patch.amp.decay, 0.001));
    v.relStep = static_cast<float> (1.0 / seconds (patch.amp.release, 0.002));
    v.fenv = 0.0f;
    v.fstage = 0;
    v.fAttackStep = static_cast<float> (1.0 / seconds (patch.filterEnv.attack, 0.0005));
    v.fSustain = patch.filterEnv.sustain;
    v.fDecayStep = static_cast<float> ((1.0 - patch.filterEnv.sustain) / seconds (patch.filterEnv.decay, 0.001));
    v.fRelStep = static_cast<float> (1.0 / seconds (patch.filterEnv.release, 0.002));
    v.keyOctaves = patch.keyTrack * static_cast<float> (pitch - 60) / 12.0f;
    v.lfoPhase = 0.0f;
    v.lfoInc = static_cast<float> (patch.lfoRate / sampleRate);
    v.controlLeft = 0;
    v.filter = patch.filter;
    v.s1L = v.s2L = v.s1R = v.s2R = 0.0f;
}

namespace
{
// PolyBLEP: the correction that takes the step of a saw or square wave off its aliasing (t: phase 0..1, dt: phase step).
inline double polyBlep (double t, double dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

inline double oscillator (Waveform wave, double t, double dt, double pw) noexcept
{
    switch (wave)
    {
        case Waveform::Sine:     return std::sin (kTwoPi * t);
        case Waveform::Triangle: return 4.0 * std::abs (t - 0.5) - 1.0;
        case Waveform::Saw:      return 2.0 * t - 1.0 - polyBlep (t, dt);
        case Waveform::Square:
        {
            double t2 = t + 1.0 - pw;
            t2 -= std::floor (t2);
            return (t < pw ? 1.0 : -1.0) + polyBlep (t, dt) - polyBlep (t2, dt);
        }
    }
    return 0.0;
}

inline float lfoValue (Waveform wave, float t) noexcept
{
    switch (wave)
    {
        case Waveform::Sine:     return static_cast<float> (std::sin (kTwoPi * t));
        case Waveform::Triangle: return 1.0f - 4.0f * std::abs (t - 0.5f);
        case Waveform::Saw:      return 2.0f * t - 1.0f;
        case Waveform::Square:   return t < 0.5f ? 1.0f : -1.0f;
    }
    return 0.0f;
}
} // namespace

void Player::fmVoiceOn (Voice& v, const SynthPatch& patch) noexcept
{
    v.fm = true;
    const auto seconds = [this] (float s, double minimum) { return std::max (minimum, static_cast<double> (s)) * sampleRate; };
    v.attackStep = static_cast<float> (1.0 / seconds (patch.amp.attack, 0.0005));
    v.sustainLevel = patch.amp.sustain;
    v.decayStep = static_cast<float> ((1.0 - patch.amp.sustain) / seconds (patch.amp.decay, 0.001));
    v.relStep = static_cast<float> (1.0 / seconds (patch.amp.release, 0.002));
    for (size_t i = 0; i < static_cast<size_t> (kFmOperators); ++i)
    {
        const auto& e = patch.op[i].env;
        v.opPhase[i] = 0.0;
        v.opOut[i] = 0.0f;
        v.opEnv[i] = 0.0f;
        v.opStage[i] = 0;
        v.opAttack[i] = static_cast<float> (1.0 / seconds (e.attack, 0.0005));
        v.opSustain[i] = e.sustain;
        v.opDecay[i] = static_cast<float> ((1.0 - e.sustain) / seconds (e.decay, 0.001));
        v.opRelease[i] = static_cast<float> (1.0 / seconds (e.release, 0.002));
    }
    v.lfoPhase = 0.0f;
    v.lfoInc = static_cast<float> (patch.lfoRate / sampleRate);
    v.controlLeft = 0;
}

void Player::updateModulation (Voice& v, const SynthPatch& patch) noexcept
{
    const float value = lfoValue (patch.lfoWave, v.lfoPhase);
    const float lfo = patch.lfoTarget == LfoTarget::Off ? 0.0f : value * patch.lfoDepth;
    v.pitchMod = patch.lfoTarget == LfoTarget::Pitch ? std::pow (2.0, 2.0 * lfo / 12.0) : 1.0;
    v.ampMod = patch.lfoTarget == LfoTarget::Amp ? 1.0f - 0.5f * patch.lfoDepth * (1.0f - value) : 1.0f;
    v.pwMod = patch.lfoTarget == LfoTarget::PulseWidth ? 0.4f * lfo : 0.0f;
}

// One sample of an FM voice, before the amplitude envelope and velocity (which the caller applies).
float Player::renderFm (Voice& v, const SynthPatch& patch) noexcept
{
    if (--v.controlLeft <= 0)
    {
        v.controlLeft = kControlInterval;
        updateModulation (v, patch);
    }
    v.lfoPhase += v.lfoInc;
    if (v.lfoPhase >= 1.0f)
        v.lfoPhase -= 1.0f;

    constexpr float kIndex = 6.0f; // radians of phase modulation at a matrix amount of 1
    const bool releasing = v.state == State::Release;
    const double base = v.increment / kTwoPi * v.pitchMod;
    std::array<float, kFmOperators> next {};
    float mix = 0.0f, outSum = 0.0f;
    for (size_t i = 0; i < static_cast<size_t> (kFmOperators); ++i)
    {
        // The operator's envelope.
        auto& env = v.opEnv[i];
        if (releasing)
            v.opStage[i] = 2;
        if (v.opStage[i] == 0)
        {
            env += v.opAttack[i];
            if (env >= 1.0f) { env = 1.0f; v.opStage[i] = 1; }
        }
        else if (v.opStage[i] == 1)
            env = std::max (v.opSustain[i], env - v.opDecay[i]);
        else
            env = std::max (0.0f, env - v.opRelease[i]);

        const auto& row = patch.matrix.fm[i];
        float mod = 0.0f;
        for (size_t s = 0; s < static_cast<size_t> (kFmOperators); ++s)
            mod += row[s] * v.opOut[s];
        next[i] = static_cast<float> (std::sin (kTwoPi * v.opPhase[i] + static_cast<double> (kIndex * mod))) * patch.op[i].level * env;
        v.opPhase[i] += base * static_cast<double> (patch.op[i].ratio);
        v.opPhase[i] -= std::floor (v.opPhase[i]);
        mix += patch.matrix.out[i] * next[i];
        outSum += patch.matrix.out[i];
    }
    v.opOut = next;
    return mix / std::max (1.0f, outSum) * v.ampMod * patch.gain;
}

// One sample of a subtractive voice, before the amplitude envelope and velocity (which the caller applies like for every voice).
float Player::renderSubtractive (Voice& v, const SynthPatch& patch) noexcept
{
    // Filter envelope: attack to 1, decay to the sustain level, release from wherever it is when the note ends.
    if (v.state == State::Release && v.fstage != 2)
        v.fstage = 2;
    if (v.fstage == 0)
    {
        v.fenv += v.fAttackStep;
        if (v.fenv >= 1.0f) { v.fenv = 1.0f; v.fstage = 1; }
    }
    else if (v.fstage == 1)
        v.fenv = std::max (v.fSustain, v.fenv - v.fDecayStep);
    else
        v.fenv = std::max (0.0f, v.fenv - v.fRelStep);

    // Control rate: the LFO, the pitch modulation and the filter coefficients every kControlInterval samples.
    if (--v.controlLeft <= 0)
    {
        v.controlLeft = kControlInterval;
        updateModulation (v, patch);
        const float lfo = patch.lfoTarget == LfoTarget::Off ? 0.0f : lfoValue (patch.lfoWave, v.lfoPhase) * patch.lfoDepth;
        if (v.filter != SamplerFilter::Off)
        {
            const double octaves = 6.0 * patch.envAmount * v.fenv + v.keyOctaves + (patch.lfoTarget == LfoTarget::Cutoff ? 3.0 * lfo : 0.0);
            const double fc = std::clamp (SamplerSettings::cutoffHz (patch.cutoff) * std::pow (2.0, octaves), 20.0, 0.45 * sampleRate);
            const double g = std::tan (kTwoPi * 0.5 * fc / sampleRate);
            const double k = 2.0 - 1.94 * static_cast<double> (patch.resonance) * (patch.steep ? 0.8 : 1.0); // two in a row ring more
            const double a1 = 1.0 / (1.0 + g * (g + k));
            v.fk = static_cast<float> (k);
            v.fa1 = static_cast<float> (a1);
            v.fa2 = static_cast<float> (g * a1);
            v.fa3 = static_cast<float> (g * g * a1);
        }
    }
    v.lfoPhase += v.lfoInc;
    if (v.lfoPhase >= 1.0f)
        v.lfoPhase -= 1.0f;

    // Oscillators and noise.
    const double base = v.increment / kTwoPi * v.pitchMod; // cycles per sample of the note
    const double dt1 = std::min (0.5, base * v.ratio1);
    const double dt2 = std::min (0.5, base * v.ratio2);
    double x = 0.0;
    if (patch.osc[0].level > 0.0f)
        x += patch.osc[0].level * oscillator (patch.osc[0].wave, v.ph1, dt1, std::clamp (patch.osc[0].pulseWidth + v.pwMod, 0.05f, 0.95f));
    if (patch.osc[1].level > 0.0f)
        x += patch.osc[1].level * oscillator (patch.osc[1].wave, v.ph2, dt2, std::clamp (patch.osc[1].pulseWidth + v.pwMod, 0.05f, 0.95f));
    v.ph1 += dt1;
    v.ph1 -= std::floor (v.ph1);
    v.ph2 += dt2;
    v.ph2 -= std::floor (v.ph2);
    if (patch.noise > 0.0f)
    {
        v.noise ^= v.noise << 13;
        v.noise ^= v.noise >> 17;
        v.noise ^= v.noise << 5;
        x += patch.noise * (static_cast<double> (v.noise) / 2147483648.0 - 1.0);
    }
    auto y = static_cast<float> (x * 0.5); // two full oscillators stay near +-1

    if (v.filter != SamplerFilter::Off)
    {
        const auto svf = [&v] (float in, float& ic1, float& ic2)
        {
            const float v3 = in - ic2;
            const float v1 = v.fa1 * ic1 + v.fa2 * v3;
            const float v2 = ic2 + v.fa2 * ic1 + v.fa3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v.filter == SamplerFilter::LowPass ? v2 : v.filter == SamplerFilter::BandPass ? v1 : in - v.fk * v1 - v2;
        };
        y = svf (y, v.s1L, v.s2L);
        if (patch.steep)
            y = svf (y, v.s1R, v.s2R);
    }
    return y * v.ampMod * patch.gain;
}

// --- Additive engine (0.24.0) ---------------------------------------------------------------------------------------------
namespace
{
// The level of harmonic n (1-based) of a timbre at `hz`, before normalisation.
inline float timbreLevel (const AdditiveTimbre& t, int n, double hz) noexcept
{
    if (n > t.partials)
        return 0.0f;
    double a = std::pow (static_cast<double> (n), -2.5 * (1.0 - static_cast<double> (t.brightness)));
    if (n % 2 == 0)
        a *= t.evenLevel;
    if (t.formant > 0.0f)
    {
        const double width = 0.6 + 2.4 * static_cast<double> (t.formantWidth); // octaves (a fifth to three octaves)
        const double d = std::log2 (std::max (hz, 1.0) / AdditiveTimbre::formantHz (t.formantFreq)) / (0.5 * width);
        a *= 1.0 + 9.0 * static_cast<double> (t.formant) * std::exp (-0.5 * d * d);
    }
    return static_cast<float> (a);
}

// The magnitude of a 2-pole filter at f / fc = x (resonance 0..1).
inline double filterMagnitude (SamplerFilter type, double x, float resonance, bool steep) noexcept
{
    if (type == SamplerFilter::Off)
        return 1.0;
    const double q = 0.707 + 9.0 * static_cast<double> (resonance) * static_cast<double> (resonance);
    const double denominator = std::sqrt ((1.0 - x * x) * (1.0 - x * x) + (x / q) * (x / q));
    double m = type == SamplerFilter::LowPass ? 1.0 / denominator : type == SamplerFilter::HighPass ? x * x / denominator : (x / q) / denominator;
    if (steep)
        m *= m;
    return m;
}
} // namespace

void Player::additiveVoiceOn (Voice& v, const SynthPatch& patch, int pitch) noexcept
{
    v.additive = true;
    const auto seconds = [this] (float s, double minimum) { return std::max (minimum, static_cast<double> (s)) * sampleRate; };
    v.attackStep = static_cast<float> (1.0 / seconds (patch.amp.attack, 0.0005));
    v.sustainLevel = patch.amp.sustain;
    v.decayStep = static_cast<float> ((1.0 - patch.amp.sustain) / seconds (patch.amp.decay, 0.001));
    v.relStep = static_cast<float> (1.0 / seconds (patch.amp.release, 0.002));
    v.fenv = 0.0f;
    v.fstage = 0;
    v.fAttackStep = static_cast<float> (kControlInterval / seconds (patch.filterEnv.attack, 0.0005));
    v.fSustain = patch.filterEnv.sustain;
    v.fDecayStep = static_cast<float> (kControlInterval * (1.0 - patch.filterEnv.sustain) / seconds (patch.filterEnv.decay, 0.001));
    v.fRelStep = static_cast<float> (kControlInterval / seconds (patch.filterEnv.release, 0.002));
    v.menv = 0.0f;
    v.mstage = 0;
    v.mAttackStep = static_cast<float> (kControlInterval / seconds (patch.morphEnv.attack, 0.0005));
    v.mSustain = patch.morphEnv.sustain;
    v.mDecayStep = static_cast<float> (kControlInterval * (1.0 - patch.morphEnv.sustain) / seconds (patch.morphEnv.decay, 0.001));
    v.mRelStep = static_cast<float> (kControlInterval / seconds (patch.morphEnv.release, 0.002));
    v.keyOctaves = patch.keyTrack * static_cast<float> (pitch - 60) / 12.0f;
    v.lfoPhase = 0.0f;
    v.lfoInc = static_cast<float> (patch.lfoRate / sampleRate);
    v.controlLeft = 0;
    for (size_t i = 0; i < static_cast<size_t> (kAdditivePartials); ++i)
    {
        v.partRe[i] = 1.0f; // every harmonic starts at phase 0 (a sine), so a render is the same every time
        v.partIm[i] = 0.0f;
        v.partAmp[i] = 0.0f;
        v.partStep[i] = 0.0f;
        v.partCos[i] = 1.0f;
        v.partSin[i] = 0.0f;
    }
}

void Player::updateAdditive (Voice& v, const SynthPatch& patch) noexcept
{
    // The envelopes of the filter and the morph move once per control interval (their steps are per interval).
    const auto advance = [] (float& env, std::uint8_t& stage, bool releasing, float attack, float decay, float sustain, float release)
    {
        if (releasing)
            stage = 2;
        if (stage == 0)
        {
            env += attack;
            if (env >= 1.0f) { env = 1.0f; stage = 1; }
        }
        else if (stage == 1)
            env = std::max (sustain, env - decay);
        else
            env = std::max (0.0f, env - release);
    };
    const bool releasing = v.state == State::Release;
    advance (v.fenv, v.fstage, releasing, v.fAttackStep, v.fDecayStep, v.fSustain, v.fRelStep);
    advance (v.menv, v.mstage, releasing, v.mAttackStep, v.mDecayStep, v.mSustain, v.mRelStep);

    updateModulation (v, patch);
    const float lfo = patch.lfoTarget == LfoTarget::Off ? 0.0f : lfoValue (patch.lfoWave, v.lfoPhase) * patch.lfoDepth;
    const float morph = std::clamp (patch.morph + patch.morphEnvAmount * v.menv + (patch.lfoTarget == LfoTarget::Morph ? 0.5f * lfo : 0.0f), 0.0f, 1.0f);
    const double f0 = v.increment / kTwoPi * v.pitchMod; // cycles per sample
    const double octaves = 6.0 * patch.envAmount * v.fenv + v.keyOctaves + (patch.lfoTarget == LfoTarget::Cutoff ? 3.0 * lfo : 0.0);
    const double fc = std::clamp (SamplerSettings::cutoffHz (patch.cutoff) * std::pow (2.0, octaves), 20.0, 20000.0);
    const double stretch = 0.0008 * ((1.0 - morph) * patch.timbreA.stretch + morph * patch.timbreB.stretch);

    std::array<float, kAdditivePartials> a {}, b {};
    std::array<double, kAdditivePartials> freq {};
    double normA = 0.0, normB = 0.0;
    for (int n = 1; n <= kAdditivePartials; ++n)
    {
        const auto i = static_cast<size_t> (n - 1);
        freq[i] = f0 * n * std::sqrt (1.0 + stretch * n * n);
        if (freq[i] >= 0.45)
            continue; // above the limit: silent (no aliasing)
        const double hz = freq[i] * sampleRate;
        a[i] = timbreLevel (patch.timbreA, n, hz);
        b[i] = timbreLevel (patch.timbreB, n, hz);
        normA += static_cast<double> (a[i]) * a[i];
        normB += static_cast<double> (b[i]) * b[i];
    }
    const float ka = normA > 0.0 ? static_cast<float> (0.82 / std::sqrt (normA)) : 0.0f;
    const float kb = normB > 0.0 ? static_cast<float> (0.82 / std::sqrt (normB)) : 0.0f;
    constexpr float step = 1.0f / static_cast<float> (kControlInterval);
    for (size_t i = 0; i < static_cast<size_t> (kAdditivePartials); ++i)
    {
        float target = 0.0f;
        if (freq[i] < 0.45)
        {
            const double gain = filterMagnitude (patch.filter, freq[i] * sampleRate / fc, patch.resonance, patch.steep);
            target = static_cast<float> (((1.0f - morph) * a[i] * ka + morph * b[i] * kb) * std::min (gain, 8.0));
            const double w = kTwoPi * freq[i];
            v.partCos[i] = static_cast<float> (std::cos (w));
            v.partSin[i] = static_cast<float> (std::sin (w));
        }
        v.partStep[i] = (target - v.partAmp[i]) * step;
        // keep the phasor on the unit circle (rounding makes it drift slowly)
        const float m = v.partRe[i] * v.partRe[i] + v.partIm[i] * v.partIm[i];
        const float k = 1.5f - 0.5f * m;
        v.partRe[i] *= k;
        v.partIm[i] *= k;
    }
}

float Player::renderAdditive (Voice& v, const SynthPatch& patch) noexcept
{
    if (--v.controlLeft <= 0)
    {
        v.controlLeft = kControlInterval;
        updateAdditive (v, patch);
    }
    v.lfoPhase += v.lfoInc;
    if (v.lfoPhase >= 1.0f)
        v.lfoPhase -= 1.0f;

    float x = 0.0f;
    for (size_t i = 0; i < static_cast<size_t> (kAdditivePartials); ++i)
    {
        x += v.partAmp[i] * v.partIm[i];
        const float re = v.partRe[i] * v.partCos[i] - v.partIm[i] * v.partSin[i];
        v.partIm[i] = v.partRe[i] * v.partSin[i] + v.partIm[i] * v.partCos[i];
        v.partRe[i] = re;
        v.partAmp[i] += v.partStep[i];
    }
    return x * v.ampMod * patch.gain;
}

// --- Physical engine (0.24.0) ---------------------------------------------------------------------------------------------
namespace
{
struct ModalModel
{
    std::array<float, 12> ratio, amp, decay; // frequency / fundamental, level, decay time relative to the fundamental's
    int count;
    float length; // the fundamental's ring time relative to PhysicalSettings::decaySeconds
};

const ModalModel& modalModel (PhysicalModel m) noexcept
{
    static const ModalModel marimba { { 1.0f, 3.99f, 10.65f }, { 1.0f, 0.5f, 0.25f }, { 1.0f, 0.3f, 0.1f }, 3, 0.6f };
    static const ModalModel vibraphone { { 1.0f, 4.0f, 10.1f }, { 1.0f, 0.35f, 0.15f }, { 1.0f, 0.45f, 0.2f }, 3, 1.0f };
    static const ModalModel glass { { 1.0f, 2.32f, 4.25f, 6.63f, 9.38f, 12.9f }, { 1.0f, 0.7f, 0.5f, 0.35f, 0.25f, 0.15f },
                                    { 1.0f, 0.8f, 0.6f, 0.5f, 0.4f, 0.3f }, 6, 0.8f };
    // a church bell: hum, prime, tierce, quint, nominal and the upper partials
    static const ModalModel bell { { 0.5f, 1.0f, 1.183f, 1.506f, 2.0f, 2.514f, 2.662f, 3.011f, 4.166f, 5.433f },
                                   { 0.6f, 1.0f, 0.8f, 0.5f, 0.7f, 0.4f, 0.35f, 0.3f, 0.25f, 0.2f },
                                   { 1.3f, 1.0f, 0.8f, 0.7f, 0.6f, 0.5f, 0.45f, 0.4f, 0.3f, 0.25f }, 10, 1.4f };
    // a struck plate: dense, inharmonic
    static const ModalModel metal { { 1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f, 3.6f, 3.652f, 4.06f, 4.6f },
                                    { 1.0f, 0.85f, 0.72f, 0.61f, 0.52f, 0.44f, 0.38f, 0.32f, 0.27f, 0.23f, 0.2f, 0.17f },
                                    { 1.0f, 0.87f, 0.77f, 0.69f, 0.63f, 0.57f, 0.53f, 0.49f, 0.45f, 0.43f, 0.4f, 0.38f }, 12, 1.0f };
    switch (m)
    {
        case PhysicalModel::Marimba: return marimba;
        case PhysicalModel::Vibraphone: return vibraphone;
        case PhysicalModel::Glass: return glass;
        case PhysicalModel::Bell: return bell;
        case PhysicalModel::Metal:
        case PhysicalModel::String: break;
    }
    return metal;
}

// The phase delay (in samples) of the first-order all-pass (a + z^-1) / (1 + a z^-1) at `w` radians per sample.
inline double allpassDelay (double a, double w) noexcept
{
    const double phase = std::atan2 (-std::sin (w), a + std::cos (w)) - std::atan2 (-a * std::sin (w), 1.0 + a * std::cos (w));
    return -phase / w;
}
} // namespace

void Player::physicalVoiceOn (Voice& v, const SynthPatch& patch, size_t voiceIndex) noexcept
{
    const auto& ph = patch.physical;
    v.physical = true;
    v.modal = ph.model != PhysicalModel::String;
    v.released = false;
    const auto seconds = [this] (float s, double minimum) { return std::max (minimum, static_cast<double> (s)) * sampleRate; };
    v.attackStep = static_cast<float> (1.0 / seconds (patch.amp.attack, 0.0003));
    v.sustainLevel = 1.0f; // the model decays by itself
    v.decayStep = 0.0f;
    v.relStep = static_cast<float> (1.0 / seconds (patch.amp.release, 0.002));
    v.lfoPhase = 0.0f;
    v.lfoInc = static_cast<float> (patch.lfoRate / sampleRate);
    v.controlLeft = 0;
    v.pitchMod = 1.0;
    v.noise = 0x9e3779b9u ^ static_cast<std::uint32_t> (v.age * 2654435761u);
    const double f0 = std::max (20.0, v.increment / kTwoPi * sampleRate); // Hz
    const double ring = PhysicalSettings::decaySeconds (ph.decay);
    const double releaseTime = 0.03 + 3.0 * (1.0 - ph.releaseDamp) * (1.0 - ph.releaseDamp);
    const float strength = 0.25f + 0.75f * v.velocity; // a harder hit is louder (and, for strings, brighter below)

    if (! v.modal)
    {
        // Karplus-Strong: the loop delays one period minus what its filters delay; the loss filter (1 - S) + S z^-1 darkens every
        // round trip, the loop gain sets the ring time, a first-order all-pass makes a stiff string's upper partials sharp.
        v.line = stringLines.data() + voiceIndex * static_cast<size_t> (stringLineLength);
        v.lineMask = stringLineLength - 1;
        v.writePos = 0;
        v.loopBlend = 0.02f + 0.48f * ph.damping;
        v.apCoef = -0.85f * ph.stiffness;
        v.apIn = v.apOut = v.lossState = 0.0f;
        v.sustainGain = static_cast<float> (std::pow (10.0, -3.0 / (ring * f0)));
        v.releaseGain = ph.releaseDamp > 0.0f ? static_cast<float> (std::pow (10.0, -3.0 / (releaseTime * f0))) : v.sustainGain;
        v.loopGain = v.sustainGain;
        v.bodyAmount = ph.body;
        v.bodyState.fill (0.0f);
        v.dcIn = v.dcOut = 0.0f;
        updatePhysical (v, patch);

        // The pluck: one period of noise, low-passed by the hardness (and the velocity), with the comb of the pluck position.
        const int period = std::clamp (static_cast<int> (v.delaySamples), 2, stringLineLength - 8);
        const int start = (v.writePos - period) & v.lineMask;
        const float c = std::clamp (0.04f + 0.9f * ph.hardness * ph.hardness * (0.5f + 0.5f * v.velocity), 0.02f, 0.98f);
        float lp = 0.0f, peak = 0.0f;
        for (int k = 0; k < period; ++k)
        {
            v.noise ^= v.noise << 13;
            v.noise ^= v.noise >> 17;
            v.noise ^= v.noise << 5;
            const float white = static_cast<float> (static_cast<double> (v.noise) / 2147483648.0 - 1.0);
            lp += c * (white - lp);
            v.line[(start + k) & v.lineMask] = lp;
        }
        const int offset = std::clamp (static_cast<int> ((0.05 + 0.45 * ph.position) * period), 1, period - 1);
        for (int k = period - 1; k >= offset; --k) // the comb, from the end so the original values are still there
            v.line[(start + k) & v.lineMask] -= v.line[(start + k - offset) & v.lineMask];
        float mean = 0.0f;
        for (int k = 0; k < period; ++k)
            mean += v.line[(start + k) & v.lineMask] / static_cast<float> (period);
        for (int k = 0; k < period; ++k)
        {
            auto& x = v.line[(start + k) & v.lineMask];
            x -= mean; // no DC in the loop
            peak = std::max (peak, std::abs (x));
        }
        const float norm = peak > 0.0f ? 0.8f * strength / peak : 0.0f;
        for (int k = 0; k < period; ++k)
            v.line[(start + k) & v.lineMask] *= norm;
        for (int k = 1; k <= 4; ++k)
            v.line[(start - k) & v.lineMask] = 0.0f;
        return;
    }

    // Struck models: resonators at the model's ratios, rung by a raised-cosine pulse whose length is the mallet's softness.
    const auto& model = modalModel (ph.model);
    v.modeCount = std::min (model.count, Voice::kModes);
    double sumSquares = 0.0;
    for (size_t k = 0; k < static_cast<size_t> (v.modeCount); ++k)
    {
        const double position = std::abs (std::sin (3.14159265358979323846 * (static_cast<double> (k) + 1.0) * (0.08 + 0.42 * ph.position)));
        v.modeRatio[k] = model.ratio[k];
        v.modeAmp[k] = static_cast<float> (model.amp[k] * (0.35 + 0.65 * position));
        const double t60 = ring * model.length * std::pow (static_cast<double> (model.decay[k]), 0.5 + 1.5 * static_cast<double> (ph.damping));
        v.modeR[k] = static_cast<float> (std::pow (10.0, -3.0 / (std::max (0.01, t60) * sampleRate)));
        v.modeRelR[k] = ph.releaseDamp > 0.0f ? std::min (v.modeR[k], static_cast<float> (std::pow (10.0, -3.0 / (releaseTime * sampleRate)))) : v.modeR[k];
        v.modeY1[k] = v.modeY2[k] = 0.0f;
        sumSquares += static_cast<double> (v.modeAmp[k]) * v.modeAmp[k];
    }
    const double softness = (1.0 - static_cast<double> (ph.hardness)) * (1.0 - 0.5 * static_cast<double> (v.velocity));
    v.strikeLength = std::max (2, static_cast<int> ((0.0003 + 0.006 * softness * softness) * sampleRate));
    v.strikeLeft = v.strikeLength;
    v.strikeNorm = static_cast<float> (strength * 0.7 / std::sqrt (std::max (sumSquares, 1e-6)) / (0.5 * v.strikeLength));
    updatePhysical (v, patch);
}

void Player::updatePhysical (Voice& v, const SynthPatch& patch) noexcept
{
    updateModulation (v, patch);
    if (v.state == State::Release && ! v.released)
        v.released = true;
    const double w0 = v.increment * v.pitchMod; // radians per sample of the fundamental
    if (! v.modal)
    {
        const double period = kTwoPi / std::max (w0, 1e-6);
        v.apDelay = static_cast<float> (allpassDelay (v.apCoef, w0));
        v.delaySamples = std::clamp (period - v.loopBlend - v.apDelay, 2.0, static_cast<double> (stringLineLength - 8));
        v.loopGain = v.released ? v.releaseGain : v.sustainGain;
        return;
    }
    for (size_t k = 0; k < static_cast<size_t> (v.modeCount); ++k)
    {
        const double w = w0 * v.modeRatio[k];
        if (w >= 0.45 * kTwoPi)
        {
            v.modeB0[k] = v.modeA1[k] = v.modeA2[k] = 0.0f; // above the limit: silent
            continue;
        }
        const float r = v.released ? v.modeRelR[k] : v.modeR[k];
        v.modeA1[k] = static_cast<float> (2.0 * r * std::cos (w));
        v.modeA2[k] = r * r;
        v.modeB0[k] = static_cast<float> (std::sin (w)) * v.modeAmp[k];
    }
}

float Player::renderPhysical (Voice& v, const SynthPatch& patch) noexcept
{
    if (--v.controlLeft <= 0)
    {
        v.controlLeft = kControlInterval;
        updatePhysical (v, patch);
    }
    v.lfoPhase += v.lfoInc;
    if (v.lfoPhase >= 1.0f)
        v.lfoPhase -= 1.0f;

    float out = 0.0f;
    if (! v.modal)
    {
        // read one loop delay back (linear interpolation), filter, write
        const double readPos = static_cast<double> (v.writePos) - v.delaySamples;
        const double floorPos = std::floor (readPos);
        const auto i0 = static_cast<int> (floorPos);
        const auto frac = static_cast<float> (readPos - floorPos);
        const float a = v.line[i0 & v.lineMask], b = v.line[(i0 + 1) & v.lineMask];
        const float y = a + frac * (b - a);
        const float lossy = v.loopGain * ((1.0f - v.loopBlend) * y + v.loopBlend * v.lossState);
        v.lossState = y;
        const float dispersed = v.apCoef * (lossy - v.apOut) + v.apIn; // first-order all-pass
        v.apIn = lossy;
        v.apOut = dispersed;
        v.line[v.writePos] = dispersed;
        v.writePos = (v.writePos + 1) & v.lineMask;
        out = y;
        if (v.bodyAmount > 0.0f)
        {
            float body = 0.0f;
            for (size_t f = 0; f < 3; ++f)
            {
                const auto& c = bodyCoefs[f];
                float& z1 = v.bodyState[2 * f];
                float& z2 = v.bodyState[2 * f + 1];
                const float o = c[0] * y + z1;
                z1 = c[1] * y - c[3] * o + z2;
                z2 = c[2] * y - c[4] * o;
                body += o;
            }
            out += 1.6f * v.bodyAmount * body;
        }
        // no DC
        const float hp = out - v.dcIn + 0.995f * v.dcOut;
        v.dcIn = out;
        v.dcOut = hp;
        out = hp;
    }
    else
    {
        float x = 0.0f;
        if (v.strikeLeft > 0)
        {
            const float t = static_cast<float> (v.strikeLength - v.strikeLeft) / static_cast<float> (v.strikeLength);
            x = 0.5f * (1.0f - std::cos (static_cast<float> (kTwoPi) * t)) * v.strikeNorm;
            --v.strikeLeft;
        }
        for (size_t k = 0; k < static_cast<size_t> (v.modeCount); ++k)
        {
            const float y = v.modeB0[k] * x + v.modeA1[k] * v.modeY1[k] - v.modeA2[k] * v.modeY2[k];
            v.modeY2[k] = v.modeY1[k];
            v.modeY1[k] = y;
            out += y;
        }
    }
    return out * v.ampMod * patch.gain;
}

void Player::pluginEvent (int channelIndex, std::uint8_t status, int data1, int data2) noexcept
{
    if (channelIndex < 0 || channelIndex >= kMaxChannels)
        return;
    auto& count = pluginEventCount[static_cast<size_t> (channelIndex)];
    if (count >= kMaxPluginEvents)
        return;
    auto& e = pluginEvents[static_cast<size_t> (channelIndex)][static_cast<size_t> (count++)];
    e.offset = chunkSample;
    e.bytes[0] = status;
    e.bytes[1] = static_cast<std::uint8_t> (std::clamp (data1, 0, 127));
    e.bytes[2] = static_cast<std::uint8_t> (std::clamp (data2, 0, 127));
}

void Player::pluginNoteOn (const Channel& channel, int channelIndex, const Note& n, int loopLength, bool live) noexcept
{
    if (channel.plugin == nullptr)
        return;
    // The same key still sounding on this plugin: end it first (a plugin may not expect two note-ons for one key).
    for (int i = numPluginNotes - 1; i >= 0; --i)
        if (pluginNotes[static_cast<size_t> (i)].plugin == channel.plugin.get() && pluginNotes[static_cast<size_t> (i)].pitch == n.pitch)
            pluginNoteOff (i);
    if (numPluginNotes >= kMaxPluginNotes)
        pluginNoteOff (0); // the oldest makes room
    pluginEvent (channelIndex, 0x90, n.pitch, std::max (1, static_cast<int> (std::lround (n.velocity * 127.0f))));
    PluginNote note;
    note.plugin = channel.plugin.get();
    note.channel = channelIndex;
    note.pitch = n.pitch;
    note.live = live;
    note.endTick = live ? INT_MAX : std::min (n.startTick + std::max (1, n.lengthTicks), loopLength);
    pluginNotes[static_cast<size_t> (numPluginNotes++)] = note;
}

void Player::pluginNoteOff (int noteIndex) noexcept
{
    if (noteIndex < 0 || noteIndex >= numPluginNotes)
        return;
    const auto note = pluginNotes[static_cast<size_t> (noteIndex)];
    pluginEvent (note.channel, 0x80, note.pitch, 0);
    pluginNotes[static_cast<size_t> (noteIndex)] = pluginNotes[static_cast<size_t> (--numPluginNotes)];
}

void Player::releasePluginNotes() noexcept
{
    for (int i = numPluginNotes - 1; i >= 0; --i)
        if (! pluginNotes[static_cast<size_t> (i)].live)
            pluginNoteOff (i);
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
    // 0.61.0: the start point, the reverse and the fades of the sampler channel (a looping note keeps to its loop: no fade out)
    const double frames = static_cast<double> (sample->numFrames());
    v.pos = loops ? 0.0 : std::floor (static_cast<double> (set.start) * frames);
    v.fadeFrom = static_cast<float> (v.pos);
    v.mirror = set.reverse && ! loops;
    v.fadeInFrames = static_cast<float> (static_cast<double> (set.fadeIn) * frames);
    v.fadeOutFrames = loops ? 0.0f : static_cast<float> (static_cast<double> (set.fadeOut) * frames);
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
template <typename SetChannel, typename SetTrack, typename SetMix>
void applyAutomation (const Project& project, double tick, size_t numChannels, size_t numTracks, SetChannel& setChannel, SetTrack& setTrack,
                      SetMix& setMix,
                      const std::array<float, kMaxChannels>& channelVolume, const std::array<float, kMaxChannels>& channelPan,
                      const std::array<float, kMaxTracks>& trackVolume, const std::array<float, kMaxTracks>& trackPan,
                      std::vector<std::array<EffectSlot, kSlotsPerTrack>>& trackSlots,
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
                    if (idx < numTracks)
                        setTrack (idx, v, trackPan[idx]);
                    break;
                case AutomationKind::TrackPan:
                    if (idx < numTracks)
                        setTrack (idx, trackVolume[idx], v);
                    break;
                case AutomationKind::TrackEffect:
                    if (idx < numTracks)
                        trackSlots[idx][static_cast<size_t> (target.slot)].params[static_cast<size_t> (target.param)] = v;
                    break;
                case AutomationKind::MasterEffect:
                    masterSlots[static_cast<size_t> (target.slot)].params[static_cast<size_t> (target.param)] = v;
                    break;
                // Plugin parameters (0.35.0): the plugin takes the value before it processes this chunk. setParameter() skips a
                // value that did not change, so a held value costs nothing.
                case AutomationKind::ChannelPlugin:
                    if (idx < numChannels && project.channels[idx].instrument == Instrument::Plugin && project.channels[idx].plugin != nullptr)
                        project.channels[idx].plugin->setParameter (target.param, v);
                    break;
                case AutomationKind::TrackPlugin:
                    if (idx < numTracks)
                        if (auto& plugin = trackSlots[idx][static_cast<size_t> (target.slot)].plugin; plugin != nullptr)
                            plugin->setParameter (target.param, v);
                    break;
                case AutomationKind::MasterPlugin:
                    if (auto& plugin = masterSlots[static_cast<size_t> (target.slot)].plugin; plugin != nullptr)
                        plugin->setParameter (target.param, v);
                    break;
                // Sends, aux buses and VCA groups (0.47.0).
                case AutomationKind::TrackSend:
                    if (idx < numTracks)
                        setMix (target.kind, idx, static_cast<size_t> (target.slot), v);
                    break;
                case AutomationKind::AuxVolume:
                case AutomationKind::AuxPan:
                case AutomationKind::VcaLevel:
                    setMix (target.kind, idx, static_cast<size_t> (target.slot), v);
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
    // Tempo changes (0.46.0): the tempo where the song is, recomputed for every chunk below when the song has markers.
    const bool tempoChanges = ! project.tempoMarkers.empty();
    double ticksPerSample = (tempoChanges ? tempoAt (project.tempoMarkers, bpm, tickPos) : bpm) * kTicksPerBeat / (60.0 * sampleRate);
    ticksPerSampleNow = ticksPerSample;
    const int loopLength = std::max (1, project.lengthTicks);
    const size_t numMixerChannels = std::min (project.channels.size(), static_cast<size_t> (kMaxChannels));
    const size_t numTracks = std::clamp (project.tracks.size(), static_cast<size_t> (1), static_cast<size_t> (kMaxTracks));

    if (project.revision != projectRevision)
    {
        projectRevision = project.revision;
        onProjectChanged (project);
    }

    double ticksPerSecond = ticksPerSample * sampleRate;
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
        routedTo[ch] = static_cast<size_t> (std::clamp (c.mixerTrack, 0, static_cast<int> (numTracks) - 1));
        setChannel (ch, c.volume, c.pan);

        if (smoothedGain[ch] < 0.0f)
            smoothedGain[ch] = targetGain[ch]; // no fade-in on the very first block
    }

    // Per-block track state. Track pan is a balance control: the centre is unity, the far side fades out.
    const auto trackAt = [&project] (size_t t) -> const MixerTrack&
    {
        return t < project.tracks.size() ? project.tracks[t] : kDefaultTrack;
    };

    // Tracks added since the last block start without smoothing (no fade from what an earlier track there left).
    for (size_t t = tracksInUse; t < numTracks; ++t)
    {
        trackGain[t] = sendGate[t] = kUnsetGain;
        sendGain[t].fill (kUnsetGain);
        trackPeaks[t] = trackMeanSquare[t] = 0.0f;
    }

    bool anyTrackSolo = false;
    for (size_t t = 0; t < numTracks; ++t)
        anyTrackSolo = anyTrackSolo || trackAt (t).solo;

    std::array<float, kMaxTracks> trackTarget {};
    std::array<float, kMaxTracks> balanceLeft {};
    std::array<float, kMaxTracks> balanceRight {};
    std::array<bool, kMaxTracks> trackAudible {};

    // VCA groups: a track's fader is scaled by the level of every group it belongs to (a muted group silences its members).
    std::array<float, kMaxTracks> trackVca {};
    for (size_t t = 0; t < numTracks; ++t)
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

    for (size_t t = 0; t < numTracks; ++t)
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
    std::array<float, kAuxBuses> auxTarget {}, auxBalanceLeft {}, auxBalanceRight {}, auxVolume {}, auxPan {};
    const auto setAux = [&] (size_t a, float volume, float pan)
    {
        auxVolume[a] = volume;
        auxPan[a] = pan;
        auxTarget[a] = project.console.aux[a].mute ? 0.0f : std::clamp (volume, 0.0f, 1.5f);
        const float p = std::clamp (pan, -1.0f, 1.0f);
        auxBalanceLeft[a] = p > 0.0f ? 1.0f - p : 1.0f;
        auxBalanceRight[a] = p < 0.0f ? 1.0f + p : 1.0f;
    };
    for (size_t a = 0; a < static_cast<size_t> (kAuxBuses); ++a)
    {
        setAux (a, project.console.aux[a].volume, project.console.aux[a].pan);
        if (auxGain[a] < 0.0f)
            auxGain[a] = auxTarget[a];
    }

    // What the tracks send to the aux buses (the project's own levels, or what automation says, 0.47.0), and the VCA levels.
    auto& sendLevel = sendLevelNow;
    for (size_t t = 0; t < numTracks; ++t)
        sendLevel[t] = trackAt (t).sends;
    std::array<float, kVcaGroups> vcaLevel = project.console.vcaLevel;
    const auto setMix = [&] (AutomationKind kind, size_t index, size_t slot, float v)
    {
        if (kind == AutomationKind::TrackSend && slot < static_cast<size_t> (kAuxBuses))
            sendLevel[index][slot] = std::clamp (v, 0.0f, 1.5f);
        else if (kind == AutomationKind::AuxVolume && slot < static_cast<size_t> (kAuxBuses))
            setAux (slot, v, auxPan[slot]);
        else if (kind == AutomationKind::AuxPan && slot < static_cast<size_t> (kAuxBuses))
            setAux (slot, auxVolume[slot], v);
        else if (kind == AutomationKind::VcaLevel && slot < static_cast<size_t> (kVcaGroups) && std::abs (vcaLevel[slot] - v) > 1.0e-6f)
        {
            vcaLevel[slot] = v;
            for (size_t t = 0; t < numTracks; ++t)
                if ((trackAt (t).vcaMask >> slot) & 1u)
                {
                    float level = 1.0f;
                    for (int g = 0; g < kVcaGroups; ++g)
                        if ((trackAt (t).vcaMask >> g) & 1u)
                            level *= project.console.vcaMute[static_cast<size_t> (g)] ? 0.0f : std::clamp (vcaLevel[static_cast<size_t> (g)], 0.0f, 1.5f);
                    trackVca[t] = level;
                    setTrack (t, trackVolume[t], trackPan[t]);
                }
        }
    };

    // Automation (Song mode): effect parameters are changed in copies of the slots, so the project itself is never touched.
    const bool automated = playing && ! project.automation.empty();
    auto& trackSlotsNow = slotsNow;
    std::array<EffectSlot, kSlotsPerTrack> masterSlotsNow {};
    if (automated)
    {
        for (size_t t = 0; t < numTracks; ++t)
            trackSlotsNow[t] = trackAt (t).slots;
        masterSlotsNow = project.masterSlots;
    }

    peaks.fill (0.0f);
    std::fill_n (trackPeaks.begin(), std::max (numTracks, tracksInUse), 0.0f);
    auxPeaks.fill (0.0f);

    lendTimeBuffers (project);
    lendEffectMemory (project);
    tracksInUse = numTracks;

    // A track with no sound coming in and no effect in its chain is silent: it skips the per-sample work (its fader and sends jump to
    // where they are going, its meter decays as it would have).
    const float rmsDecayPerChunk = std::pow (1.0f - rmsCoef, static_cast<float> (kChunk));
    for (int done = 0; done < numSamples; done += kChunk)
    {
        const int n = std::min (kChunk, numSamples - done);
        if (tempoChanges)
        {
            ticksPerSample = tempoAt (project.tempoMarkers, bpm, tickPos) * kTicksPerBeat / (60.0 * sampleRate);
            ticksPerSecond = ticksPerSample * sampleRate;
            ticksPerSampleNow = ticksPerSample;
        }
        // The highest note each channel sounds now, for a Pitch Corrector that follows one (0.41.0).
        heldNotes.fill (-1);
        for (const auto& v : voices)
            if ((v.state == State::Attack || v.state == State::Sustain) && v.channel >= 0 && v.channel < kMaxChannels)
                heldNotes[static_cast<size_t> (v.channel)] = std::max (heldNotes[static_cast<size_t> (v.channel)], v.livePitch);
        const EffectTransport transport { playing ? tickPos : -1.0, ticksPerSample, heldNotes.data(), kMaxChannels }; // where the song is at this chunk's start

        if (automated)
            applyAutomation (project, tickPos, numMixerChannels, numTracks, setChannel, setTrack, setMix, channelVolume, channelPan, trackVolume, trackPan,
                             trackSlotsNow, masterSlotsNow);

        for (size_t t = 0; t < numTracks; ++t)
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
        // The time signature (0.46.0): a beat is a quarter or an eighth, and the accent comes every `beatsPerBar` beats.
        const int meterBeatTicks = kTicksPerBeat * 4 / (project.beatUnit == 8 ? 8 : 4);
        const int meterBeats = std::clamp (project.beatsPerBar, 1, 16);
        const double samplesPerBeat = 60.0 * sampleRate / std::max (bpm, 1.0) * meterBeatTicks / kTicksPerBeat;
        if (pendingClickDelay >= 0.0)
        {
            freeBeatSamples = pendingClickDelay > 0.0 ? samplesPerBeat - pendingClickDelay : 1.0e18;
            freeBeatCount = 0;
            pendingClickDelay = -1.0;
        }

        for (int i = 0; i < n; ++i)
        {
            if (metronomeOn)
            {
                if (playing)
                {
                    const int beat = static_cast<int> (tickPos / meterBeatTicks);
                    if (beat != lastClickBeat)
                    {
                        // switched on in the middle of a beat: wait for the next one instead of clicking off the beat
                        const bool joinedMidBeat = lastClickBeat < 0 && std::fmod (tickPos, static_cast<double> (meterBeatTicks)) > 0.5;
                        lastClickBeat = beat;
                        if (! joinedMidBeat)
                            startClick (beat % meterBeats == 0);
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
                        startClick (freeBeatCount % meterBeats == 0);
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

            chunkSample = i;
            if (playing)
            {
                for (int k = numPluginNotes - 1; k >= 0; --k)
                    if (! pluginNotes[static_cast<size_t> (k)].live && tickPos >= static_cast<double> (pluginNotes[static_cast<size_t> (k)].endTick))
                        pluginNoteOff (k);

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

                const int frames = v.data->numFrames();
                if (v.pos < 0.0 || static_cast<int> (v.pos) + 1 >= frames + (v.reverse ? 1 : 0))
                {
                    v = ClipVoice {}; // the end of the audio (its beginning, played backwards)
                    continue;
                }
                const int idx = static_cast<int> (v.pos);
                const int next = std::min (idx + 1, frames - 1);
                const float frac = static_cast<float> (v.pos - idx);
                const float* sl = v.data->left.data();
                const float* sr = v.data->right.data();
                if (playing && (v.fadeInSeconds > 0.0f || v.fadeOutSeconds > 0.0f))
                {
                    // Sine fades over the clip's first and last seconds, from where the song is.
                    const double sinceStart = (tickPos - v.startTick) / ticksPerSecond;
                    const double untilEnd = (v.endTick - tickPos) / ticksPerSecond;
                    double f = 1.0;
                    if (v.fadeInSeconds > 0.0f)
                        f *= std::sin (0.25 * kTwoPi * std::clamp (sinceStart / v.fadeInSeconds, 0.0, 1.0));
                    if (v.fadeOutSeconds > 0.0f)
                        f *= std::sin (0.25 * kTwoPi * std::clamp (untilEnd / v.fadeOutSeconds, 0.0, 1.0));
                    v.fade = static_cast<float> (f);
                }
                else if (playing)
                    v.fade = 1.0f;
                const float gain = v.env * v.gain * v.fade;
                trackL[static_cast<size_t> (v.track)][static_cast<size_t> (i)] += (sl[idx] + frac * (sl[next] - sl[idx])) * gain * v.balanceLeft;
                trackR[static_cast<size_t> (v.track)][static_cast<size_t> (i)] += (sr[idx] + frac * (sr[next] - sr[idx])) * gain * v.balanceRight;
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
                    // 0.61.0: reversed, the same walk reads the sample from its end
                    const int ri = v.mirror ? frames - 1 - idx : idx;
                    const int rn = v.mirror ? frames - 1 - next : next;
                    float xl = sl[ri] + frac * (sl[rn] - sl[ri]);
                    float xr = sr[ri] + frac * (sr[rn] - sr[ri]);
                    if (v.fadeInFrames > 0.0f || v.fadeOutFrames > 0.0f)
                    {
                        float fade = 1.0f;
                        if (v.fadeInFrames > 0.0f)
                            fade = std::min (fade, (static_cast<float> (v.pos) - v.fadeFrom) / v.fadeInFrames);
                        if (v.fadeOutFrames > 0.0f)
                            fade = std::min (fade, static_cast<float> (frames - 1 - v.pos) / v.fadeOutFrames);
                        fade = std::clamp (fade, 0.0f, 1.0f);
                        xl *= fade;
                        xr *= fade;
                    }
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
                else if (v.subtractive)
                {
                    // The patch of the voice's channel in this snapshot (a channel that changed instrument meanwhile still has one).
                    const auto& patch = static_cast<size_t> (v.channel) < project.channels.size() ? project.channels[static_cast<size_t> (v.channel)].synth
                                                                                                  : defaultPatch;
                    l = r = renderSubtractive (v, patch) * v.env * v.velocity * kSubtractiveGain;
                }
                else if (v.fm)
                {
                    const auto& patch = static_cast<size_t> (v.channel) < project.channels.size() ? project.channels[static_cast<size_t> (v.channel)].synth
                                                                                                  : defaultPatch;
                    l = r = renderFm (v, patch) * v.env * v.velocity * kFmGain;
                }
                else if (v.additive)
                {
                    const auto& patch = static_cast<size_t> (v.channel) < project.channels.size() ? project.channels[static_cast<size_t> (v.channel)].synth
                                                                                                  : defaultPatch;
                    l = r = renderAdditive (v, patch) * v.env * v.velocity * kAdditiveGain;
                }
                else if (v.physical)
                {
                    const auto& patch = static_cast<size_t> (v.channel) < project.channels.size() ? project.channels[static_cast<size_t> (v.channel)].synth
                                                                                                  : defaultPatch;
                    l = r = renderPhysical (v, patch) * v.env * kPhysicalGain; // the velocity is in the strike already
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

        // 1b. Plugin instruments render this chunk with the MIDI it produced, then go through their channel strip like the voices did.
        for (size_t ch = 0; ch < numMixerChannels; ++ch)
        {
            const auto& c = project.channels[ch];
            const int count = std::min (pluginEventCount[ch], kMaxPluginEvents);
            pluginEventCount[ch] = 0;
            if (c.instrument != Instrument::Plugin || c.plugin == nullptr)
                continue;
            std::fill_n (pluginL.begin(), n, 0.0f);
            std::fill_n (pluginR.begin(), n, 0.0f);
            c.plugin->setTransport (ticksPerSample * sampleRate * 60.0 / kTicksPerBeat, tickPos / kTicksPerBeat, playing);
            c.plugin->process (pluginL.data(), pluginR.data(), n, pluginEvents[ch].data(), count);
            const float g = smoothedGain[ch];
            for (int i = 0; i < n; ++i)
            {
                const float l = pluginL[static_cast<size_t> (i)] * g * panLeft[ch];
                const float r = pluginR[static_cast<size_t> (i)] * g * panRight[ch];
                peaks[ch] = std::max ({ peaks[ch], std::fabs (l), std::fabs (r) });
                trackL[routedTo[ch]][static_cast<size_t> (i)] += l;
                trackR[routedTo[ch]][static_cast<size_t> (i)] += r;
            }
        }
        chunkSample = 0;

        // 2. Each track: insert effects (pre-fader), then fader, balance, mute/solo, metering, master bus.
        std::array<float, kChunk> mixL {}, mixR {};

        // 2a. Sidechain sources (0.45.0): the tracks a compressor listens to keep a copy of their sound as it is now, before
        // any track's effects run, so the order of the tracks does not matter.
        std::fill_n (keyWanted.begin(), numTracks, static_cast<char> (0));
        bool anyKey = false;
        const auto wantKeys = [&] (const std::array<EffectSlot, kSlotsPerTrack>& slots)
        {
            for (const auto& slot : slots)
                if ((slot.type == EffectType::Compressor || slot.type == EffectType::Gate) && ! slot.bypass) // 0.61.0: the gate too
                    if (const auto k = std::lround (slot.params[5]) - 1; k >= 0 && k < static_cast<long> (numTracks))
                    {
                        keyWanted[static_cast<size_t> (k)] = 1;
                        anyKey = true;
                    }
        };
        for (size_t t = 0; t < numTracks; ++t)
            wantKeys (trackAt (t).slots);
        for (const auto& bus : project.console.aux)
            wantKeys (bus.slots);
        wantKeys (project.masterSlots);
        for (size_t t = 0; t < numTracks; ++t)
        {
            keyPtrL[t] = nullptr;
            keyPtrR[t] = nullptr;
            if (keyWanted[t] == 0)
                continue;
            std::copy_n (trackL[t].begin(), n, keyL[t].begin());
            std::copy_n (trackR[t].begin(), n, keyR[t].begin());
            keyPtrL[t] = keyL[t].data();
            keyPtrR[t] = keyR[t].data();
        }
        auto trackTransport = transport;
        if (anyKey)
        {
            trackTransport.keyLeft = keyPtrL.data();
            trackTransport.keyRight = keyPtrR.data();
            trackTransport.numKeys = static_cast<int> (numTracks);
        }

        for (size_t t = 0; t < numTracks; ++t)
        {
            float* l = trackL[t].data();
            float* r = trackR[t].data();
            const auto& tr = trackAt (t);

            bool hasEffect = false;
            for (const auto& slot : tr.slots)
                hasEffect = hasEffect || (slot.type != EffectType::None && ! slot.bypass);
            if (! hasEffect)
            {
                bool silent = true;
                for (int i = 0; i < n && silent; ++i)
                    silent = ! (std::abs (l[i]) > 0.0f || std::abs (r[i]) > 0.0f);
                if (silent)
                {
                    for (size_t s = 0; s < static_cast<size_t> (kSlotsPerTrack); ++s)
                        effects[t][s].process (tr.slots[s], l, r, n, trackTransport); // only to let a removed effect forget its state
                    trackGain[t] = trackTarget[t];
                    sendGate[t] = trackAudible[t] ? 1.0f : 0.0f;
                    sendGain[t] = sendLevel[t];
                    trackMeanSquare[t] = trackMeanSquare[t] * (n == kChunk ? rmsDecayPerChunk : std::pow (1.0f - rmsCoef, static_cast<float> (n)));
                    if (trackMeanSquare[t] < 1.0e-20f)
                        trackMeanSquare[t] = 0.0f;
                    continue;
                }
            }

            for (size_t s = 0; s < static_cast<size_t> (kSlotsPerTrack); ++s)
                effects[t][s].process (automated ? trackSlotsNow[t][s] : tr.slots[s], l, r, n, trackTransport);

            // 0.61.0: the track's polarity, channel swap and stereo separation (as an FL Studio mixer track's), after its effects
            if (tr.invertPhase || tr.swapStereo || std::abs (tr.separation) > 0.0005f)
            {
                const float polarity = tr.invertPhase ? -1.0f : 1.0f;
                const float sideGain = tr.separation >= 0.0f ? 1.0f - std::min (tr.separation, 1.0f) : 1.0f + std::min (-tr.separation, 1.0f);
                for (int i = 0; i < n; ++i)
                {
                    const float a = (tr.swapStereo ? r[i] : l[i]) * polarity;
                    const float b = (tr.swapStereo ? l[i] : r[i]) * polarity;
                    const float mid = 0.5f * (a + b), side = 0.5f * (a - b) * sideGain;
                    l[i] = mid + side;
                    r[i] = mid - side;
                }
            }

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
                    sendGain[t][a] += (sendLevel[t][a] - sendGain[t][a]) * kGainSmoothing;
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
                auxEffects[a][s].process (bus.slots[s], l, r, n, trackTransport);

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
            masterEffects[s].process (automated ? masterSlotsNow[s] : project.masterSlots[s], mixL.data(), mixR.data(), n, trackTransport);

        // The soft limiter (0.24.0): exactly linear up to 0.7 (-3 dBFS), then a tanh knee that never passes 1 and has the same slope
        // where it starts. Until 0.23.0 it was a plain tanh, which bent (and so distorted, by about -40 dB at -10 dBFS) every level.
        const auto limit = [] (float x) noexcept
        {
            constexpr float t = 0.7f;
            const float a = std::abs (x);
            return a <= t ? x : std::copysign (t + (1.0f - t) * std::tanh ((a - t) / (1.0f - t)), x);
        };
        for (int i = 0; i < n; ++i)
        {
            const float outL = limit (mixL[static_cast<size_t> (i)]);
            const float outR = limit (mixR[static_cast<size_t> (i)]);
            const float clickSample = click[static_cast<size_t> (i)];
            for (int ch = 0; ch < numChannels; ++ch)
                if (out[ch] != nullptr)
                    out[ch][done + i] += ((ch & 1) == 0 ? outL : outR) + clickSample;
        }
    }
}
} // namespace sls
