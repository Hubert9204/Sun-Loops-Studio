#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "audio/LiveNotes.h"
#include "audio/NoteSequence.h"

namespace sls
{
// Recording what a MIDI keyboard plays into the notes of a pattern.
//
// The audio thread stamps every note that comes from a MIDI keyboard with the position of the song (makeTake) and
// hands it to the message thread; the NoteRecorder there pairs note on and note off into Notes, puts them on the
// pattern's grid and wraps them into one pattern length. Free of JUCE and of the GUI, so it can be tested headless.

// One note on or off from a keyboard, with where in the song's loop it happened.
struct MidiTake
{
    bool on = false;
    std::uint8_t channel = 0;
    std::uint8_t pitch = 60;
    float velocity = 0.8f;
    float tick = 0.0f;      // position in the loop that was playing, 0 <= tick < loopTicks, already corrected for latency
    float loopTicks = 1.0f; // length of that loop (the pattern in pattern mode, the whole song in song mode)
};

// The position to stamp on a keyboard event that the audio thread handles at the start of a block.
//   blockStartTick : where the song is at the start of this block
//   latencyTicks   : how far the sound that the player is hearing lags behind the song (output latency and half a
//                    block, the time the event waited in the queue): the player pressed the key when the song they
//                    heard was that far back
inline MidiTake makeTake (const LiveNoteEvent& e, double blockStartTick, double latencyTicks, double loopTicks) noexcept
{
    MidiTake t;
    t.on = e.on;
    t.channel = e.channel;
    t.pitch = e.pitch;
    t.velocity = e.velocity;
    t.loopTicks = static_cast<float> (std::max (1.0, loopTicks));

    double pos = blockStartTick - std::max (0.0, latencyTicks);
    if (loopTicks >= 1.0)
        pos = std::fmod (std::fmod (pos, loopTicks) + loopTicks, loopTicks);
    t.tick = static_cast<float> (std::max (0.0, pos));
    return t;
}

class NoteRecorder
{
public:
    // `patternTicks`: the length of a pattern. `quantizeTicks`: the grid note starts are rounded to (0: not at all).
    void configure (int lengthOfPatternTicks, int quantizeTicks) noexcept
    {
        patternTicks = std::max (1, lengthOfPatternTicks);
        quantize = std::max (0, quantizeTicks);
    }

    // Feeds one event (in the order they happened). Calls emit (channel, Note) for every note that is complete: a
    // note off ends a note; a note that was held across the end of the loop becomes two notes (the end of the pattern,
    // then the start of it again), and a note off with no note on before it (the key went down before recording
    // started) is ignored.
    template <typename Emit>
    void process (const MidiTake& e, Emit&& emit)
    {
        const auto key = index (e.channel, e.pitch);

        if (e.on)
        {
            if (held[key].active) // a repeated note on: end the old note here
                finish (e.channel, e.pitch, e.tick, e.loopTicks, emit);
            held[key] = { true, e.tick, e.velocity };
            return;
        }

        if (held[key].active)
            finish (e.channel, e.pitch, e.tick, e.loopTicks, emit);
    }

    // Ends every note that is still held, `lengthTicks` long (recording was switched off or the song stopped with a
    // key down).
    template <typename Emit>
    void flush (int lengthTicks, Emit&& emit)
    {
        for (int channel = 0; channel < kChannels; ++channel)
            for (int pitch = 0; pitch < 128; ++pitch)
            {
                auto& h = held[index (static_cast<std::uint8_t> (channel), static_cast<std::uint8_t> (pitch))];
                if (! h.active)
                    continue;
                h.active = false;
                put (channel, pitch, h.tick, std::max (1, lengthTicks), h.velocity, emit);
            }
    }

    int heldCount() const noexcept
    {
        int n = 0;
        for (const auto& h : held)
            n += h.active ? 1 : 0;
        return n;
    }

    // The note start `tick` placed on the grid and inside the pattern (rounding up to the end of the pattern wraps to 0).
    int placeStart (double tick) const noexcept
    {
        double t = std::fmod (std::max (0.0, tick), static_cast<double> (patternTicks));
        if (quantize > 0)
            t = std::round (t / quantize) * quantize;
        const int start = static_cast<int> (std::lround (t));
        return start >= patternTicks ? 0 : start;
    }

    // Shortest note that is recorded: a key tapped quickly still makes a note you can see and hear.
    int minimumLength() const noexcept { return std::max (quantize > 0 ? quantize : 0, kMinimumLengthTicks); }

    static constexpr int kChannels = 8;
    static constexpr int kMinimumLengthTicks = 60; // an eighth of a beat

private:
    struct Held
    {
        bool active = false;
        float tick = 0.0f;
        float velocity = 0.8f;
    };

    static size_t index (std::uint8_t channel, std::uint8_t pitch) noexcept
    {
        return static_cast<size_t> (std::min<int> (channel, kChannels - 1)) * 128 + std::min<size_t> (pitch, 127);
    }

    template <typename Emit>
    void finish (std::uint8_t channel, std::uint8_t pitch, float offTick, float loopTicks, Emit&& emit)
    {
        auto& h = held[index (channel, pitch)];
        double duration = static_cast<double> (offTick) - h.tick;
        if (duration < 0.0)
            duration += loopTicks; // the loop came round while the key was down
        h.active = false;
        put (channel, pitch, h.tick, std::max (1, static_cast<int> (std::lround (duration))), h.velocity, emit);
    }

    // Puts a note of `duration` ticks starting at loop position `tick` into the pattern.
    template <typename Emit>
    void put (int channel, int pitch, double tick, int duration, float velocity, Emit&& emit)
    {
        duration = std::max (duration, minimumLength());
        int start = placeStart (tick);
        int remaining = std::min (duration, patternTicks); // never longer than the pattern

        for (int part = 0; part < 2 && remaining > 0; ++part)
        {
            const int length = std::min (remaining, patternTicks - start);
            Note n;
            n.pitch = pitch;
            n.startTick = start;
            n.lengthTicks = length;
            n.velocity = std::clamp (velocity, 0.0f, 1.0f);
            emit (channel, n);
            remaining -= length;
            start = 0; // what is left continues at the start of the pattern
        }
    }

    int patternTicks = 8 * kTicksPerBeat;
    int quantize = 0;
    std::array<Held, kChannels * 128> held {};
};

// Adds `incoming` to `notes`, except a note that is already there (same pitch and start): playing over the same
// loop twice does not stack identical notes. Returns true if something was added.
inline bool addRecordedNote (std::vector<Note>& notes, const Note& incoming)
{
    for (const auto& n : notes)
        if (n.pitch == incoming.pitch && n.startTick == incoming.startTick)
            return false;
    notes.push_back (incoming);
    return true;
}
} // namespace sls
