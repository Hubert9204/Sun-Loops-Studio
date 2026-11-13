#pragma once

#include <array>
#include <functional>
#include <atomic>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>

#include "audio/InputRecorder.h"
#include "audio/RecordSetup.h"
#include "audio/Loudness.h"
#include "audio/LiveNotes.h"
#include "audio/MidiNotes.h"
#include "audio/NoteRecorder.h"
#include "audio/Auditioner.h"
#include "audio/OneShot.h"
#include "audio/Player.h"
#include "audio/Project.h"
#include "audio/ScoreLog.h"
#include "audio/SnapshotExchange.h"
#include "audio/Spectrum.h"
#include "audio/SpscQueue.h"

namespace sls
{
// Owns the audio device, the transport state and the Player.
// The audio callback is real-time safe: no allocation, no locks, no I/O.
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // Message thread.
    // Opens the audio device. `savedState` is what createStateXml() returned in an earlier run (or null); without
    // it the default device is used with a buffer of kDefaultBufferSize. Returns an error message, empty on success.
    juce::String initialise (const juce::XmlElement* savedState = nullptr);
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    juce::String describeDevice() const;

    void publishProject (std::unique_ptr<Project> project) { exchange.publish (std::move (project)); }

    // Plays a note by hand through channel `channel` until the matching off, even when the transport is stopped.
    // Returns false if the event could not be queued. Callable from any non-real-time thread (the piano roll on the
    // message thread, MIDI keyboards on their own threads): the callers take turns on a tiny spin lock that the audio
    // thread never touches.
    bool liveNoteOn (int channel, int pitch, float velocity = 0.8f) noexcept
    {
        return pushLive ({ true, static_cast<std::uint8_t> (channel), static_cast<std::uint8_t> (pitch), velocity });
    }
    bool liveNoteOff (int channel, int pitch) noexcept
    {
        return pushLive ({ false, static_cast<std::uint8_t> (channel), static_cast<std::uint8_t> (pitch), 0.0f });
    }

    // MIDI keyboards: notes arriving from the enabled MIDI inputs (chosen in the Audio dialog) play through this
    // channel of the song. The matching note off always goes to the channel that got the note on.
    void setMidiTargetChannel (int channel) noexcept { midiTarget.store (channel, std::memory_order_relaxed); }
    // The names of the MIDI inputs that are switched on, "" if none.
    juce::String describeMidiInputs() const;
    // True once if a MIDI note has arrived since the last call (for an activity light).
    bool consumeMidiActivity() noexcept { return midiActivity.exchange (false, std::memory_order_relaxed); }
    // Feed a MIDI message as if it had come from a keyboard (also used by the tests).
    void handleMidi (const juce::MidiMessage& message);

    // Recording notes: while this is on and the song is playing, every note that a MIDI keyboard plays is also stamped
    // with the song position and queued for popMidiTake(), which the message thread empties (see NoteRecorder).
    void setNoteRecording (bool on) noexcept { noteRecording.store (on, std::memory_order_relaxed); }
    bool popMidiTake (MidiTake& take) noexcept { return midiTakes.pop (take); } // message thread only

    // The score log (0.28.0): every note a MIDI keyboard plays, at any time, stamped with when it was played (seconds of the audio
    // clock, corrected for latency) and queued for the message thread's ScoreLog.
    bool popScoreEvent (ScoreEvent& e) noexcept { return scoreEvents.pop (e); } // message thread only

    // Effect memory (0.29.0): call from the message thread's timer. Makes the big buffers that reverbs, delays, choruses and Pitch
    // Correctors in more slots than the player's pool covers are waiting for (see Player::effectMemoryShortfall).
    void serviceEffectMemory();
    int timeShapersWithoutMemory() const noexcept { return player.timeShaperShortfall(); } // only shape the volume (0.41.0)

    // Audio input. The input is only opened when the user chooses one in the Audio dialog or presses Record.
    int getNumActiveInputChannels() const;
    // The names of every input channel of the device (also those that are switched off), in order.
    juce::StringArray getInputChannelNames() const;
    // Switches on these input channels of the device (counted from 0), keeping those already on. Error message, or empty.
    juce::String enableInputChannels (const std::vector<int>& channels);

