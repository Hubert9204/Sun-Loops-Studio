#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace sls
{
// A button whose right click runs `onRightClick` instead of pressing it.
class RightClickButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;

    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onRightClick)
                onRightClick();
            return; // no press, no click
        }
        juce::TextButton::mouseDown (e);
    }
};
} // namespace sls
