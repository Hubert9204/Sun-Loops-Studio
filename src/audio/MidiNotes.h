#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>

#include "audio/LiveNotes.h"

namespace sls
{
// Turns what a MIDI keyboard sends into live notes for one channel of the song. Only notes are used (note on, note
// off, "all notes off" and "all sound off"); everything else (pitch bend, controllers, program changes...) is
// ignored. The channel that gets a note on also gets the matching note off, even if the target channel was changed
// while the key was held, so a note can never be left hanging. Not thread safe: the caller serialises calls.
class MidiNoteMapper
{
public:
    // Calls emit (LiveNoteEvent) for every event the message produces.
    template <typename Emit>
    void map (const juce::MidiMessage& message, int targetChannel, Emit&& emit)
    {
        if (message.isNoteOn (false)) // a note on with velocity 0 is a note off (isNoteOn (false) is false for it)
        {
            const int pitch = message.getNoteNumber();
            if (pitch < 0 || pitch > 127)
                return;

            const auto index = static_cast<size_t> (pitch);
            if (held[index] >= 0) // the key is already down (a repeated note on): end the old note first
                emit (LiveNoteEvent { false, static_cast<std::uint8_t> (held[index]), static_cast<std::uint8_t> (pitch), 0.0f });

            const int channel = std::clamp (targetChannel, 0, 255);
            held[index] = static_cast<std::int16_t> (channel);
            emit (LiveNoteEvent { true, static_cast<std::uint8_t> (channel), static_cast<std::uint8_t> (pitch),
                                  std::clamp (static_cast<float> (message.getVelocity()) / 127.0f, 0.0f, 1.0f) });
        }
        else if (message.isNoteOff (true))
        {
            release (message.getNoteNumber(), emit);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            releaseAll (emit);
        }
    }

    // Ends every note that is held (a keyboard was unplugged, or the inputs were switched off).
    template <typename Emit>
    void releaseAll (Emit&& emit)
    {
        for (int pitch = 0; pitch < 128; ++pitch)
            release (pitch, emit);
    }

    int heldCount() const noexcept
    {
        int n = 0;
        for (auto h : held)
            n += h >= 0 ? 1 : 0;
        return n;
    }

private:
    template <typename Emit>
    void release (int pitch, Emit&& emit)
    {
        if (pitch < 0 || pitch > 127 || held[static_cast<size_t> (pitch)] < 0)
            return;
        emit (LiveNoteEvent { false, static_cast<std::uint8_t> (held[static_cast<size_t> (pitch)]), static_cast<std::uint8_t> (pitch), 0.0f });
        held[static_cast<size_t> (pitch)] = -1;
    }

    std::array<std::int16_t, 128> held = [] { std::array<std::int16_t, 128> a {}; a.fill (-1); return a; }();
};
} // namespace sls