    // Multitrack recording (0.36.0): one file per stream, each from its own input channel(s), all started in the same audio
    // block. With `fromSongStart` the song is rewound first (as before 0.36.0); otherwise it plays on from where it is and
    // getRecordStartTick() tells where the takes begin. Every take is delayed by the round-trip latency so that it lines up
    // with what was playing. The input channels must be on (enableInputChannels). Returns an error message, empty on success;
    // after an error nothing is recorded and no file is left.
    struct RecordStream
    {
        juce::File file;
        int deviceInput = 0; // the device's input channel (from 0); Stereo takes it and the next
        InputRecorder::Channels channels = InputRecorder::Channels::Mono;
    };
    static constexpr int kMaxRecordStreams = 8;
    // `countInBeats` (0.47.0): the metronome counts that many beats first (with the transport stopped), then the song and the
    // takes start together, on the beat after the last click. stopRecording() during the count-in cancels it.
    juce::String startRecording (const std::vector<RecordStream>& streams, bool fromSongStart, int countInBeats = 0);
    bool isCountingIn() const noexcept { return countingIn.load (std::memory_order_relaxed); }
    std::vector<InputRecorder::Take> stopRecording(); // in the order of the streams; the transport keeps running
    double getRecordedSeconds() const noexcept { return recorders[0].getSecondsRecorded(); }
    float consumeStreamPeak (int stream) noexcept
    {
        return stream >= 0 && stream < kMaxRecordStreams ? recorders[static_cast<size_t> (stream)].consumePeak() : 0.0f;
    }
    // The song position (ticks) of the takes' first frame, or -1 before the audio thread has started them.
    double getRecordStartTick() const noexcept { return recordStartTick.load (std::memory_order_acquire); }
    // Hear the input through the speakers (adds its latency; keep off with speakers and a microphone: feedback).
    void setInputMonitoring (bool on) noexcept { monitorInput.store (on, std::memory_order_relaxed); }
    // Loudest input sample since the last call (0 without input).
    float consumeInputPeak() noexcept { return inputPeak.exchange (0.0f, std::memory_order_relaxed); }

    // The start-up sound: a sample that is mixed straight into the output (after the master volume, so it is always as
    // loud as the file), whatever the transport is doing. setStartupSound must be called before initialise();
    // playStartupSound can be called from any thread, any number of times (it starts the sound over).
    void setStartupSound (std::shared_ptr<const SampleData> sound) noexcept { startup.setSound (std::move (sound)); }
    Auditioner& auditioner() noexcept { return audition; } // the sample editor's preview
    void playStartupSound() noexcept { startupTrigger.store (true, std::memory_order_release); }

    // Any thread.
    void setPlaying (bool shouldPlay) noexcept { playing.store (shouldPlay, std::memory_order_relaxed); }
    bool isPlaying() const noexcept            { return playing.load (std::memory_order_relaxed); }
    void setBpm (double newBpm) noexcept       { bpm.store (newBpm, std::memory_order_relaxed); }
    void setGain (float newGain) noexcept      { gain.store (newGain, std::memory_order_relaxed); }
    void setMetronome (bool on, float volume) noexcept
    {
        metronomeOn.store (on, std::memory_order_relaxed);
        metronomeVolume.store (volume, std::memory_order_relaxed);
    }
    int getPlayheadTick() const noexcept       { return playheadTick.load (std::memory_order_relaxed); }

