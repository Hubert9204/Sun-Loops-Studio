#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>

#include "audio/LiveNotes.h"
#include "audio/Player.h"
#include "audio/Project.h"
#include "audio/SnapshotExchange.h"

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

    // Message thread only (one producer). Plays a note by hand through channel `channel` until the matching
    // off, even when the transport is stopped. Returns false if the event could not be queued.
    bool liveNoteOn (int channel, int pitch, float velocity = 0.8f) noexcept
    {
        return liveNotes.push ({ true, static_cast<std::uint8_t> (channel), static_cast<std::uint8_t> (pitch), velocity });
    }
    bool liveNoteOff (int channel, int pitch) noexcept
    {
        return liveNotes.push ({ false, static_cast<std::uint8_t> (channel), static_cast<std::uint8_t> (pitch), 0.0f });
    }

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

    juce::AudioDeviceManager deviceManager;
    SnapshotExchange<Project> exchange;
    Player player;
    LiveNoteQueue liveNotes;

    std::atomic<bool> playing { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<float> gain { 0.8f };
    std::atomic<float> peak { 0.0f };
    std::atomic<float> masterRms { 0.0f };
    std::atomic<float> peakLoad { 0.0f };
    std::atomic<float> loadTest { 0.0f };
    std::atomic<bool> metronomeOn { false };
    std::atomic<float> metronomeVolume { 0.5f };
    float masterMeanSquare = 0.0f; // audio thread only
    double sampleRateHz = 48000.0; // set in audioDeviceAboutToStart
    std::array<std::atomic<float>, kMaxChannels> channelPeaks {};
    std::array<std::atomic<float>, kMaxTracks> trackPeaks {};
    std::array<std::atomic<float>, kMaxTracks> trackRms {};
    std::atomic<int> playheadTick { 0 };
};
} // namespace sls
