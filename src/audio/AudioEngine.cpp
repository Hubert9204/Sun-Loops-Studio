#include "audio/AudioEngine.h"

#include <cmath>

#include "audio/BufferTuning.h"

namespace sls
{
AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    midiDeviceWatch.reset();
    deviceManager.removeMidiInputDeviceCallback ({}, &midiReceiver);
    deviceManager.removeAudioCallback (this);
    recorder.stop();
    deviceManager.closeAudioDevice();
}

juce::String AudioEngine::initialise (const juce::XmlElement* savedState)
{
    // 0 inputs, 2 outputs. JUCE picks ALSA or JACK on Linux; PipeWire is reached
    // through its pipewire-alsa / pipewire-jack compatibility layers. A saved state brings back the device and
    // buffer size the user chose; the first time the buffer is a safe 1024 rather than the device's own default.
    juce::AudioDeviceManager::AudioDeviceSetup preferred;
    preferred.bufferSize = kDefaultBufferSize;
    preferred.useDefaultInputChannels = false; // no input is opened until the user asks for one (privacy, and less to go wrong)
    auto error = deviceManager.initialise (2, 2, savedState, true, {}, &preferred);
    if (error.isEmpty())
    {
        deviceManager.addAudioCallback (this);
        deviceManager.addMidiInputDeviceCallback ({}, &midiReceiver); // every enabled MIDI input

        // First run: switch on every MIDI input that is connected, so a keyboard just works. Later runs keep what the
        // user chose in the Audio dialog (the saved state remembers it).
        if (savedState == nullptr)
            for (const auto& device : juce::MidiInput::getAvailableDevices())
                deviceManager.setMidiInputDeviceEnabled (device.identifier, true);

        watchMidiDevices();
    }
    return error;
}

// A keyboard plugged in while the program runs is switched on at once. Devices that were already there at start-up
// are left as the user set them.
void AudioEngine::watchMidiDevices()
{
    knownMidiInputs.clear();
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        knownMidiInputs.add (device.identifier);

    midiDeviceWatch = juce::MidiDeviceListConnection::make ([this]
    {
        for (const auto& device : juce::MidiInput::getAvailableDevices())
            if (! knownMidiInputs.contains (device.identifier))
            {
                knownMidiInputs.add (device.identifier);
                deviceManager.setMidiInputDeviceEnabled (device.identifier, true);
            }
    });
}

juce::String AudioEngine::describeMidiInputs() const
{
    juce::StringArray names;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (deviceManager.isMidiInputDeviceEnabled (device.identifier))
            names.add (device.name);
    return names.joinIntoString (", ");
}

void AudioEngine::handleMidi (const juce::MidiMessage& message)
{
    bool note = false;
    {
        const juce::SpinLock::ScopedLockType lock (producerLock);
        midiMapper.map (message, midiTarget.load (std::memory_order_relaxed), [this, &note] (LiveNoteEvent e)
                        {
                            e.fromMidi = true;
                            liveNotes.push (e);
                            note = note || e.on;
                        });
    }
    if (note)
        midiActivity.store (true, std::memory_order_relaxed);
}

int AudioEngine::getNumActiveInputChannels() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 0;
}

juce::String AudioEngine::enableDefaultInput()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return "No audio device is running.";
    if (getNumActiveInputChannels() > 0)
        return {};

    auto setup = deviceManager.getAudioDeviceSetup();
    const int available = device->getInputChannelNames().size();
    if (setup.inputDeviceName.isEmpty())
        setup.inputDeviceName = setup.outputDeviceName;

    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, std::max (1, std::min (2, available)), true);

    const auto error = deviceManager.setAudioDeviceSetup (setup, true);
    if (error.isNotEmpty() || getNumActiveInputChannels() == 0)
        return "No audio input could be opened (" + (error.isNotEmpty() ? error : juce::String ("the device has none")) + "). "
               "Connect a microphone or an audio interface and choose it in the Audio dialog.";
    return {};
}

juce::String AudioEngine::startRecording (const juce::File& file, InputRecorder::Channels channels)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return "No audio device is running.";
    if (recorder.isActive())
        return "Already recording.";

    // The sound a performer plays along with is heard `output latency` late and arrives `input latency` after
    // that: the first frames of the take belong to before the song started, so they are left out.
    const int skip = std::max (0, device->getInputLatencyInSamples()) + std::max (0, device->getOutputLatencyInSamples());

    // The song must already be running when the recorder arms: the audio callback checks the recorder first and
    // the transport second, so it can never record a block of a song that has not started.
    playing.store (true, std::memory_order_seq_cst);
    const auto error = recorder.start (file, device->getCurrentSampleRate(), channels, skip);
    if (error.isNotEmpty())
        playing.store (false, std::memory_order_seq_cst);
    return error;
}

