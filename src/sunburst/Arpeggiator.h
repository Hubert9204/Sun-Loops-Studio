#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

#include "sunburst/SynthDsp.h"

namespace sunburst
{
// Turns held keys into a pattern of notes in time with the tempo. Runs on the incoming MIDI of a block and writes the notes it
// plays (and every other message) into `out`. No allocation once `out` has room (MidiBuffer::ensureSize in prepare).
class Arpeggiator
{
public:
    struct Settings
    {
        ArpMode mode = ArpMode::Up;
        double stepBeats = 0.25;
        int octaves = 1;
        float gate = 0.6f;
    };

    void reset() noexcept
    {
        heldCount = 0;
        sounding = 0;
        step = 0;
        samplesToStep = 0.0;
        noteOffIn = -1.0;
        random.seed (0x51ed2701u);
    }

    void process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples, double sampleRate, double bpm, const Settings& s) noexcept
    {
        out.clear();
        const double stepSamples = std::max (1.0, s.stepBeats * 60.0 / std::max (20.0, bpm) * sampleRate);
        auto it = in.begin();
        for (int pos = 0; pos < numSamples; ++pos)
        {
            for (; it != in.end() && (*it).samplePosition <= pos; ++it)
            {
                const auto m = (*it).getMessage();
                if (m.isNoteOn())
                {
                    const bool wasEmpty = heldCount == 0;
                    add (m.getNoteNumber(), m.getFloatVelocity());
                    if (wasEmpty)
                    {
                        step = 0;
                        samplesToStep = 0.0; // the first note at once
                    }
                }
                else if (m.isNoteOff())
                    remove (m.getNoteNumber());
                else if (m.isAllNotesOff() || m.isAllSoundOff())
                {
                    heldCount = 0;
                    stopSounding (out, pos);
                    out.addEvent (m, pos);
                }
                else
                    out.addEvent (m, pos);
            }

            if (noteOffIn >= 0.0)
            {
                noteOffIn -= 1.0;
                if (noteOffIn < 0.0)
                    stopSounding (out, pos);
            }
            if (heldCount == 0)
            {
                if (sounding > 0 && noteOffIn < 0.0)
                    stopSounding (out, pos);
                continue;
            }
            if (samplesToStep <= 0.0)
            {
                samplesToStep += stepSamples;
                stopSounding (out, pos);
                playStep (out, pos, s);
                noteOffIn = std::max (1.0, stepSamples * std::clamp (s.gate, 0.05f, 1.0f) - 1.0);
            }
            samplesToStep -= 1.0;
        }
    }

private:
    void add (int note, float velocity) noexcept
    {
        for (int i = 0; i < heldCount; ++i)
            if (held[static_cast<size_t> (i)] == note)
                return;
        if (heldCount < static_cast<int> (held.size()))
        {
            held[static_cast<size_t> (heldCount)] = note;
            velocities[static_cast<size_t> (heldCount)] = velocity;
            ++heldCount;
        }
    }
    void remove (int note) noexcept
    {
        for (int i = 0; i < heldCount; ++i)
            if (held[static_cast<size_t> (i)] == note)
            {
                for (int j = i; j < heldCount - 1; ++j)
                {
                    held[static_cast<size_t> (j)] = held[static_cast<size_t> (j + 1)];
                    velocities[static_cast<size_t> (j)] = velocities[static_cast<size_t> (j + 1)];
                }
                --heldCount;
                return;
            }
    }
    void stopSounding (juce::MidiBuffer& out, int pos) noexcept
    {
        for (int i = 0; i < sounding; ++i)
            out.addEvent (juce::MidiMessage::noteOff (1, playing[static_cast<size_t> (i)]), pos);
        sounding = 0;
        noteOffIn = -1.0;
    }
    void playStep (juce::MidiBuffer& out, int pos, const Settings& s) noexcept
    {
        // The notes in pitch order (or in the order they were played), over the octaves.
        std::array<int, 32> order {};
        std::array<float, 32> vel {};
        for (int i = 0; i < heldCount; ++i)
        {
            order[static_cast<size_t> (i)] = held[static_cast<size_t> (i)];
            vel[static_cast<size_t> (i)] = velocities[static_cast<size_t> (i)];
        }
        if (s.mode != ArpMode::AsPlayed)
            for (int i = 1; i < heldCount; ++i) // insertion sort, both arrays
                for (int j = i; j > 0 && order[static_cast<size_t> (j - 1)] > order[static_cast<size_t> (j)]; --j)
                {
                    std::swap (order[static_cast<size_t> (j - 1)], order[static_cast<size_t> (j)]);
                    std::swap (vel[static_cast<size_t> (j - 1)], vel[static_cast<size_t> (j)]);
                }
        const int octaves = std::clamp (s.octaves, 1, 4);
        const int length = heldCount * octaves;

        if (s.mode == ArpMode::Chord)
        {
            const int octave = step % octaves;
            for (int i = 0; i < heldCount && sounding < static_cast<int> (playing.size()); ++i)
                sound (out, pos, order[static_cast<size_t> (i)] + 12 * octave, vel[static_cast<size_t> (i)]);
            ++step;
            return;
        }

        int index = 0;
        switch (s.mode)
        {
            case ArpMode::Up:
            case ArpMode::AsPlayed: index = step % length; break;
            case ArpMode::Down: index = length - 1 - step % length; break;
            case ArpMode::UpDown:
            {
                const int cycle = std::max (1, 2 * length - 2);
                const int p = step % cycle;
                index = length > 1 ? (p < length ? p : cycle - p) : 0;
                break;
            }
            case ArpMode::Random: index = static_cast<int> (random.next() % static_cast<std::uint32_t> (length)); break;
            case ArpMode::Chord: break;
        }
        const int octave = index / heldCount, which = index % heldCount;
        sound (out, pos, order[static_cast<size_t> (which)] + 12 * octave, vel[static_cast<size_t> (which)]);
        ++step;
    }
    void sound (juce::MidiBuffer& out, int pos, int note, float velocity) noexcept
    {
        if (note > 127)
            return;
        out.addEvent (juce::MidiMessage::noteOn (1, note, std::max (0.01f, velocity)), pos);
        playing[static_cast<size_t> (sounding++)] = note;
    }

    std::array<int, 32> held {};
    std::array<float, 32> velocities {};
    int heldCount = 0;
    std::array<int, 32> playing {};
    int sounding = 0;
    int step = 0;
    double samplesToStep = 0.0, noteOffIn = -1.0;
    Random random;
};
} // namespace sunburst
