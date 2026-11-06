// Two tiny plugins used only by the tests (never installed): they let the plugin host be tested with real VST3 and LV2 binaries.
//   SLS_TEST_SYNTH: an instrument; every held MIDI note plays a sine at its pitch with an amplitude of velocity * 0.25.
//   SLS_TEST_GAIN : an effect that multiplies its input by the "Gain" parameter (default 0.5) and saves that value in its state.
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cmath>

namespace
{
class TestSynth final : public juce::AudioProcessor
{
public:
    TestSynth() : juce::AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true)) {}

    const juce::String getName() const override { return "SLS Test Synth"; }
    void prepareToPlay (double sr, int) override { rate = sr; phases.fill (0.0); levels.fill (0.0f); }
    void releaseResources() override {}
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        int position = 0;
        const auto render = [&] (int until)
        {
            for (int i = position; i < until; ++i)
            {
                float s = 0.0f;
                for (int note = 0; note < 128; ++note)
                {
                    if (levels[static_cast<size_t> (note)] <= 0.0f)
                        continue;
                    auto& ph = phases[static_cast<size_t> (note)];
                    s += levels[static_cast<size_t> (note)] * static_cast<float> (std::sin (ph));
                    ph += juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (note) / rate;
                }
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.setSample (ch, i, s);
            }
            position = until;
        };
        for (const auto meta : midi)
        {
            render (std::min (meta.samplePosition, buffer.getNumSamples()));
            const auto m = meta.getMessage();
            if (m.isNoteOn())
                levels[static_cast<size_t> (m.getNoteNumber())] = m.getFloatVelocity() * 0.25f;
            else if (m.isNoteOff())
                levels[static_cast<size_t> (m.getNoteNumber())] = 0.0f;
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                levels.fill (0.0f);
        }
        render (buffer.getNumSamples());
    }

private:
    double rate = 48000.0;
    std::array<double, 128> phases {};
    std::array<float, 128> levels {};
};

class TestGain final : public juce::AudioProcessor
{
public:
    TestGain() : juce::AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true)
                                                          .withOutput ("Out", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (gain = new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Gain", 0.0f, 1.0f, 0.5f));
    }

    const juce::String getName() const override { return "SLS Test Gain"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& block) override { juce::MemoryOutputStream (block, false).writeFloat (gain->get()); }
    void setStateInformation (const void* data, int size) override
    {
        if (size >= 4)
            *gain = juce::MemoryInputStream (data, static_cast<size_t> (size), false).readFloat();
    }
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override { buffer.applyGain (gain->get()); }

private:
    juce::AudioParameterFloat* gain = nullptr;
};
} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
#if SLS_TEST_SYNTH
    return new TestSynth();
#else
    return new TestGain();
#endif
}
