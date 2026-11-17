#include "ui/PatcherWindow.h"

namespace sls
{
PatcherWindow::PatcherWindow (const juce::String& title)
    : juce::DocumentWindow (title, juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton)
{
    fx = std::make_unique<FxPanel>();
    fx->setNested (true);
    fx->setTrack (&inner, 0);
    fx->onChanged = [this] (int) { if (onEdited) onEdited(); };
    fx->onSlotsChanged = [this] (int) { if (onEdited) onEdited(); };
    setUsingNativeTitleBar (false);
    setContentNonOwned (fx.get(), false);
    setResizable (true, true);
    setResizeLimits (300, 360, 900, 1200);
    centreWithSize (400, 620);
    setAlwaysOnTop (true);
}

PatcherWindow::~PatcherWindow()
{
    clearContentComponent();
}
} // namespace sls