    // Plugins (0.17.0). The sample rate the device runs at (what new plugins are prepared for).
    double getSampleRate() const noexcept { return deviceRate.load (std::memory_order_relaxed); }
    // Called (not on the audio thread, with the audio stopped) whenever the device starts, with its sample rate: plugins are prepared again there.
    std::function<void (double)> onDeviceStarting;
    // While suspended the audio callback outputs silence and does not run the Player, so another thread (an export) may use the plugins.
    // setSuspended (true) returns once the callback is no longer inside the Player (or after about half a second). Message thread.
    void setSuspended (bool shouldSuspend);
    // Moves the song position to `tick` (playing or stopped); it is also where Stop returns to. Applied by the next audio block.
    void requestSeek (int tick) noexcept { seekRequest.store (std::max (0, tick), std::memory_order_release); }
    // 0.62.0: true once after the transport stopped itself at a Pause marker (message thread), and the marker's tick.
    bool takeMarkerPause (int& tick) noexcept
    {
        if (! markerPaused.exchange (false, std::memory_order_acq_rel))
            return false;
        tick = markerPauseTick.load (std::memory_order_relaxed);
        return true;
    }

    // Peak since the last call (master, and per mixer channel).
    float consumePeak() noexcept { return peak.exchange (0.0f, std::memory_order_relaxed); }
    float consumeChannelPeak (int channel) noexcept
    {
        return channel >= 0 && channel < kMaxChannels
                   ? channelPeaks[static_cast<size_t> (channel)].exchange (0.0f, std::memory_order_relaxed)
                   : 0.0f;
    }

    // Load of the audio callback: the longest time one block took to compute divided by the time that block lasts,
    // since the last call (1.0 means a block took all of its time: that is a dropout). Message thread.
    float consumePeakLoad() noexcept { return peakLoad.exchange (0.0f, std::memory_order_relaxed); }
    // For measuring headroom (the buffer "Auto" button): while set, every audio block is kept busy for this fraction
    // of its time (0 = off, at most 0.6) by spinning after the real work. The load that consumePeakLoad() reports
    // does not include the spinning, so it stays the song's own cost; the spinning only makes a computer that is
    // short of headroom drop blocks sooner, which getXRunCount() then shows.
    void setLoadTest (float fractionOfBlock) noexcept
    {
        loadTest.store (fractionOfBlock > 0.0f ? (fractionOfBlock < 0.6f ? fractionOfBlock : 0.6f) : 0.0f,
                        std::memory_order_relaxed);
    }
    // Blocks the audio system reports as dropped since the device was opened; -1 if it cannot tell.
    int getXRunCount() const noexcept { return deviceManager.getXRunCount(); }

    // Mixer track levels (after effects and fader, scaled by the master volume): peak since the last call,
    // and the latest RMS.
    // What the program sends to the speakers (after the master volume): loudness and peaks, and the latest samples for the
    // spectrum analyser and the goniometer. Readable from any thread; resetLoudness() starts a new integrated measurement.
    const LoudnessMeter& getLoudness() const noexcept { return loudness; }
    void resetLoudness() noexcept { loudness.requestReset(); }
    const StereoRing& getOutputRing() const noexcept { return outputRing; }

    float getMasterRms() const noexcept { return masterRms.load (std::memory_order_relaxed); }
    float consumeTrackPeak (int track) noexcept
    {
        return track >= 0 && track < kMaxTracks
                   ? trackPeaks[static_cast<size_t> (track)].exchange (0.0f, std::memory_order_relaxed)
                   : 0.0f;
    }
    float getTrackRms (int track) const noexcept
    {
        return track >= 0 && track < kMaxTracks
                   ? trackRms[static_cast<size_t> (track)].load (std::memory_order_relaxed)
                   : 0.0f;
    }
    float consumeAuxPeak (int aux) noexcept
    {
        return aux >= 0 && aux < kAuxBuses ? auxPeaks[static_cast<size_t> (aux)].exchange (0.0f, std::memory_order_relaxed) : 0.0f;
    }
    float getAuxRms (int aux) const noexcept
    {
        return aux >= 0 && aux < kAuxBuses ? auxRms[static_cast<size_t> (aux)].load (std::memory_order_relaxed) : 0.0f;
    }

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    static_assert (std::atomic<double>::is_always_lock_free, "bpm must be lock-free");

    static void raiseTo (std::atomic<float>& target, float value) noexcept
    {
        float current = target.load (std::memory_order_relaxed);
        while (value > current && ! target.compare_exchange_weak (current, value, std::memory_order_relaxed))
        {
        }
    }

