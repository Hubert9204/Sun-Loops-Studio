#include "orchestra/Engine.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace orchestra
{
namespace
{
// 4-point, 3rd-order Hermite (Catmull-Rom): good for transpositions of a few semitones, cheap. (Milestone 5 adds a windowed sinc.)
inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b = w + a;
    return ((((a * t) - b) * t + c) * t + x0);
}

// A sample of `data` at index i: inside a forward loop the indices past its end wrap to its start; outside the sample, silence.
inline float at (const float* data, int frames, int i, int loopStart, int loopEnd, bool looping) noexcept
{
    if (looping && i >= loopEnd)
        i = loopStart + (i - loopStart) % (loopEnd - loopStart);
    return i >= 0 && i < frames ? data[i] : 0.0f;
}

// Velocity to level: the velocity layers carry most of the dynamics, so the curve is gentle (about -10 dB at velocity 1).
inline float velocityGain (int velocity) noexcept
{
    const float v = static_cast<float> (std::clamp (velocity, 1, 127)) / 127.0f;
    return 0.3f + 0.7f * v * v;
}
} // namespace

Engine::Engine() = default;

void Engine::prepare (double sampleRate) noexcept
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    allNotesOff();
    for (auto& v : voices)
        v.state = Voice::State::Idle;
    sounding.store (0, std::memory_order_relaxed);
}

void Engine::setInstrument (int part, std::shared_ptr<const Instrument> inst)
{
    if (part < 0 || part >= kParts)
        return;
    while (! tryLock())
        std::this_thread::yield();
    requested[static_cast<size_t> (part)] = std::move (inst);
    pending[static_cast<size_t> (part)] = true;
    anyPending.store (true, std::memory_order_release);
    unlock();
}

std::shared_ptr<const Instrument> Engine::instrument (int part) const
{
    if (part < 0 || part >= kParts)
        return nullptr;
    auto* self = const_cast<Engine*> (this);
    while (! self->tryLock())
        std::this_thread::yield();
    auto result = requested[static_cast<size_t> (part)];
    self->unlock();
    return result;
}

void Engine::collectRetired()
{
    std::array<std::shared_ptr<const Instrument>, kParts> toFree {};
    while (! tryLock())
        std::this_thread::yield();
    const auto now = samplesDone.load (std::memory_order_relaxed);
    const auto wait = static_cast<std::uint64_t> (0.1 * rate); // killed voices fade in 5 ms; 100 ms is plenty
    for (size_t p = 0; p < retired.size(); ++p)
        if (retired[p] != nullptr && now - retiredAt[p] > wait)
            toFree[p] = std::move (retired[p]);
    unlock();
    // (freed here, outside the lock, on this thread)
}

void Engine::swapInstruments() noexcept
{
    if (! anyPending.load (std::memory_order_acquire) || ! tryLock())
        return; // nothing new, or the message thread is busy with it: next time
    bool waiting = false;
    for (size_t p = 0; p < parts.size(); ++p)
    {
        if (! pending[p])
            continue;
        if (retired[p] != nullptr)
        {
            waiting = true; // a previous instrument still waits to be freed: swap once it is gone
            continue;
        }
        for (auto& v : voices) // the old instrument's voices stop (they point into it)
            if (v.state != Voice::State::Idle && v.part == static_cast<int> (p))
                v.kill (static_cast<float> (rate));
        retired[p] = std::move (parts[p].active); // never freed on this thread
        retiredAt[p] = samplesDone.load (std::memory_order_relaxed);
        parts[p].active = requested[p];           // a copy: the message thread keeps its own reference
        pending[p] = false;
    }
    anyPending.store (waiting, std::memory_order_relaxed);
    unlock();
}