InputRecorder::Take AudioEngine::stopRecording()
{
    return recorder.stop();
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
    outputLatencySamples.store (device != nullptr ? static_cast<double> (std::max (0, device->getOutputLatencyInSamples())) : 0.0,
                                std::memory_order_relaxed);
    masterMeanSquare = 0.0f;
    player.prepare (sampleRateHz);
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn,
                                                    float* const* out, int numOut,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    const juce::ScopedNoDenormals noDenormals; // reverb/delay tails decay through the denormal range
    const auto startTicks = juce::Time::getHighResolutionTicks();
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr)
            juce::FloatVectorOperations::clear (out[ch], numSamples);

    const Project* project = exchange.acquire();

    // Recording: the recorder is checked before the transport (see startRecording). The song restarts from the
    // beginning in the very block where the recording starts, so take and song line up.
    const bool recording = recorder.isActive();
    if (recording && ! captureStarted)
        player.rewind();
    captureStarted = recording;
    const bool isPlaying = playing.load (std::memory_order_seq_cst);

    if (numIn > 0 && in != nullptr)
    {
        float inLevel = 0.0f;
        for (int ch = 0; ch < numIn; ++ch)
            if (in[ch] != nullptr)
                for (int i = 0; i < numSamples; ++i)
                    inLevel = std::fmax (inLevel, std::fabs (in[ch][i]));
        raiseTo (inputPeak, inLevel);
    }
    if (recording)
        recorder.process (in, numIn, numSamples);

    const bool takeNotes = isPlaying && noteRecording.load (std::memory_order_relaxed);
    const double blockStartTick = player.getPositionTicks();
    const double currentBpm = bpm.load (std::memory_order_relaxed);
    // What a player pressed a key to: the song they heard, which is the output latency behind the song the player is
    // computing, and the key waited half a block on average before this callback saw it.
    const double latencySamples = outputLatencySamples.load (std::memory_order_relaxed) + 0.5 * numSamples;
    const double latencyTicks = latencySamples / sampleRateHz * currentBpm / 60.0 * kTicksPerBeat;

    LiveNoteEvent live;
    while (liveNotes.pop (live))
    {
        if (takeNotes && live.fromMidi)
            midiTakes.push (makeTake (live, blockStartTick, latencyTicks, project->lengthTicks));

        if (live.on)
            player.liveNoteOn (*project, live.channel, live.pitch, live.velocity);
        else
            player.liveNoteOff (live.channel, live.pitch);
    }

    player.setMetronome (metronomeOn.load (std::memory_order_relaxed), metronomeVolume.load (std::memory_order_relaxed));
    player.render (*project, currentBpm, isPlaying, out, numOut, numSamples);
    playheadTick.store (static_cast<int> (player.getPositionTicks()), std::memory_order_relaxed);

    const float g = gain.load (std::memory_order_relaxed);

    // Input monitoring: the input is added to the output (a mono input is heard on both sides).
    if (numIn > 0 && in != nullptr && in[0] != nullptr && monitorInput.load (std::memory_order_relaxed))
        for (int ch = 0; ch < numOut; ++ch)
            if (out[ch] != nullptr)
            {
                const float* source = (ch > 0 && numIn > 1 && in[1] != nullptr) ? in[1] : in[0];
                for (int i = 0; i < numSamples; ++i)
                    out[ch][i] += source[i] / (g > 0.05f ? g : 0.05f); // the master volume below scales it back
            }

    for (int ch = 0; ch < kMaxChannels; ++ch)
        raiseTo (channelPeaks[static_cast<size_t> (ch)], player.getChannelPeak (ch) * g);

    for (int t = 0; t < kMaxTracks; ++t)
    {
        raiseTo (trackPeaks[static_cast<size_t> (t)], player.getTrackPeak (t) * g);
        trackRms[static_cast<size_t> (t)].store (player.getTrackRms (t) * g, std::memory_order_relaxed);
    }
    for (int a = 0; a < kAuxBuses; ++a)
    {
        raiseTo (auxPeaks[static_cast<size_t> (a)], player.getAuxPeak (a) * g);
        auxRms[static_cast<size_t> (a)].store (player.getAuxRms (a) * g, std::memory_order_relaxed);
    }

    // The start-up sound is added after the master volume is applied (see playStartupSound), so it is as loud as the file.
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr)
            juce::FloatVectorOperations::multiply (out[ch], g, numSamples);
    if (startupTrigger.exchange (false, std::memory_order_acquire))
        startup.start();
    startup.mixInto (out, numOut, numSamples, sampleRateHz);

    float localPeak = 0.0f;
    double sumSquares = 0.0;
    int measuredChannels = 0;
    for (int ch = 0; ch < numOut; ++ch)
    {
        if (out[ch] == nullptr)
            continue;
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

    if (numSamples > 0 && sampleRateHz > 0.0)
    {
        const double used = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
        raiseTo (peakLoad, static_cast<float> (used * sampleRateHz / numSamples));

        const float stress = loadTest.load (std::memory_order_relaxed);
        if (stress > 0.0f)
        {
            const double target = stress * numSamples / sampleRateHz; // seconds the block should take in all
            while (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks) < target)
            {
            }
        }
    }
}
} // namespace sls