    class MidiReceiver : public juce::MidiInputCallback
    {
    public:
        explicit MidiReceiver (AudioEngine& e) : engine (e) {}
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override { engine.handleMidi (message); }

    private:
        AudioEngine& engine;
    };

    bool pushLive (const LiveNoteEvent& e) noexcept
    {
        const juce::SpinLock::ScopedLockType lock (producerLock);
        return liveNotes.push (e);
    }
    void watchMidiDevices();

    juce::AudioDeviceManager deviceManager;
    SnapshotExchange<Project> exchange;
    Player player;
    LiveNoteQueue liveNotes;
    juce::SpinLock producerLock; // serialises the threads that produce live notes; never taken by the audio thread
    MidiNoteMapper midiMapper;   // guarded by producerLock
    MidiReceiver midiReceiver { *this };
    juce::MidiDeviceListConnection midiDeviceWatch;
    juce::StringArray knownMidiInputs;
    std::array<InputRecorder, kMaxRecordStreams> recorders;
    std::atomic<bool> captureGate { false };      // set once every recorder of a take is armed: they all start in one block
    std::atomic<bool> captureFromStart { true };  // rewind the song when the take starts
    std::atomic<double> recordStartTick { -1.0 };
    bool captureStarted = false; // audio thread only: the take has started (and the song was rewound for it if asked)
    // Count-in (0.47.0): beats asked for by startRecording, a cancel from stopRecording, and the audio thread's own countdown.
    std::atomic<int> countInRequest { 0 };
    std::atomic<bool> countInCancel { false };
    std::atomic<bool> countingIn { false };
    long long countInLeft = 0; // audio thread only: samples of count-in still to play
    Auditioner audition;                            // preview of one sample; hand-over rules in Auditioner.h
    OneShot startup;                                // set before the device starts; played by the audio thread only
    std::atomic<bool> startupTrigger { false };
    SpscQueue<MidiTake, 256> midiTakes; // audio thread -> message thread
    SpscQueue<ScoreEvent, 1024> scoreEvents; // audio thread -> message thread
    int offeredMemory = 0;   // message thread: effect memory handed to the player and not yet counted in its pool
    int lastMemoryCount = 0; // message thread: the pool's size at the last look
    double audioClockSeconds = 0.0;          // audio thread only: time played since the engine was made (only goes forwards)
    std::atomic<bool> noteRecording { false };
    std::atomic<double> outputLatencySamples { 0.0 };
    std::atomic<int> midiTarget { 0 };
    std::atomic<bool> midiActivity { false };
    std::atomic<bool> monitorInput { false };
    std::atomic<float> inputPeak { 0.0f };

    std::atomic<bool> playing { false };
    std::atomic<bool> markerPaused { false }; // 0.62.0
    std::atomic<int> markerPauseTick { 0 };
    std::atomic<double> bpm { 120.0 };
    std::atomic<float> gain { 0.8f };
    std::atomic<float> peak { 0.0f };
    std::atomic<float> masterRms { 0.0f };
    LoudnessMeter loudness;
    StereoRing outputRing;
    std::atomic<float> peakLoad { 0.0f };
    std::atomic<float> loadTest { 0.0f };
    std::atomic<bool> metronomeOn { false };
    std::atomic<float> metronomeVolume { 0.5f };
    float masterMeanSquare = 0.0f; // audio thread only
    double sampleRateHz = 48000.0; // set in audioDeviceAboutToStart
    std::array<std::atomic<float>, kMaxChannels> channelPeaks {};
    std::array<std::atomic<float>, kMaxTracks> trackPeaks {};
    std::array<std::atomic<float>, kMaxTracks> trackRms {};
    std::array<std::atomic<float>, kAuxBuses> auxPeaks {};
    std::array<std::atomic<float>, kAuxBuses> auxRms {};
    std::atomic<int> playheadTick { 0 };
    std::atomic<int> seekRequest { -1 };
    std::atomic<bool> suspended { false };
    std::atomic<bool> inCallback { false };
    std::atomic<double> deviceRate { 48000.0 };
};
} // namespace sls