Engine::Voice* Engine::startVoice() noexcept
{
    int active = 0;
    for (const auto& v : voices)
        active += v.state == Voice::State::Playing || v.state == Voice::State::Releasing ? 1 : 0;
    if (active >= kMaxVoices)
    {
        // Steal: the oldest released voice, else the oldest playing one. It fades in 5 ms in its own slot.
        Voice* victim = nullptr;
        for (auto& v : voices)
            if (v.state == Voice::State::Releasing && (victim == nullptr || v.order < victim->order))
                victim = &v;
        if (victim == nullptr)
            for (auto& v : voices)
                if (v.state == Voice::State::Playing && (victim == nullptr || v.order < victim->order))
                    victim = &v;
        if (victim != nullptr)
        {
            victim->kill (static_cast<float> (rate));
            stolen.fetch_add (1, std::memory_order_relaxed);
        }
    }
    for (auto& v : voices)
        if (v.state == Voice::State::Idle)
            return &v;
    // Every slot is busy (many voices being cut at once): reuse the oldest of those fading out.
    Voice* oldest = nullptr;
    for (auto& v : voices)
        if (v.state == Voice::State::Killing && (oldest == nullptr || v.order < oldest->order))
            oldest = &v;
    return oldest;
}

void Engine::noteOn (int channel, int key, int velocity) noexcept
{
    if (channel < 0 || channel >= kParts || key < 0 || key > 127)
        return;
    if (velocity <= 0)
    {
        noteOff (channel, key);
        return;
    }
    swapInstruments(); // (an instrument set just before this note plays it)
    auto& part = parts[static_cast<size_t> (channel)];
    if (part.active == nullptr)
        return;
    // The same key again on this part: the old note is released (repeated notes overlap only by their release).
    for (auto& v : voices)
        if (v.part == channel && v.key == key && v.state == Voice::State::Playing)
        {
            v.held = false;
            v.pedalHeld = false;
            v.release();
        }
    const Region* zones[kZonesPerNote];
    const int n = part.active->pick (key, velocity, zones, kZonesPerNote);
    const float level = velocityGain (velocity);
    for (int z = 0; z < n; ++z)
    {
        Voice* v = startVoice();
        if (v == nullptr)
            return;
        const Region& r = *zones[z];
        v->region = &r;
        v->buffer = r.sample.get();
        v->pos = 0.0;
        v->increment = std::pow (2.0, (key - r.rootKey + r.tuneCents / 100.0) / 12.0) * (r.sample->sampleRate / rate);
        v->env = 0.0f;
        v->attackStep = 1.0f / std::max (1.0f, part.active->attackSeconds * static_cast<float> (rate));
        // Release: an exponential fall to -80 dB in the instrument's release time.
        v->releaseCoef = static_cast<float> (std::exp (std::log (1.0e-4) / std::max (1.0, part.active->releaseSeconds * rate)));
        const float gain = level * std::pow (10.0f, r.gainDb / 20.0f);
        const float pan = std::clamp (r.pan, -1.0f, 1.0f);
        const float angle = (pan + 1.0f) * 0.25f * 3.14159265f; // constant power
        v->gainL = gain * std::cos (angle) * 1.41421356f;
        v->gainR = gain * std::sin (angle) * 1.41421356f;
        v->part = channel;
        v->key = key;
        v->held = true;
        v->pedalHeld = false;
        v->order = ++counter;
        v->state = Voice::State::Playing;
    }
}

void Engine::noteOff (int channel, int key) noexcept
{
    if (channel < 0 || channel >= kParts)
        return;
    const bool pedal = parts[static_cast<size_t> (channel)].pedal;
    for (auto& v : voices)
        if (v.part == channel && v.key == key && v.held && v.state == Voice::State::Playing)
        {
            v.held = false;
            if (pedal)
                v.pedalHeld = true;
            else
                v.release();
        }
}

void Engine::controller (int channel, int number, int value) noexcept
{
    if (channel < 0 || channel >= kParts)
        return;
    auto& part = parts[static_cast<size_t> (channel)];
    const float x = static_cast<float> (std::clamp (value, 0, 127)) / 127.0f;
    switch (number)
    {
        case 7:  part.volume = x * x * 1.5f; break;           // channel volume (100 is about unity)
        case 10: part.pan = x * 2.0f - 1.0f; break;
        case 11: part.expression = x; break;                   // (Milestone 2: also the dynamic crossfade)
        case 64:                                               // sustain pedal
            part.pedal = value >= 64;
            if (! part.pedal)
                for (auto& v : voices)
                    if (v.part == channel && v.pedalHeld)
                    {
                        v.pedalHeld = false;
                        v.release();
                    }
            break;
        case 120: // all sound off
            for (auto& v : voices)
                if (v.part == channel)
                    v.kill (static_cast<float> (rate));
            break;
        case 123: // all notes off
            for (auto& v : voices)
                if (v.part == channel && v.state == Voice::State::Playing)
                {
                    v.held = v.pedalHeld = false;
                    v.release();
                }
            break;
        default: break;
    }
}

