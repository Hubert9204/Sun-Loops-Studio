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
    captureGate.store (false);
    for (auto& r : recorders)
        r.stop();
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

void AudioEngine::serviceEffectMemory()
{
    // The player says how many effect slots wait for memory; it is made here (allocating is fine on this thread) for the rate the
    // device runs at, and handed over. A few per call keep the message thread responsive; the next call makes the rest.
    const double rate = deviceRate.load (std::memory_order_relaxed);
    if (rate <= 0.0)
        return;
    const int wanted = std::min (8, player.effectMemoryShortfall() - offeredMemory);
    for (int i = 0; i < wanted; ++i)
    {
        auto memory = std::make_unique<EffectMemory>();
        memory->prepare (rate);
        if (! player.offerEffectMemory (std::move (memory)))
            break;
        ++offeredMemory;
    }
    // Once the player's count has caught up with what was offered, nothing is in flight any more.
    const int count = player.effectMemoryCount();
    if (count < lastMemoryCount)
        offeredMemory = 0; // the pool was made again (a new device or rate): what was in flight was freed with it
    else if (count > lastMemoryCount)
        offeredMemory = std::max (0, offeredMemory - (count - lastMemoryCount));
    lastMemoryCount = count;
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

juce::StringArray AudioEngine::getInputChannelNames() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device != nullptr ? device->getInputChannelNames() : juce::StringArray();
}

juce::String AudioEngine::enableInputChannels (const std::vector<int>& channels)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return "No audio device is running.";

    const auto active = device->getActiveInputChannels();
    bool allOn = true;
    for (const int c : channels)
        allOn = allOn && c >= 0 && active[c];
    if (allOn && ! channels.empty())
        return {};

    auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.inputDeviceName.isEmpty())
        setup.inputDeviceName = setup.outputDeviceName;
    setup.useDefaultInputChannels = false;
    setup.inputChannels = active;
    for (const int c : channels)
        if (c >= 0)
            setup.inputChannels.setBit (c);
    if (channels.empty())
        setup.inputChannels.setBit (0);

    const auto error = deviceManager.setAudioDeviceSetup (setup, true);
    auto* now = deviceManager.getCurrentAudioDevice();
    if (error.isNotEmpty() || now == nullptr || now->getActiveInputChannels().countNumberOfSetBits() == 0)
        return "No audio input could be opened (" + (error.isNotEmpty() ? error : juce::String ("the device has none")) + "). "
               "Connect a microphone or an audio interface and choose it in the Audio dialog.";
    const auto on = now->getActiveInputChannels();
    for (const int c : channels)
        if (c < 0 || ! on[c])
            return "The device has no input " + juce::String (c + 1) + " (it has " + juce::String (now->getInputChannelNames().size())
                   + "). Choose another input for the track, or another device in the Audio dialog.";
    return {};
}

juce::String AudioEngine::startRecording (const std::vector<RecordStream>& streams, bool fromSongStart, int countInBeats)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return "No audio device is running.";
    if (captureGate.load())
        return "Already recording.";
    if (streams.empty() || streams.size() > static_cast<size_t> (kMaxRecordStreams))
        return "Nothing to record.";

    // The audio callback gets the switched-on inputs packed together: find each stream's place among them.
    const auto active = device->getActiveInputChannels();
    std::vector<bool> switchedOn (static_cast<size_t> (std::max (0, active.getHighestBit() + 1)));
    for (size_t c = 0; c < switchedOn.size(); ++c)
        switchedOn[c] = active[static_cast<int> (c)];

    // The sound a performer plays along with is heard `output latency` late and arrives `input latency` after
    // that: the first frames of the take belong to before the song started, so they are left out.
    const int skip = std::max (0, device->getInputLatencyInSamples()) + std::max (0, device->getOutputLatencyInSamples());
    const double rate = device->getCurrentSampleRate();

    for (size_t i = 0; i < streams.size(); ++i)
    {
        const auto& stream = streams[i];
        const int index = callbackInputIndex (switchedOn, stream.deviceInput);
        juce::String error = index < 0 ? "Input " + juce::String (stream.deviceInput + 1) + " is not switched on." : juce::String();
        if (error.isEmpty())
            error = recorders[i].start (stream.file, rate, stream.channels, skip, index);
        if (error.isNotEmpty())
        {
            for (size_t j = 0; j < i; ++j)
                recorders[j].stop().file.deleteFile();
            return error;
        }
    }

    // The song must already be running when the take starts: the audio callback checks the gate first and the transport
    // second, so it can never record a block of a song that has not started.
    captureFromStart.store (fromSongStart, std::memory_order_seq_cst);
    recordStartTick.store (-1.0, std::memory_order_seq_cst);
    if (countInBeats > 0)
    {
        // The audio thread counts in and then opens the gate itself (see audioDeviceIOCallbackWithContext).
        countInCancel.store (false, std::memory_order_seq_cst);
        countingIn.store (true, std::memory_order_seq_cst);
        playing.store (false, std::memory_order_seq_cst);
        countInRequest.store (std::min (countInBeats, 64), std::memory_order_seq_cst);
        return {};
    }
    playing.store (true, std::memory_order_seq_cst);
    captureGate.store (true, std::memory_order_seq_cst);
    return {};
}

