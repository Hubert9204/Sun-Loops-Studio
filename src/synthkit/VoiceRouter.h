#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>

// The MIDI handling and voice allocation that SunBurst and HexForge share (0.51.0). A Voice has `note`, `velocity`, `keyDown`,
// `age`, isActive(), isReleasing(), level(), release(), kill() and start (note, velocity, fromNote, legato, seed, patch); Controls
// has `bend`, `aftertouch` and `modWheel`.
namespace synthkit
{
template <class Voice, size_t N>
class VoiceRouter
{
public:
    explicit VoiceRouter (std::array<Voice, N>& v) noexcept : voices (v) {}

    // Modes: 0 poly, 1 mono, 2 legato.
    template <class Controls, class Patch>
    void handleMidi (const juce::MidiMessage& m, Controls& controls, int mode, int polyphony, const Patch& patch) noexcept
    {
        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getFloatVelocity(), mode, polyphony, patch);
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber(), mode, patch);
        else if (m.isPitchWheel())
            controls.bend = static_cast<float> (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isChannelPressure())
            controls.aftertouch = static_cast<float> (m.getChannelPressureValue()) / 127.0f;
        else if (m.isAftertouch())
            controls.aftertouch = static_cast<float> (m.getAfterTouchValue()) / 127.0f;
        else if (m.isController())
        {
            const int cc = m.getControllerNumber(), value = m.getControllerValue();
            if (cc == 1)
                controls.modWheel = static_cast<float> (value) / 127.0f;
            else if (cc == 64)
                setSustain (value >= 64);
            else if (cc == 120 || cc == 123)
                allNotesOff (cc == 120);
        }
    }

    template <class Patch>
    void noteOn (int note, float velocity, int mode, int polyphony, const Patch& patch) noexcept
    {
        const std::uint32_t seed = seedCounter++ * 747796405u + static_cast<std::uint32_t> (note);
        if (mode == 0)
        {
            if (auto* v = findVoiceFor (note, polyphony))
            {
                v->start (note, velocity, lastNote, false, seed, patch);
                v->age = ++noteCounter;
            }
        }
        else
        {
            // Mono and legato: one voice and a stack of held keys (the last one sounds).
            removeFromStack (note);
            if (monoCount < static_cast<int> (monoStack.size()))
                monoStack[static_cast<size_t> (monoCount++)] = note;
            auto& v = voices[0];
            const bool legato = mode == 2 && v.isActive() && monoCount > 1;
            v.start (note, velocity, v.isActive() ? static_cast<double> (v.note >= 0 ? v.note : note) : lastNote, legato, seed, patch);
            v.age = ++noteCounter;
        }
        lastNote = note;
    }

    template <class Patch>
    void noteOff (int note, int mode, const Patch& patch) noexcept
    {
        if (mode == 0)
        {
            for (auto& v : voices)
                if (v.isActive() && v.note == note && v.keyDown)
                {
                    v.keyDown = false;
                    if (! sustain)
                        v.release();
                }
            return;
        }
        removeFromStack (note);
        auto& v = voices[0];
        if (v.note != note)
            return;
        if (monoCount > 0)
        {
            // Back to the key still held: in legato without a new attack, in mono with one.
            const int previous = monoStack[static_cast<size_t> (monoCount - 1)];
            v.start (previous, v.velocity, note, mode == 2, seedCounter++, patch);
            lastNote = previous;
        }
        else
        {
            v.keyDown = false;
            if (! sustain)
                v.release();
        }
    }

    void setSustain (bool down) noexcept
    {
        sustain = down;
        if (! sustain)
            for (auto& v : voices)
                if (v.isActive() && ! v.keyDown && ! v.isReleasing())
                    v.release();
    }

    void allNotesOff (bool hard) noexcept
    {
        for (auto& v : voices)
        {
            if (hard)
                v.kill();
            else if (v.isActive())
                v.release();
        }
        monoCount = 0;
    }

    void reset() noexcept
    {
        monoCount = 0;
        sustain = false;
    }

private:
    // The same note again: reuse its voice. Otherwise a silent voice, then the quietest released one, then the oldest.
    Voice* findVoiceFor (int note, int polyphony) noexcept
    {
        polyphony = std::clamp (polyphony, 1, static_cast<int> (N));
        Voice* best = nullptr;
        for (int i = 0; i < polyphony; ++i)
            if (auto& v = voices[static_cast<size_t> (i)]; v.isActive() && v.note == note)
                return &v;
        for (int i = 0; i < polyphony; ++i)
            if (! voices[static_cast<size_t> (i)].isActive())
                return &voices[static_cast<size_t> (i)];
        for (int i = 0; i < polyphony; ++i)
            if (auto& v = voices[static_cast<size_t> (i)]; v.isReleasing() && (best == nullptr || v.level() < best->level()))
                best = &v;
        if (best != nullptr)
            return best;
        for (int i = 0; i < polyphony; ++i)
            if (auto& v = voices[static_cast<size_t> (i)]; best == nullptr || v.age < best->age)
                best = &v;
        return best;
    }

    void removeFromStack (int note) noexcept
    {
        for (int i = 0; i < monoCount; ++i)
            if (monoStack[static_cast<size_t> (i)] == note)
            {
                for (int j = i; j < monoCount - 1; ++j)
                    monoStack[static_cast<size_t> (j)] = monoStack[static_cast<size_t> (j) + 1];
                --monoCount;
                return;
            }
    }

    std::array<Voice, N>& voices;
    std::array<int, 128> monoStack {};
    int monoCount = 0;
    bool sustain = false;
    double lastNote = -1.0;
    std::uint64_t noteCounter = 0;
    std::uint32_t seedCounter = 1;
};
} // namespace synthkit
