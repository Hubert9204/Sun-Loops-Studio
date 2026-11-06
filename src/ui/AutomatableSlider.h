#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace sls
{
// A slider with one extra: a right click offers "Create automation clip", which hands the request to `onAutomate`
// (the owner knows which parameter the slider stands for). Everything else is juce::Slider.
class AutomatableSlider : public juce::Slider
{
public:
    using juce::Slider::Slider;

    std::function<void()> onAutomate;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onAutomate && e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Create automation clip");
            menu.showMenuAsync (juce::PopupMenu::Options(),
                                [safe = juce::Component::SafePointer<AutomatableSlider> (this)] (int result)
                                {
                                    if (result == 1 && safe != nullptr && safe->onAutomate)
                                        safe->onAutomate();
                                });
            return;
        }
        juce::Slider::mouseDown (e);
    }
};
} // namespace sls
