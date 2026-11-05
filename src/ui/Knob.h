#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace sls
{
// A small rotary control with a caption underneath. While the mouse is over it (or it is being dragged)
// the caption shows the current value instead. Double-click resets, the mouse wheel nudges.
class Knob : public juce::Component
{
public:
    Knob (juce::String captionText, double minimum, double maximum, double step, double resetValue,
          std::function<juce::String (double)> formatValue);
    ~Knob() override;

    void setValue (double v) { slider.setValue (v, juce::dontSendNotification); repaint(); }
    double getValue() const { return slider.getValue(); }
    void setTooltipText (const juce::String& text) { slider.setTooltip (text); }

    std::function<void (double)> onValueChange;

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider& getSlider() noexcept { return slider; }

    // Also called for the mouse events of the slider inside (it reports to this component), so the caption
    // can switch to the value while the knob is hovered or dragged.
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDrag (const juce::MouseEvent&) override { repaint(); }

private:

    juce::String caption;
    std::function<juce::String (double)> format;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
};
} // namespace sls
