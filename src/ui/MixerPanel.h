#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Project.h"
#include "ui/AudioMeter.h"

namespace sls
{
// One vertical mixer channel: name, pan, mute/solo, fader with meter and a dB read-out.
class MixerStrip : public juce::Component
{
public:
    MixerStrip (Channel& channelToControl, int channelIndex);

    void refreshFromChannel();
    void setSelected (bool shouldBeSelected);
    void setMeterLevel (float level) { meter.setLevel (level); }
    int getIndex() const noexcept { return index; }

    std::function<void (int)> onSelect;
    std::function<void (int)> onChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static juce::String volumeToText (float volume);

    static constexpr int kWidth = 76;

private:
    Channel& channel;
    int index;
    bool selected = false;

    juce::Label nameLabel;
    juce::Slider panSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label dbLabel;
    AudioMeter meter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerStrip)
};

// The Mixer panel body: all channels side by side, scrolling horizontally when the panel is narrow.
class MixerPanel : public juce::Viewport
{
public:
    MixerPanel();

    void setChannels (std::vector<Channel>& channels);

    void refreshStrip (int index);
    void setSelected (int index);
    void setMeterLevel (int index, float level);

    std::function<void (int)> onSelect;
    std::function<void (int)> onChanged;

    void resized() override;

private:
    struct Inner : juce::Component {};

    void layoutInner();

    Inner inner;
    std::vector<std::unique_ptr<MixerStrip>> strips;
};
} // namespace sls
