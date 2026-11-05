#pragma once

#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/AudioEngine.h"
#include "audio/Project.h"
#include "ui/AudioMeter.h"
#include "ui/ChannelStrip.h"
#include "ui/PianoRoll.h"

namespace sls
{
class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void publishProject();
    void selectChannel (int index);
    void setPlaying (bool shouldPlay);
    void openAudioSettings();
    void exportWav();
    void chooseSampleFor (int channelIndex);

    AudioEngine engine;
    juce::AudioFormatManager formats;

    // Source of truth for the UI. Never resized after construction, so the strips can hold references.
    std::vector<Channel> channels;
    std::vector<std::unique_ptr<ChannelStrip>> strips;
    int selected = 0;

    PianoRoll pianoRoll;

    juce::TextButton playButton { "Play" };
    juce::TextButton clearButton { "Clear notes" };
    juce::TextButton exportButton { "Export WAV..." };
    juce::TextButton deviceButton { "Audio settings..." };
    juce::Slider bpmSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label bpmLabel { {}, "BPM" };
    juce::Label volumeLabel { {}, "Master" };
    juce::Label deviceLabel;
    juce::Label statusLabel;
    AudioMeter meter;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace sls