void Engine::allNotesOff() noexcept
{
    for (auto& p : parts)
        p.pedal = false;
    for (auto& v : voices)
        v.kill (static_cast<float> (rate));
}

bool Engine::Voice::render (float* left, float* right, int numSamples) noexcept
{
    const auto& r = *region;
    const float* l = buffer->left.data();
    const float* rr = buffer->stereo() ? buffer->right.data() : l;
    const int frames = buffer->frames();
    const bool looping = r.loops();
    const int loopLen = r.loopEnd - r.loopStart;
    for (int i = 0; i < numSamples; ++i)
    {
        // envelope
        switch (state)
        {
            case State::Playing:   env = std::min (1.0f, env + attackStep); break;
            case State::Releasing: env *= releaseCoef; break;
            case State::Killing:   env -= killStep; break;
            case State::Idle:      return false;
        }
        if (env <= 1.0e-5f && state != State::Playing)
        {
            state = State::Idle;
            return false;
        }
        if (looping && pos >= r.loopEnd)
            pos -= loopLen * std::floor ((pos - r.loopStart) / loopLen);
        const int idx = static_cast<int> (pos);
        if (! looping && idx >= frames)
        {
            state = State::Idle;
            return false;
        }
        const float t = static_cast<float> (pos - idx);
        const float sl = hermite (at (l, frames, idx - 1, r.loopStart, r.loopEnd, looping), at (l, frames, idx, r.loopStart, r.loopEnd, looping),
                                  at (l, frames, idx + 1, r.loopStart, r.loopEnd, looping), at (l, frames, idx + 2, r.loopStart, r.loopEnd, looping), t);
        const float sr = rr == l ? sl
                                 : hermite (at (rr, frames, idx - 1, r.loopStart, r.loopEnd, looping), at (rr, frames, idx, r.loopStart, r.loopEnd, looping),
                                            at (rr, frames, idx + 1, r.loopStart, r.loopEnd, looping), at (rr, frames, idx + 2, r.loopStart, r.loopEnd, looping), t);
        left[i] += sl * env * gainL;
        right[i] += sr * env * gainR;
        pos += increment;
    }
    return true;
}

void Engine::render (float* left, float* right, int numSamples) noexcept
{
    swapInstruments();
    // Each part's level applies to its voices as they are mixed: render a voice into a small scratch block, then add it scaled.
    constexpr int kChunk = 64;
    float bufL[kChunk], bufR[kChunk];
    int count = 0;
    for (int start = 0; start < numSamples; start += kChunk)
    {
        const int n = std::min (kChunk, numSamples - start);
        for (auto& v : voices)
        {
            if (v.state == Voice::State::Idle)
                continue;
            std::fill (bufL, bufL + n, 0.0f);
            std::fill (bufR, bufR + n, 0.0f);
            v.render (bufL, bufR, n);
            const auto& part = parts[static_cast<size_t> (v.part)];
            // The part's pan is a balance (centre: both sides at full level), so a centred part leaves its zones' panning as it is.
            const float g = part.volume * part.expression;
            const float gl = g * std::min (1.0f, 1.0f - part.pan), gr = g * std::min (1.0f, 1.0f + part.pan);
            for (int i = 0; i < n; ++i)
            {
                left[start + i] += bufL[i] * gl;
                right[start + i] += bufR[i] * gr;
            }
        }
    }
    for (const auto& v : voices)
        count += v.state == Voice::State::Playing || v.state == Voice::State::Releasing ? 1 : 0;
    sounding.store (count, std::memory_order_relaxed);
    samplesDone.fetch_add (static_cast<std::uint64_t> (numSamples), std::memory_order_relaxed);
}
} // namespace orchestra
