#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>

#include "audio/Effects.h"
#include "audio/Project.h"

namespace sls
{
// Sequencer + synth + sampler + mixer (channels -> mixer tracks -> insert effects -> fader -> master effects -> soft limiter). Single-threaded: the caller (audio
// callback or offline renderer) owns it. render() never allocates, locks or does I/O.
class Player
{
public:
    static constexpr int kMaxVoices = 32;
    static constexpr int kChunk = 64; // samples processed per mixer/effects pass
    static constexpr double kMaxLiveSeconds = 20.0; // a held live note is ended after this long, whatever happens

    void prepare (double sampleRate) noexcept;

    // Offline rendering lets sample tails ring out after the transport stops.
    void setReleaseSamplesOnStop (bool shouldRelease) noexcept { releaseSamplesOnStop = shouldRelease; }

    // Adds the output to out[ch][0..numSamples). Channels may be null.
    // When playing is false the transport is stopped and rewound.
    void render (const Project& project, double bpm, bool playing,
                 float* const* out, int numChannels, int numSamples) noexcept;

    double getPositionTicks() const noexcept { return tickPos; }

    // Back to the start of the song, as if the transport had been stopped and started again: the position goes to
    // zero and the sound that was playing fades out. Audio thread only (the engine calls it at the start of a recording).
    void rewind() noexcept;

    // Metronome: a short click on every beat (a higher, louder one on the first beat of each bar), added to the
    // output after the master effects and the limiter. While the transport plays the clicks follow it (beat 1 of the
    // loop is a downbeat); while it is stopped they run freely at `bpm`, starting at once. Off by default, so exports
    // and offline renders never contain it. Audio thread only (the engine calls it before every render).
    void setMetronome (bool on, float volume) noexcept
    {
        metronomeOn = on;
        metronomeVolume = std::clamp (volume, 0.0f, 1.0f);
    }

    // Notes played by hand (piano roll keyboard). They go through the channel's own volume, pan, pitch, mixer track
    // and effects, work with the transport stopped or running, and are not affected by the loop boundary.
    // A synth note sounds until liveNoteOff; a sampler note plays its sample once. `project` must be the snapshot
    // that the next render() call will use. Both calls are real-time safe and ignore a channel that does not exist.
    void liveNoteOn (const Project& project, int channel, int pitch, float velocity) noexcept;
    void liveNoteOff (int channel, int pitch) noexcept;

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
        bool live = false;                  // played by hand: ends on liveNoteOff, not by the sequencer
        int livePitch = 0;
        std::uint64_t liveSamples = 0;
        const SampleData* sample = nullptr; // owned by the current Project snapshot
        double pos = 0.0;
        double step = 1.0;
        std::uint64_t age = 0;
    };

    // Audio clips of the playlist (recordings, imported files): one voice each, played straight through.
    struct ClipVoice
    {
        bool active = false;
        bool releasing = false;
        const SampleData* data = nullptr; // owned by the current Project snapshot
        double pos = 0.0;
        double step = 1.0;
        int endTick = 0;
        int track = 0;
        float gain = 1.0f;
        float env = 0.0f;
        std::uint64_t age = 0;
    };

    void startClip (const AudioEvent& e) noexcept;
    void releaseClips() noexcept;

    void noteOn (const Channel& channel, int channelIndex, const Note& n, int loopLength, bool live = false) noexcept;
    void releaseVoices (bool includeSamplers) noexcept;
    void onProjectChanged (const Project& project) noexcept;

    static constexpr int kMaxClipVoices = 16;
    std::array<ClipVoice, kMaxClipVoices> clipVoices {};
    size_t nextClip = 0;

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
    std::array<EffectUnit, kSlotsPerTrack> masterEffects; // on the master bus
    float rmsCoef = 0.0001f;

    double sampleRate = 48000.0;
    double tickPos = 0.0;
    std::uint64_t projectRevision = ~std::uint64_t { 0 };
    std::uint64_t ageCounter = 0;
    bool wasPlaying = false;
    bool releaseSamplesOnStop = true;
    float releaseStep = 0.001f;
    std::uint64_t liveLimitSamples = 20 * 48000;

    // Metronome state.
    bool metronomeOn = false;
    float metronomeVolume = 0.5f;
    int lastClickBeat = -1;        // while playing: the beat that last clicked
    double freeBeatSamples = 0.0;  // while stopped: samples since the last click (starts "due")
    int freeBeatCount = 0;
    int clickPos = -1;             // samples into the current click, -1 when none
    bool clickAccent = false;
    void startClick (bool accent) noexcept { clickPos = 0; clickAccent = accent; }
    float nextClickSample() noexcept;
};
} // namespace sls
