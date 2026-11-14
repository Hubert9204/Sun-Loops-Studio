#include "sunsymphony/SunSymphonyProcessor.h"

namespace sunsymphony
{
namespace
{
constexpr const char* kTestTone = "test-tone";

// Reads one sample file to float (message thread). nullptr when it cannot be read.
std::shared_ptr<const orchestra::SampleBuffer> readSample (juce::AudioFormatManager& formats, const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > (1 << 26))
        return nullptr;
    const int frames = static_cast<int> (reader->lengthInSamples);
    const int channels = juce::jmin (2, static_cast<int> (reader->numChannels));
    juce::AudioBuffer<float> audio (channels, frames);
    if (! reader->read (&audio, 0, frames, 0, true, channels > 1))
        return nullptr;
    auto buffer = std::make_shared<orchestra::SampleBuffer>();
    buffer->sampleRate = reader->sampleRate;
    buffer->source = file.getFullPathName().toStdString();
    buffer->left.assign (audio.getReadPointer (0), audio.getReadPointer (0) + frames);
    if (channels > 1)
        buffer->right.assign (audio.getReadPointer (1), audio.getReadPointer (1) + frames);
    return buffer;
}
} // namespace

std::function<juce::AudioProcessorEditor* (SunSymphonyProcessor&)>& SunSymphonyProcessor::editorFactory()
{
    static std::function<juce::AudioProcessorEditor* (SunSymphonyProcessor&)> factory;
    return factory;
}

SunSymphonyProcessor::SunSymphonyProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();
    addParameter (master = new juce::AudioParameterFloat (juce::ParameterID { "master", 1 }, "Master",
                                                          juce::NormalisableRange<float> (0.0f, 1.5f), 0.8f,
                                                          juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                                              [] (float v, int) { return v > 1.0e-5f ? juce::String (20.0f * std::log10 (v), 1) + " dB" : juce::String ("-inf"); })));
    juce::String error;
    loadInstrument (0, kTestTone, error); // part 1 makes a sound out of the box
    startTimerHz (4);
}

SunSymphonyProcessor::~SunSymphonyProcessor()
{
    stopTimer();
}

juce::File SunSymphonyProcessor::defaultLibraryFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Sun Loops Studio").getChildFile ("Orchestral");
}

bool SunSymphonyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SunSymphonyProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    smoothedGain = master->get();
}

bool SunSymphonyProcessor::loadInstrument (int part, const juce::String& source, juce::String& error)
{
    if (part < 0 || part >= orchestra::kParts)
        return false;
    std::shared_ptr<const orchestra::Instrument> inst;
    if (source.isEmpty())
    {
        inst = nullptr; // an empty part
    }
    else if (source == kTestTone)
    {
        inst = orchestra::Instrument::makeTestTone();
    }
    else
    {
        const juce::File file (source);
        if (! file.existsAsFile())
        {
            error = "Not found: " + source;
            return false;
        }
        std::string message;
        auto& fmts = formats;
        const auto folder = file.getParentDirectory();
        inst = orchestra::Instrument::fromSfz (file.loadFileAsString().toStdString(), file.getFileNameWithoutExtension().toStdString(),
                                               file.getFileNameWithoutExtension().toStdString(),
                                               [&fmts, &folder] (const std::string& path) { return readSample (fmts, folder.getChildFile (juce::String (path))); },
                                               message);
        error = juce::String (message);
        if (inst == nullptr)
            return false;
    }
    engine.setInstrument (part, std::move (inst));
    sources[static_cast<size_t> (part)] = source;
    return true;
}

juce::String SunSymphonyProcessor::partName (int part) const
{
    const auto inst = engine.instrument (part);
    return inst != nullptr ? juce::String (inst->name) : juce::String ("(empty)");
}

void SunSymphonyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    // The on-screen keyboard's notes join the host's (on channel 1, part 1).
    keyboard.processNextMidiBuffer (midi, 0, numSamples, true);
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;
    int position = 0;
    for (const auto metadata : midi)
    {
        const int when = juce::jlimit (position, numSamples, metadata.samplePosition);
        if (when > position)
            engine.render (left + position, right + position, when - position);
        position = when;
        const auto m = metadata.getMessage();
        const int ch = m.getChannel() - 1;
        if (m.isNoteOn())
            engine.noteOn (ch, m.getNoteNumber(), m.getVelocity());
        else if (m.isNoteOff())
            engine.noteOff (ch, m.getNoteNumber());
        else if (m.isController())
            engine.controller (ch, m.getControllerNumber(), m.getControllerValue());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            engine.allNotesOff();
    }
    if (position < numSamples)
        engine.render (left + position, right + position, numSamples - position);
    if (right == left)
        return;
    // Master level, smoothed over the block.
    const float target = master->get();
    buffer.applyGainRamp (0, numSamples, smoothedGain, target);
    smoothedGain = target;
}

void SunSymphonyProcessor::getStateInformation (juce::MemoryBlock& data)
{
    juce::ValueTree state ("SunSymphony");
    state.setProperty ("version", 1, nullptr);
    state.setProperty ("master", master->get(), nullptr);
    for (int p = 0; p < orchestra::kParts; ++p)
    {
        juce::ValueTree part ("Part");
        part.setProperty ("index", p, nullptr);
        part.setProperty ("source", sources[static_cast<size_t> (p)], nullptr);
        state.appendChild (part, nullptr);
    }
    juce::MemoryOutputStream out (data, false);
    state.writeToStream (out);
}

void SunSymphonyProcessor::setStateInformation (const void* data, int size)
{
    const auto state = juce::ValueTree::readFromData (data, static_cast<size_t> (size));
    if (! state.hasType ("SunSymphony"))
        return;
    *master = static_cast<float> (state.getProperty ("master", 0.8));
    for (int p = 0; p < orchestra::kParts; ++p)
    {
        juce::String error;
        loadInstrument (p, {}, error);
    }
    for (const auto& part : state)
    {
        juce::String error;
        loadInstrument (part.getProperty ("index", -1), part.getProperty ("source").toString(), error); // a missing file leaves it empty
    }
}

juce::AudioProcessorEditor* SunSymphonyProcessor::createEditor()
{
    if (auto& factory = editorFactory())
        return factory (*this);
    return new juce::GenericAudioProcessorEditor (*this);
}
} // namespace sunsymphony
