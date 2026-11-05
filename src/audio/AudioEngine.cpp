#include "audio/AudioEngine.h"

#include <cmath>

namespace sls
{
AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

juce::String AudioEngine::initialise()
{
    // 0 inputs, 2 outputs. JUCE picks ALSA or JACK on Linux; PipeWire is reached
    // through its pipewire-alsa / pipewire-jack compatibility layers.
    auto error = deviceManager.initialiseWithDefaultDevices (0, 2);
    if (error.isEmpty())
        deviceManager.addAudioCallback (this);
    return error;
}

juce::String AudioEngine::describeDevice() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return "No audio device";

    const double sr = device->getCurrentSampleRate();
    const int buf = device->getCurrentBufferSizeSamples();
    const int latency = device->getOutputLatencyInSamples();

    return device->getTypeName() + " / " + device->getName()
         + "  |  " + juce::String (sr, 0) + " Hz"
         + "  |  buffer " + juce::String (buf)
         + "  |  output latency " + juce::String (latency)
         + " samples (" + juce::String (sr > 0.0 ? 1000.0 * latency / sr : 0.0, 1) + " ms)";
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRateHz = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
    masterMeanSquare = 0.0f;
    player.prepare (sampleRateHz);
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                    float* const* out, int numOut,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    const juce::ScopedNoDenormals noDenormals; // reverb/delay tails decay through the denormal range
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr)
            juce::FloatVectorOperations::clear (out[ch], numSamples);

    const Project* project = exchange.acquire();
    const bool isPlaying = playing.load (std::memory_order_relaxed);

    player.render (*project, bpm.load (std::memory_order_relaxed), isPlaying, out, numOut, numSamples);
    playheadTick.store (static_cast<int> (player.getPositionTicks()), std::memory_order_relaxed);

    const float g = gain.load (std::memory_order_relaxed);

    for (int ch = 0; ch < kMaxChannels; ++ch)
        raiseTo (channelPeaks[static_cast<size_t> (ch)], player.getChannelPeak (ch) * g);

    for (int t = 0; t < kMaxTracks; ++t)
    {
        raiseTo (trackPeaks[static_cast<size_t> (t)], player.getTrackPeak (t) * g);
        trackRms[static_cast<size_t> (t)].store (player.getTrackRms (t) * g, std::memory_order_relaxed);
    }

    float localPeak = 0.0f;
    double sumSquares = 0.0;
    int measuredChannels = 0;
    for (int ch = 0; ch < numOut; ++ch)
    {
        if (out[ch] == nullptr)
            continue;
        juce::FloatVectorOperations::multiply (out[ch], g, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            localPeak = std::fmax (localPeak, std::fabs (out[ch][i]));
            sumSquares += static_cast<double> (out[ch][i]) * out[ch][i];
        }
        ++measuredChannels;
    }

    raiseTo (peak, localPeak);

    // RMS over a sliding ~300 ms window, like the mixer tracks.
    if (measuredChannels > 0 && numSamples > 0)
    {
        const float blockMs = static_cast<float> (sumSquares / (static_cast<double> (numSamples) * measuredChannels));
        const float k = static_cast<float> (1.0 - std::exp (-static_cast<double> (numSamples) / (0.3 * sampleRateHz)));
        masterMeanSquare += (blockMs - masterMeanSquare) * k;
        if (masterMeanSquare < 1.0e-20f)
            masterMeanSquare = 0.0f;
        masterRms.store (std::sqrt (masterMeanSquare), std::memory_order_relaxed);
    }
}
} // namespace sls
