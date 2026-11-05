#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/TapTempo.h"

namespace sls
{
// The little window that opens from a right-click on the Metronome button: the tempo (the same BPM as the toolbar),
// the click volume and a Tap button for setting the tempo by tapping along.
class MetronomePanel : public juce::Component
{
public:
    MetronomePanel (double bpm, float volume, std::function<void (double)> onBpm, std::function<void (float)> onVolume);

    void setBpm (double bpm);
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 260;
    static constexpr int kHeight = 150;

private:
    std::function<void (double)> onBpmChanged;
    std::function<void (float)> onVolumeChanged;
    juce::Label title { {}, "Metronome" };
    juce::Label bpmLabel { {}, "Tempo" };
    juce::Label volumeLabel { {}, "Volume" };
    juce::Slider bpmSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::TextButton tapButton { "Tap tempo" };
    TapTempo tapper;
};
} // namespace sls
