#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <vector>

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
        float padGain = 1.0f;               // the pad's (or a sampler zone's) level and pan (1 / 1 / 1 for every other voice)
        float padLeft = 1.0f;
        float padRight = 1.0f;

        // Sampler voices (0.16.0).
        bool held = false;                  // sounds while the note is held, then releases (envelope or loop); otherwise one-shot
        double loopStart = 0.0, loopEnd = 0.0; // frames; a loop when loopEnd > loopStart + 1
        float sustainLevel = 1.0f;          // the envelope decays to this level after the attack
        float decayStep = 0.0f;
        float relStep = 0.0f;               // 0: the player's usual short release
        SamplerFilter filter = SamplerFilter::Off;
        float fa1 = 0.0f, fa2 = 0.0f, fa3 = 0.0f, fk = 0.0f; // state-variable filter coefficients
        float s1L = 0.0f, s2L = 0.0f, s1R = 0.0f, s2R = 0.0f; // and its state (a subtractive voice: two mono filters in a row)

        // Subtractive synth voices (0.18.0). The patch is read from the voice's channel in the snapshot; `increment` stays the note's
        // rate (radians per sample, so legato and slide work as for the classic synth) and the oscillators run at ratios of it.
        bool subtractive = false;
        double ph1 = 0.0, ph2 = 0.0;        // oscillator phases, 0..1
        double ratio1 = 1.0, ratio2 = 1.0;  // oscillator frequency / note frequency
        float lfoPhase = 0.0f, lfoInc = 0.0f;
        std::uint32_t noise = 22222u;       // noise generator state
        float fenv = 0.0f;                  // filter envelope, 0..1
        std::uint8_t fstage = 0;            // 0 attack, 1 decay / sustain, 2 release
        float fAttackStep = 1.0f, fDecayStep = 0.0f, fSustain = 0.0f, fRelStep = 0.0f;
        float keyOctaves = 0.0f;            // key tracking of the cutoff, in octaves from C4
        int controlLeft = 0;                // samples until the LFO, the cutoff and the pitch modulation are computed again
        double pitchMod = 1.0;
        float pwMod = 0.0f, ampMod = 1.0f;

        // FM voices (0.19.0): four sine operators with their own envelopes; the last output of each feeds the matrix.
        bool fm = false;
        std::array<double, kFmOperators> opPhase {};
        std::array<float, kFmOperators> opOut {}, opEnv {}, opAttack {}, opDecay {}, opSustain {}, opRelease {};
        std::array<std::uint8_t, kFmOperators> opStage {}; // 0 attack, 1 decay / sustain, 2 release

        // Additive voices (0.24.0): a rotating phasor (re, im) per harmonic, turned by (cos, sin) every sample; the levels and the
        // rotations are computed again every kControlInterval samples and the levels move there in a straight line.
        bool additive = false;
        std::array<float, kAdditivePartials> partRe {}, partIm {}, partCos {}, partSin {}, partAmp {}, partStep {};
        float menv = 0.0f;                  // morph envelope, 0..1
        std::uint8_t mstage = 0;
        float mAttackStep = 1.0f, mDecayStep = 0.0f, mSustain = 0.0f, mRelStep = 0.0f;

        // Physical voices (0.24.0). String: a delay line (owned by the Player, one per voice) with a loss filter, an all-pass for
        // stiffness and a body; struck models: up to kModes resonators excited by a short pulse.
        bool physical = false;
        bool modal = false;
        float* line = nullptr;
        int lineMask = 0, writePos = 0;
        double delaySamples = 2.0;          // the loop's delay for the current pitch
        float loopGain = 0.0f, loopBlend = 0.5f, lossState = 0.0f;
        float apCoef = 0.0f, apIn = 0.0f, apOut = 0.0f, apDelay = 0.0f;
        float bodyAmount = 0.0f, dcIn = 0.0f, dcOut = 0.0f;
        std::array<float, 6> bodyState {}; // three band-pass filters, two states each
        float sustainGain = 0.0f, releaseGain = 0.0f; // loop gains while held and once released
        static constexpr int kModes = 12;
        int modeCount = 0;
        std::array<float, kModes> modeRatio {}, modeB0 {}, modeA1 {}, modeA2 {}, modeY1 {}, modeY2 {}, modeR {}, modeRelR {}, modeAmp {};
        int strikeLeft = 0, strikeLength = 1;
        float strikeNorm = 1.0f;
        bool released = false;
    };
    static constexpr int kControlInterval = 16; // samples between two updates of a subtractive voice's modulation
    void subtractiveVoiceOn (Voice& v, const SynthPatch& patch, int pitch) noexcept;
    float renderSubtractive (Voice& v, const SynthPatch& patch) noexcept;
    void fmVoiceOn (Voice& v, const SynthPatch& patch) noexcept;
    float renderFm (Voice& v, const SynthPatch& patch) noexcept;
    void updateModulation (Voice& v, const SynthPatch& patch) noexcept; // the LFO and what it drives, every kControlInterval samples
    void additiveVoiceOn (Voice& v, const SynthPatch& patch, int pitch) noexcept;
    void updateAdditive (Voice& v, const SynthPatch& patch) noexcept;
    float renderAdditive (Voice& v, const SynthPatch& patch) noexcept;
    void physicalVoiceOn (Voice& v, const SynthPatch& patch, size_t voiceIndex) noexcept;
    void updatePhysical (Voice& v, const SynthPatch& patch) noexcept;
    float renderPhysical (Voice& v, const SynthPatch& patch) noexcept;
    std::vector<float> stringLines; // kMaxVoices delay lines of stringLineLength samples (a power of two), made in prepare()
    int stringLineLength = 0;
    std::array<std::array<float, 5>, 3> bodyCoefs {}; // the body's band-pass filters (b0, b1, b2, a1, a2) at this sample rate

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

    // Plugin instruments (0.17.0): notes become MIDI events at their sample in the 64-sample chunk; the plugin renders the chunk after
    // the voices, so it is as sample-accurate as the built-in instruments. The notes that are sounding are tracked to send their note-offs.
    struct PluginNote
    {
        const HostedPlugin* plugin = nullptr;
        int channel = 0;
        int pitch = 0;
        int endTick = 0;
        bool live = false;
    };
    static constexpr int kMaxPluginNotes = 256;
    static constexpr int kMaxPluginEvents = 256; // per channel and chunk; more are dropped
    void pluginNoteOn (const Channel& channel, int channelIndex, const Note& n, int loopLength, bool live) noexcept;
    void pluginEvent (int channelIndex, std::uint8_t status, int data1, int data2) noexcept;
    void pluginNoteOff (int noteIndex) noexcept; // ends pluginNotes[noteIndex] and removes it
    void releasePluginNotes() noexcept;          // every note not held by hand
    std::array<PluginNote, kMaxPluginNotes> pluginNotes {};
    int numPluginNotes = 0;
    std::array<std::array<PluginMidiEvent, kMaxPluginEvents>, kMaxChannels> pluginEvents {};
    std::array<int, kMaxChannels> pluginEventCount {};
    std::array<float, kChunk> pluginL {}, pluginR {};
    int chunkSample = 0; // the sample of the current chunk being rendered (where new MIDI events go)
    // The next note of every channel and the next audio clip from `tick` on (no sound changes).
    void locate (const Project& project, double tick) noexcept;
    // Starts the audio clips that began before `tick` and are still going there, from the matching point in their sound.
    void startClipsInside (const Project& project, double tick, double ticksPerSecond) noexcept;
    // Moves the transport to `tick` while it plays (the loop region and the end of the song): sounds are handled as at the loop boundary.
    void jumpTo (const Project& project, double tick, double ticksPerSecond) noexcept;
    void releaseClips() noexcept;

    Voice& takeVoice() noexcept;
    void drumNoteOn (const Channel& channel, int channelIndex, const Note& n) noexcept;
    // Starts one sampler voice (the channel's sample, or one zone of its program).
    void samplerVoiceOn (const Channel& channel, int channelIndex, const Note& n, int loopLength, bool live, const SampleData* sample,
                         double rootKey, double tune, double keyTrack, float gain, float pan, double loopStart, double loopEnd) noexcept;
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
