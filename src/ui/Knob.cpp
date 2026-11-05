#include "ui/Knob.h"

namespace sls
{
Knob::Knob (juce::String captionText, double minimum, double maximum, double step, double resetValue,
            std::function<juce::String (double)> formatValue)
    : caption (std::move (captionText)), format (std::move (formatValue))
{
    slider.setRange (minimum, maximum, step);
    slider.setDoubleClickReturnValue (true, resetValue);
    slider.setValue (resetValue, juce::dontSendNotification);
    slider.setMouseDragSensitivity (140);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffe8943a));
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff1b1d22));
    slider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffeeeeee));
    slider.onValueChange = [this]
    {
        repaint();
        if (onValueChange)
            onValueChange (slider.getValue());
    };
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
}

Knob::~Knob()
{
    slider.removeMouseListener (this);
}

void Knob::resized()
{
    slider.setBounds (getLocalBounds()); // the caption is drawn in the open gap at the bottom of the arc
}

void Knob::paint (juce::Graphics& g)
{
    auto text = getLocalBounds().removeFromBottom (10);
    const bool showValue = slider.isMouseOverOrDragging() && format != nullptr;

    g.setFont (juce::FontOptions (showValue ? 10.0f : 9.0f, showValue ? juce::Font::bold : juce::Font::plain));
    g.setColour (showValue ? juce::Colour (0xffe8943a) : juce::Colour (0xff9aa0ab));
    g.drawText (showValue ? format (slider.getValue()) : caption, text, juce::Justification::centred, false);
}
} // namespace sls