std::vector<InputRecorder::Take> AudioEngine::stopRecording()
{
    // Close the gate first: no recorder gets a block after this one, so the takes end (almost) together.
    if (countingIn.load (std::memory_order_seq_cst))
    {
        countInRequest.store (0, std::memory_order_seq_cst);
        countInCancel.store (true, std::memory_order_seq_cst);
    }
    captureGate.store (false, std::memory_order_seq_cst);
    std::vector<InputRecorder::Take> takes;
    for (auto& r : recorders)
        if (auto take = r.stop(); take.file != juce::File())
            takes.push_back (std::move (take));
    return takes;
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
    loudness.prepare (sampleRateHz);
    outputLatencySamples.store (device != nullptr ? static_cast<double> (std::max (0, device->getOutputLatencyInSamples())) : 0.0,
                                std::memory_order_relaxed);
    masterMeanSquare = 0.0f;
    player.prepare (sampleRateHz);
    deviceRate.store (sampleRateHz, std::memory_order_relaxed);
    if (onDeviceStarting)
        onDeviceStarting (sampleRateHz);
}

void AudioEngine::setSuspended (bool shouldSuspend)
{
    suspended.store (shouldSuspend, std::memory_order_seq_cst);
    if (! shouldSuspend)
        return;
    for (int i = 0; i < 500 && inCallback.load (std::memory_order_seq_cst); ++i)
        juce::Thread::sleep (1);
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

    // Suspended (an export is using the plugins): silence, and the Player is not touched.
    inCallback.store (true, std::memory_order_seq_cst);
    if (suspended.load (std::memory_order_seq_cst))
    {
        audioClockSeconds += numSamples / sampleRateHz;
        inCallback.store (false, std::memory_order_seq_cst);
        return;
    }
    struct LeaveCallback
    {
        std::atomic<bool>& flag;
        ~LeaveCallback() { flag.store (false, std::memory_order_seq_cst); }
    } leave { inCallback };

    const Project* project = exchange.acquire();

    // Count-in (0.47.0): the clicks are placed so that the last beat ends exactly at the end of a block; the song and the takes
    // start with the next block, so the first beat of the take falls where the next click would have been.
    bool startTakeAfterThisBlock = false;
    if (countInCancel.exchange (false, std::memory_order_acq_rel))
    {
        countInLeft = 0;
        countingIn.store (false, std::memory_order_relaxed);
    }
    if (const int beats = countInRequest.exchange (0, std::memory_order_acq_rel); beats > 0 && numSamples > 0)
    {
        const int beatTicks = kTicksPerBeat * 4 / (project->beatUnit == 8 ? 8 : 4);
        const double perBeat = 60.0 * sampleRateHz / std::max (1.0, bpm.load (std::memory_order_relaxed)) * beatTicks / kTicksPerBeat;
        const double total = beats * perBeat;
        const auto blocks = static_cast<long long> (std::ceil (total / numSamples));
        countInLeft = blocks * numSamples;
        player.delayFreeClick (static_cast<double> (countInLeft) - total);
    }
    if (countInLeft > 0)
    {
        countInLeft -= numSamples;
        if (countInLeft <= 0)
        {
            countInLeft = 0;
            startTakeAfterThisBlock = true;
        }
    }

    // Recording: the recorder is checked before the transport (see startRecording). The song restarts from the
    // beginning in the very block where the recording starts, so take and song line up.
    const bool recording = captureGate.load (std::memory_order_seq_cst);
    if (recording && ! captureStarted)
    {
        // A seek asked for just before (Song mode switched on for the take) is applied first, so the take's start is known.
        const int pendingSeek = seekRequest.exchange (-1, std::memory_order_acq_rel);
        if (captureFromStart.load (std::memory_order_relaxed))
            player.rewind();
        else if (pendingSeek >= 0)
            player.seek (*project, pendingSeek, bpm.load (std::memory_order_relaxed));
        recordStartTick.store (player.getPositionTicks(), std::memory_order_release);
    }
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
        for (auto& r : recorders)
            r.process (in, numIn, numSamples); // wait-free; nothing for a recorder that is not part of this take

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
        if (live.fromMidi)
            scoreEvents.push ({ live.on, live.channel, live.pitch, live.velocity, audioClockSeconds - latencySamples / sampleRateHz });

        if (live.on)
            player.liveNoteOn (*project, live.channel, live.pitch, live.velocity);
        else
            player.liveNoteOff (live.channel, live.pitch);
    }

    if (const int seekTo = seekRequest.exchange (-1, std::memory_order_acq_rel); seekTo >= 0)
        player.seek (*project, seekTo, currentBpm);
    const bool countInNow = countingIn.load (std::memory_order_relaxed);
    player.setMetronome (metronomeOn.load (std::memory_order_relaxed) || countInNow,
                         countInNow ? std::max (0.5f, metronomeVolume.load (std::memory_order_relaxed)) : metronomeVolume.load (std::memory_order_relaxed));
    player.render (*project, currentBpm, isPlaying, out, numOut, numSamples);
    if (startTakeAfterThisBlock)
    {
        // Same order as startRecording: the transport, then the gate (the next block starts the take and the song together).
        playing.store (true, std::memory_order_seq_cst);
        captureGate.store (true, std::memory_order_seq_cst);
        countingIn.store (false, std::memory_order_seq_cst);
    }
    audioClockSeconds += numSamples / sampleRateHz;
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

    const int tracksNow = static_cast<int> (std::min (project->tracks.size(), static_cast<size_t> (kMaxTracks)));
    for (int t = 0; t < tracksNow; ++t)
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
    audition.mixInto (out, numOut, numSamples, sampleRateHz);

    if (numOut > 0 && out[0] != nullptr && numSamples > 0)
    {
        const float* right = numOut > 1 && out[1] != nullptr ? out[1] : out[0];
        loudness.process (out[0], right, numSamples);
        outputRing.push (out[0], right, numSamples);
    }

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
