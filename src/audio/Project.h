#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/NoteSequence.h"

namespace sls
{
constexpr int kMaxChannels = 8;

// Decoded audio kept in memory. Immutable once shared.
struct SampleData
{
    std::vector<float> left, right;
    double sampleRate = 44100.0;
    std::string name;

    int numFrames() const noexcept { return static_cast<int> (left.size()); }
};

enum class Instrument { Synth, Sampler };

struct Channel
{
    std::string name = "Channel";
    Instrument instrument = Instrument::Synth;
    std::vector<Note> notes;
    std::shared_ptr<const SampleData> sample; // Sampler only. Plays one-shot.
    int rootPitch = 60;                       // Sampler: pitch at which the sample plays unchanged
    float volume = 0.8f;                      // 0..1.5
    float pan = 0.0f;                         // -1 (left) .. +1 (right)
    bool mute = false;
    bool solo = false;
};

// Immutable snapshot of everything the audio thread needs.
// Build it with Project::make() on the message thread and hand it to a SnapshotExchange.
struct Project
{
    std::vector<Channel> channels;
    int lengthTicks = 8 * kTicksPerBeat;
    std::uint64_t revision = 0; // assigned by SnapshotExchange

    static Project make (std::vector<Channel> input, int loopLengthTicks)
    {
        Project p;
        p.lengthTicks = std::max (1, loopLengthTicks);

        if (input.size() > static_cast<size_t> (kMaxChannels))
            input.resize (kMaxChannels);

        for (auto& c : input)
        {
            c.notes = Sequence::make (std::move (c.notes), p.lengthTicks).notes; // validate + sort
            c.volume = std::clamp (c.volume, 0.0f, 1.5f);
            c.pan = std::clamp (c.pan, -1.0f, 1.0f);
            c.rootPitch = std::clamp (c.rootPitch, 0, 127);
        }

        p.channels = std::move (input);
        return p;
    }
};
} // namespace sls
