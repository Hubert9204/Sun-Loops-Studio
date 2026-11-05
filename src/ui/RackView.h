#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/ChannelStrip.h"

namespace sls
{
// The Channel Rack panel body: the channel strips stacked in a scrollable list.
// The strips are owned elsewhere; the rack only places them.
class RackView : public juce::Viewport
{
public:
    RackView()
    {
        setViewedComponent (&inner, false);
        setScrollBarsShown (true, false);

        addButton.setMouseClickGrabsKeyboardFocus (false);
        addButton.onClick = [this] { if (onAddChannel) onAddChannel(); };
        inner.addAndMakeVisible (addButton);
    }

    // The "+" row below the strips; hidden when no more channels can be added.
    void setAddChannelEnabled (bool shouldShow)
    {
        addButton.setVisible (shouldShow);
        layoutInner();
    }

    std::function<void()> onAddChannel;

    void setStrips (std::vector<ChannelStrip*> newStrips)
    {
        for (auto* s : strips)
            inner.removeChildComponent (s);

        strips = std::move (newStrips);
        for (auto* s : strips)
            inner.addAndMakeVisible (s);

        layoutInner();
    }

    int getContentHeight() const noexcept
    {
        return static_cast<int> (strips.size()) * (kRowHeight + kGap) + (addButton.isVisible() ? kAddRowHeight : 0);
    }

    void resized() override
    {
        Viewport::resized();
        layoutInner();
    }

    static constexpr int kRowHeight = 52;
    static constexpr int kGap = 2;
    static constexpr int kAddRowHeight = 30;

private:
    struct Inner : juce::Component {};

    void layoutInner()
    {
        const int width = std::max (1, getMaximumVisibleWidth());
        inner.setSize (width, std::max (getContentHeight(), getMaximumVisibleHeight()));

        int y = 0;
        for (auto* s : strips)
        {
            s->setBounds (0, y, width, kRowHeight);
            y += kRowHeight + kGap;
        }
        addButton.setBounds (6, y + 2, 130, kAddRowHeight - 6);
    }

    Inner inner;
    juce::TextButton addButton { "+ Add channel" };
    std::vector<ChannelStrip*> strips;
};
} // namespace sls
