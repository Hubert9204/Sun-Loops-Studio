#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/Effects.h"
#include "audio/NoteSequence.h"

namespace sls
{
constexpr int kMaxChannels = 8;
constexpr int kMaxTracks = 8; // mixer tracks; every channel is routed to exactly one of them
constexpr float kMaxSwingShiftTicks = 60.0f; // swing 100 % delays off-beat notes by half a 16th step

// Decoded audio kept in memory. Immutable once shared.
struct SampleData
{
    std::vector<float> left, right;
    double sampleRate = 44100.0;
    std::string name;
    std::string source; // where it came from: an absolute file path, or "builtin:<id>"; used by channel presets

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
    float pitch = 0.0f;                       // channel tuning in semitones (-24..+24), applied to every note
    float swing = 0.0f;                       // 0..1: delays notes that fall on the second half of every 8th
    int mixerTrack = 0;                       // which mixer track this channel plays through (0-based)
};

// One vertical strip of the mixer: its own fader, balance, mute/solo and a chain of insert effects.
// The channels routed to it are summed, run through the effects (pre-fader), then through the fader.
struct MixerTrack
{
    std::string name = "Track";
    float volume = 1.0f; // 0..1.5, 1 = unity
    float pan = 0.0f;    // -1..+1, balance: the louder side stays at unity
    bool mute = false;
    bool solo = false;
    std::array<EffectSlot, kSlotsPerTrack> slots {};
};

inline float finiteOr (float v, float fallback) noexcept { return std::isfinite (v) ? v : fallback; }

// Immutable snapshot of everything the audio thread needs.
// Build it with Project::make() on the message thread and hand it to a SnapshotExchange.
struct Project
{
    std::vector<Channel> channels;
    std::vector<MixerTrack> tracks; // always kMaxTracks entries after make()
    int lengthTicks = 8 * kTicksPerBeat;
    std::uint64_t revision = 0; // assigned by SnapshotExchange

    static std::vector<MixerTrack> defaultTracks()
    {
        std::vector<MixerTrack> t (static_cast<size_t> (kMaxTracks));
        for (size_t i = 0; i < t.size(); ++i)
            t[i].name = "Track " + std::to_string (i + 1);
        return t;
    }

    // Swing: notes on the second half of every 8th (the off-beat 16ths) are delayed. Notes that would be
    // pushed past the end of the loop stay where they are.
    static void applySwing (std::vector<Note>& notes, float swing, int loopLengthTicks)
    {
        const int shift = static_cast<int> (std::lround (swing * kMaxSwingShiftTicks));
        if (shift <= 0)
            return;

        constexpr int kStep = kTicksPerBeat / 4;  // 16th note
        for (auto& n : notes)
            if (n.startTick >= 0 && (n.startTick / kStep) % 2 == 1 && n.startTick + shift < loopLengthTicks)
                n.startTick += shift;
    }

    static Project make (std::vector<Channel> input, int loopLengthTicks, std::vector<MixerTrack> tracks = {})
    {
        Project p;
        p.lengthTicks = std::max (1, loopLengthTicks);

        if (input.size() > static_cast<size_t> (kMaxChannels))
            input.resize (kMaxChannels);

        for (auto& c : input)
        {
            c.swing = std::clamp (finiteOr (c.swing, 0.0f), 0.0f, 1.0f);
            applySwing (c.notes, c.swing, p.lengthTicks);
            c.notes = Sequence::make (std::move (c.notes), p.lengthTicks).notes; // validate + sort
            c.volume = std::clamp (c.volume, 0.0f, 1.5f);
            c.pan = std::clamp (c.pan, -1.0f, 1.0f);
            c.rootPitch = std::clamp (c.rootPitch, 0, 127);
            c.pitch = std::clamp (finiteOr (c.pitch, 0.0f), -24.0f, 24.0f);
            c.mixerTrack = std::clamp (c.mixerTrack, 0, kMaxTracks - 1);
        }

        if (tracks.empty())
            tracks = defaultTracks();
        tracks.resize (static_cast<size_t> (kMaxTracks));
        for (auto& t : tracks)
        {
            t.volume = std::clamp (finiteOr (t.volume, 1.0f), 0.0f, 1.5f);
            t.pan = std::clamp (finiteOr (t.pan, 0.0f), -1.0f, 1.0f);
            for (auto& slot : t.slots)
                slot = slot.sanitised();
        }

        p.channels = std::move (input);
        p.tracks = std::move (tracks);
        return p;
    }
};
} // namespace sls
