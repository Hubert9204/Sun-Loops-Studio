#include "ui/MixerPanel.h"

#include <cmath>

namespace sls
{
namespace
{
const juce::Colour kStripBg (0xff343841);
const juce::Colour kStripSelectedBg (0xff434a58);
const juce::Colour kAccent (0xffe8943a);
} // namespace

// ------------------------------------------------------------------------- MixerStrip

juce::String MixerStrip::volumeToText (float volume)
{
    if (volume <= 0.0001f)
        return "-inf dB";
    return juce::String (20.0f * std::log10 (volume), 1) + " dB";
}

MixerStrip::MixerStrip (Channel& channelToControl, int channelIndex)
    : channel (channelToControl), index (channelIndex)
{
    nameLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setInterceptsMouseClicks (false, false);

    dbLabel.setFont (juce::FontOptions (11.0f));
    dbLabel.setJustificationType (juce::Justification::centred);
    dbLabel.setInterceptsMouseClicks (false, false);

    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.setDoubleClickReturnValue (true, 0.0);
    panSlider.setTooltip ("Pan");
    panSlider.onValueChange = [this]
    {
        channel.pan = static_cast<float> (panSlider.getValue());
        if (onChanged) onChanged (index);
    };

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9534f));
    muteButton.onClick = [this]
    {
        channel.mute = muteButton.getToggleState();
        if (onChanged) onChanged (index);
    };

    soloButton.setClickingTogglesState (true);
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0b030));
    soloButton.onClick = [this]
    {
        channel.solo = soloButton.getToggleState();
        if (onChanged) onChanged (index);
    };

    fader.setRange (0.0, 1.5, 0.01);
    fader.setDoubleClickReturnValue (true, 0.8);
    fader.setTooltip ("Volume");
    fader.onValueChange = [this]
    {
        channel.volume = static_cast<float> (fader.getValue());
        dbLabel.setText (volumeToText (channel.volume), juce::dontSendNotification);
        if (onChanged) onChanged (index);
    };

    for (auto* c : std::initializer_list<juce::Component*> { &nameLabel, &panSlider, &muteButton, &soloButton,
                                                              &fader, &dbLabel, &meter })
        addAndMakeVisible (c);

    refreshFromChannel();
}

void MixerStrip::refreshFromChannel()
{
    nameLabel.setText (juce::String (channel.name), juce::dontSendNotification);
    panSlider.setValue (channel.pan, juce::dontSendNotification);
    muteButton.setToggleState (channel.mute, juce::dontSendNotification);
    soloButton.setToggleState (channel.solo, juce::dontSendNotification);
    fader.setValue (channel.volume, juce::dontSendNotification);
    dbLabel.setText (volumeToText (channel.volume), juce::dontSendNotification);
}

void MixerStrip::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void MixerStrip::paint (juce::Graphics& g)
{
    g.setColour (selected ? kStripSelectedBg : kStripBg);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);

    if (selected)
    {
        g.setColour (kAccent);
        g.fillRoundedRectangle (4.0f, 1.0f, static_cast<float> (getWidth()) - 8.0f, 3.0f, 1.5f);
    }
}

void MixerStrip::resized()
{
    auto area = getLocalBounds().reduced (6, 6);

    nameLabel.setBounds (area.removeFromTop (20));
    panSlider.setBounds (area.removeFromTop (18));
    area.removeFromTop (4);

    auto buttons = area.removeFromTop (24);
    muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
    soloButton.setBounds (buttons.reduced (2, 0));
    area.removeFromTop (6);

    dbLabel.setBounds (area.removeFromBottom (18));
    area.removeFromBottom (4);

    auto faderArea = area.withSizeKeepingCentre (std::min (area.getWidth(), 46), area.getHeight());
    meter.setBounds (faderArea.removeFromRight (12));
    faderArea.removeFromRight (4);
    fader.setBounds (faderArea);
}

void MixerStrip::mouseDown (const juce::MouseEvent&)
{
    if (onSelect)
        onSelect (index);
}

// ------------------------------------------------------------------------- MixerPanel

MixerPanel::MixerPanel()
{
    setViewedComponent (&inner, false);
    setScrollBarsShown (false, true);
}

void MixerPanel::setChannels (std::vector<Channel>& channels)
{
    strips.clear();

    for (size_t i = 0; i < channels.size(); ++i)
    {
        auto strip = std::make_unique<MixerStrip> (channels[i], static_cast<int> (i));
        strip->onSelect = [this] (int idx) { if (onSelect) onSelect (idx); };
        strip->onChanged = [this] (int idx) { if (onChanged) onChanged (idx); };
        inner.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }
    layoutInner();
}

void MixerPanel::refreshStrip (int index)
{
    if (index >= 0 && index < static_cast<int> (strips.size()))
        strips[static_cast<size_t> (index)]->refreshFromChannel();
}

void MixerPanel::setSelected (int index)
{
    for (auto& s : strips)
        s->setSelected (s->getIndex() == index);
}

void MixerPanel::setMeterLevel (int index, float level)
{
    if (index >= 0 && index < static_cast<int> (strips.size()))
        strips[static_cast<size_t> (index)]->setMeterLevel (level);
}

void MixerPanel::resized()
{
    Viewport::resized();
    layoutInner();
}

void MixerPanel::layoutInner()
{
    const int needed = static_cast<int> (strips.size()) * MixerStrip::kWidth;
    const int height = std::max (1, getMaximumVisibleHeight());
    inner.setSize (std::max (needed, getMaximumVisibleWidth()), height);

    int x = 0;
    for (auto& s : strips)
    {
        s->setBounds (x, 0, MixerStrip::kWidth, height);
        x += MixerStrip::kWidth;
    }
}
} // namespace sls
