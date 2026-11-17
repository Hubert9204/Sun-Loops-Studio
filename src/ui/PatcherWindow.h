#pragma once

#include <functional>
#include <memory>

#include "ui/MixerPanel.h"

namespace sls
{
// The Patcher's own window (0.73.0): an effects panel for the effects inside one Patcher slot, as if they were a mixer track's
// ten slots. What is changed there goes back into the Patcher slot through onEdited.
class PatcherWindow : public juce::DocumentWindow
{
public:
    explicit PatcherWindow (const juce::String& title);
    ~PatcherWindow() override;

    FxPanel& panel() noexcept { return *fx; }
    MixerTrack inner; // its slots are the Patcher's effects

    std::function<void()> onEdited;
    std::function<void()> onClose;
    void closeButtonPressed() override
    {
        if (onClose)
            onClose();
    }

private:
    std::unique_ptr<FxPanel> fx;
};
} // namespace sls
