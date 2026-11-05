#include "ui/MetronomePanel.h"

namespace sls
{
MetronomePanel::MetronomePanel (double bpm, float volume, std::function<void (double)> onBpm, std::function<void (float)> onVolume)
    : onBpmChanged (std::move (onBpm)), onVolumeChanged (std::move (onVolume))
{
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    for (auto* l : { &title, &bpmLabel, &volumeLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colour (0xffe6e8ec));
        addAndMakeVisible (l);
    }

    bpmSlider.setRange (TapTempo::kMinBpm, TapTempo::kMaxBpm, 1.0);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22);
    bpmSlider.setValue (bpm, juce::dontSendNotification);
    bpmSlider.onValueChange = [this] { if (onBpmChanged) onBpmChanged (bpmSlider.getValue()); };

    volumeSlider.setRange (0.0, 1.0, 0.01);
    volumeSlider.setValue (volume, juce::dontSendNotification);
    volumeSlider.onValueChange = [this] { if (onVolumeChanged) onVolumeChanged (static_cast<float> (volumeSlider.getValue())); };

    tapButton.setMouseClickGrabsKeyboardFocus (false);
    tapButton.onClick = [this]
    {
        const double result = tapper.tap (juce::Time::getMillisecondCounterHiRes() / 1000.0);
        if (result > 0.0)
            bpmSlider.setValue (std::round (result), juce::sendNotificationSync);
    };

    addAndMakeVisible (bpmSlider);
    addAndMakeVisible (volumeSlider);
    addAndMakeVisible (tapButton);
    setSize (kWidth, kHeight);
}

void MetronomePanel::setBpm (double bpm)
{
    bpmSlider.setValue (bpm, juce::dontSendNotification);
}

void MetronomePanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2d34));
}

void MetronomePanel::resized()
{
    auto area = getLocalBounds().reduced (14, 12);
    title.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);

    auto row = area.removeFromTop (26);
    bpmLabel.setBounds (row.removeFromLeft (56));
    bpmSlider.setBounds (row);
    area.removeFromTop (6);

    row = area.removeFromTop (26);
    volumeLabel.setBounds (row.removeFromLeft (56));
    volumeSlider.setBounds (row);
    area.removeFromTop (10);

    tapButton.setBounds (area.removeFromTop (30));
}
} // namespace sls
