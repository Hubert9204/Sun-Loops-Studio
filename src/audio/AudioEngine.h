#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>

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
    juce::String initialise();      // returns an error message, empty on success
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    juce::String describeDevice() const;

    void publishProject (std::unique_ptr<Project> project) { exchange.publish (std::move (project)); }

    // Any thread.
    void setPlaying (bool shouldPlay) noexcept { playing.store (shouldPlay, std::memory_order_relaxed); }
    bool isPlaying() const noexcept            { return playing.load (std::memory_order_relaxed); }
    void setBpm (double newBpm) noexcept       { bpm.store (newBpm, std::memory_order_relaxed); }
    void setGain (float newGain) noexcept      { gain.store (newGain, std::memory_order_relaxed); }
    int getPlayheadTick() const noexcept       { return playheadTick.load (std::memory_order_relaxed); }

    // Peak since the last call (master, and per mixer channel).
    float consumePeak() noexcept { return peak.exchange (0.0f, std::memory_order_relaxed); }
    float consumeChannelPeak (int channel) noexcept
    {
        return channel >= 0 && channel < kMaxChannels
                   ? channelPeaks[static_cast<size_t> (channel)].exchange (0.0f, std::memory_order_relaxed)
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

    std::atomic<bool> playing { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<float> gain { 0.8f };
    std::atomic<float> peak { 0.0f };
    std::array<std::atomic<float>, kMaxChannels> channelPeaks {};
    std::atomic<int> playheadTick { 0 };
};
} // namespace sls
