#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>

#include "audio/Effects.h"
#include "audio/Project.h"

namespace sls
{
// Sequencer + synth + sampler + mixer (channels -> mixer tracks -> insert effects -> fader -> master). Single-threaded: the caller (audio
// callback or offline renderer) owns it. render() never allocates, locks or does I/O.
class Player
{
public:
    static constexpr int kMaxVoices = 32;
    static constexpr int kChunk = 64; // samples processed per mixer/effects pass

    void prepare (double sampleRate) noexcept;

    // Offline rendering lets sample tails ring out after the transport stops.
    void setReleaseSamplesOnStop (bool shouldRelease) noexcept { releaseSamplesOnStop = shouldRelease; }

    // Adds the output to out[ch][0..numSamples). Channels may be null.
    // When playing is false the transport is stopped and rewound.
    void render (const Project& project, double bpm, bool playing,
                 float* const* out, int numChannels, int numSamples) noexcept;

    double getPositionTicks() const noexcept { return tickPos; }

    // Peak (after volume and pan) of mixer channel `channel` in the last rendered block.
    float getChannelPeak (int channel) const noexcept
    {
        return channel >= 0 && channel < kMaxChannels ? peaks[static_cast<size_t> (channel)] : 0.0f;
    }

    // Level of mixer track `track` in the last rendered block, measured after its effects and fader
    // (before the master volume): peak, and RMS over a sliding ~300 ms window.
    float getTrackPeak (int track) const noexcept
    {
        return track >= 0 && track < kMaxTracks ? trackPeaks[static_cast<size_t> (track)] : 0.0f;
    }
    float getTrackRms (int track) const noexcept
    {
        return track >= 0 && track < kMaxTracks ? std::sqrt (trackMeanSquare[static_cast<size_t> (track)]) : 0.0f;
    }

private:
    enum class State : std::uint8_t { Idle, Attack, Sustain, Release };

    struct Voice
    {
        State state = State::Idle;
        bool sampler = false;
        int channel = 0;
        int endTick = 0;
        double phase = 0.0;
        double increment = 0.0;
        float velocity = 0.0f;
        float env = 0.0f;
        float attackStep = 0.01f;
        const SampleData* sample = nullptr; // owned by the current Project snapshot
        double pos = 0.0;
        double step = 1.0;
        std::uint64_t age = 0;
    };

    void noteOn (const Channel& channel, int channelIndex, const Note& n, int loopLength) noexcept;
    void releaseVoices (bool includeSamplers) noexcept;
    void onProjectChanged (const Project& project) noexcept;

    std::array<Voice, kMaxVoices> voices {};
    std::array<std::size_t, kMaxChannels> nextEvent {};
    std::array<float, kMaxChannels> smoothedGain {};
    std::array<float, kMaxChannels> peaks {};

    // Mixer tracks.
    std::array<std::array<float, kChunk>, kMaxTracks> trackL {}, trackR {};
    std::array<float, kMaxTracks> trackGain {};
    std::array<float, kMaxTracks> trackPeaks {};
    std::array<float, kMaxTracks> trackMeanSquare {};
    std::array<std::array<EffectUnit, kSlotsPerTrack>, kMaxTracks> effects;
    float rmsCoef = 0.0001f;

    double sampleRate = 48000.0;
    double tickPos = 0.0;
    std::uint64_t projectRevision = ~std::uint64_t { 0 };
    std::uint64_t ageCounter = 0;
    bool wasPlaying = false;
    bool releaseSamplesOnStop = true;
    float releaseStep = 0.001f;
};
} // namespace sls
