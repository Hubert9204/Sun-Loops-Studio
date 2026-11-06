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

    // Moves the song position to `tick` (clamped into the project; past its end means the start), playing or not.
    // The notes that are sounding stop (with the usual short fade); audio clips that cover `tick` start from the middle,
    // at the right place in their sound. The position also becomes the place that Stop returns to. Audio thread only.
    void seek (const Project& project, double tick, double bpm) noexcept;
    double getStartPosition() const noexcept { return startPosition; }

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
    // The same for aux bus `aux` (after its effects and fader).
    float getAuxPeak (int aux) const noexcept
    {
        return aux >= 0 && aux < kAuxBuses ? auxPeaks[static_cast<size_t> (aux)] : 0.0f;
    }
    float getAuxRms (int aux) const noexcept
    {
        return aux >= 0 && aux < kAuxBuses ? std::sqrt (auxMeanSquare[static_cast<size_t> (aux)]) : 0.0f;
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
        int glideLeft = 0;                  // samples left of a pitch slide (multiplies increment / step by glideRatio each sample)
        double glideRatio = 1.0;
        double glideTarget = 0.0;           // the exact rate at the end of the slide
        int pad = -1;                       // drum voices: the pad (0..15) and its choke group; -1 for every other voice
        int choke = 0;
        float padGain = 1.0f;               // the pad's level and pan (1 / 1 / 1 for every other voice)
        float padLeft = 1.0f;
        float padRight = 1.0f;
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

    void startClip (const AudioEvent& e, double extraSeconds = 0.0) noexcept;
    // The next note of every channel and the next audio clip from `tick` on (no sound changes).
    void locate (const Project& project, double tick) noexcept;
    // Starts the audio clips that began before `tick` and are still going there, from the matching point in their sound.
    void startClipsInside (const Project& project, double tick, double ticksPerSecond) noexcept;
    // Moves the transport to `tick` while it plays (the loop region and the end of the song): sounds are handled as at the loop boundary.
    void jumpTo (const Project& project, double tick, double ticksPerSecond) noexcept;
    void releaseClips() noexcept;

    Voice& takeVoice() noexcept;
    void drumNoteOn (const Channel& channel, int channelIndex, const Note& n) noexcept;
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

    // Aux buses: the sends of every track are summed here, run through the bus's effects and fader, and added to the master bus.
    std::array<std::array<EffectUnit, kSlotsPerTrack>, kAuxBuses> auxEffects;
    std::array<std::array<float, kChunk>, kAuxBuses> auxL {}, auxR {};
    std::array<std::array<float, kAuxBuses>, kMaxTracks> sendGain {}; // smoothed send levels
    std::array<float, kMaxTracks> sendGate {};                        // smoothed 1 / 0: the track is audible (mute / solo)
    std::array<float, kAuxBuses> auxGain {};
    std::array<float, kAuxBuses> auxPeaks {};
    std::array<float, kAuxBuses> auxMeanSquare {};
    float rmsCoef = 0.0001f;

    double sampleRate = 48000.0;
    double ticksPerSampleNow = 0.01; // of the block being rendered: converts a note's length to samples for slides
    double tickPos = 0.0;
    double startPosition = 0.0; // where Stop puts the transport (the last seek)
    std::uint64_t projectRevision = ~std::uint64_t { 0 };
    std::uint64_t ageCounter = 0;
    bool wasPlaying = false;
    bool releaseSamplesOnStop = true;
    float releaseStep = 0.001f;
    std::uint64_t liveLimitSamples = 20ull * 48000;

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
