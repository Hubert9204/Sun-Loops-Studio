#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "orchestra/Instrument.h"

// SunSymphony's engine (Milestone 0): 16 parts, 128 voices, sample playback with 4-point Hermite interpolation. Plain C++, no JUCE:
// the processor turns MIDI into noteOn / noteOff / controller calls and renders the stretches between them.
//
// Threads: setInstrument() and collectRetired() run on the message thread; everything else on the audio thread, which never
// allocates, frees or waits. A new instrument reaches the audio thread through a try-lock at the start of render(); the one it
// replaces is handed back (retired) and freed later by collectRetired(), once its voices have faded.
namespace orchestra
{
constexpr int kParts = 16;
constexpr int kMaxVoices = 128;  // voices that may sound at once
constexpr int kSpareVoices = 16; // more slots, for voices that are being cut short (5 ms) when the 128 are in use
constexpr int kZonesPerNote = 4; // zones one note may start at once (layered zones)

class Engine
{
public:
    Engine();

    void prepare (double sampleRate) noexcept;
    double sampleRate() const noexcept { return rate; }

    // Message thread.
    void setInstrument (int part, std::shared_ptr<const Instrument> instrument);
    std::shared_ptr<const Instrument> instrument (int part) const; // the one asked for last (may not be playing yet)
    void collectRetired();       // frees replaced instruments whose voices are gone (call now and then)

    // Audio thread. `channel` is the MIDI channel, 0-15: part n listens to channel n.
    void noteOn (int channel, int key, int velocity) noexcept;
    void noteOff (int channel, int key) noexcept;
    void controller (int channel, int number, int value) noexcept;
    void allNotesOff() noexcept;
    // Adds (does not replace) the voices' sound to left/right.
    void render (float* left, float* right, int numSamples) noexcept;

    int activeVoices() const noexcept { return sounding.load (std::memory_order_relaxed); }
    int voicesStolen() const noexcept { return stolen.load (std::memory_order_relaxed); }

private:
    struct Voice
    {
        enum class State : std::uint8_t { Idle, Playing, Releasing, Killing };
        State state = State::Idle;
        const Region* region = nullptr;
        const SampleBuffer* buffer = nullptr;
        double pos = 0.0, increment = 1.0;
        float env = 0.0f, attackStep = 1.0f, releaseCoef = 0.999f, killStep = 0.01f;
        float gainL = 0.0f, gainR = 0.0f;
        int part = 0, key = 0;
        bool held = false;       // the key is down
        bool pedalHeld = false;  // released while the sustain pedal was down
        std::uint64_t order = 0; // when it started (for stealing)

        void release() noexcept { if (state == State::Playing) state = State::Releasing; }
        void kill (float sampleRate) noexcept
        {
            if (state == State::Idle)
                return;
            state = State::Killing;
            killStep = std::max (env, 1.0e-4f) / std::max (1.0f, 0.005f * sampleRate); // 5 ms to silence
        }
        bool render (float* left, float* right, int numSamples) noexcept; // false when it has finished
    };

    struct Part
    {
        std::shared_ptr<const Instrument> active;   // audio thread
        float volume = 1.0f, pan = 0.0f, expression = 1.0f;
        bool pedal = false;
    };

    Voice* startVoice() noexcept;
    void swapInstruments() noexcept;
    bool tryLock() noexcept { return ! lock.exchange (true, std::memory_order_acquire); }
    void unlock() noexcept { lock.store (false, std::memory_order_release); }

    double rate = 48000.0;
    std::array<Voice, kMaxVoices + kSpareVoices> voices {};
    std::array<Part, kParts> parts {};
    std::uint64_t counter = 0;
    std::atomic<int> sounding { 0 }, stolen { 0 };
    std::atomic<std::uint64_t> samplesDone { 0 };

    // Shared between the threads, under `lock`.
    std::atomic<bool> lock { false };
    std::array<std::shared_ptr<const Instrument>, kParts> requested {}; // what the message thread asked for
    std::array<bool, kParts> pending {};
    std::atomic<bool> anyPending { false };  // a quick look before taking the lock
    std::array<std::shared_ptr<const Instrument>, kParts> retired {};   // replaced, waiting to be freed
    std::array<std::uint64_t, kParts> retiredAt {};
};
} // namespace orchestra
