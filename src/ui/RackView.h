#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/ChannelStrip.h"
#include "ui/DragItems.h"

namespace sls
{
// The Channel Rack panel body: the channel strips stacked in a scrollable list.
// The strips are owned elsewhere; the rack only places them.
class RackView : public juce::Viewport, public juce::DragAndDropTarget
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
    std::function<void (const drag::Item&)> onItemDroppedOnEmptyArea; // new channel from a browser item

    // juce::DragAndDropTarget: only reached where no channel strip is under the mouse.
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        drag::Item item;
        return addButton.isVisible() && drag::Item::parse (d.description, item);
    }
    void itemDragEnter (const SourceDetails&) override { setHighlight (true); }
    void itemDragExit (const SourceDetails&) override { setHighlight (false); }
    void itemDropped (const SourceDetails& d) override
    {
        setHighlight (false);
        drag::Item item;
        if (drag::Item::parse (d.description, item) && onItemDroppedOnEmptyArea)
            onItemDroppedOnEmptyArea (item);
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (highlight)
        {
            g.setColour (juce::Colour (0xff4cc38a));
            g.drawRect (getLocalBounds(), 2);
        }
    }

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

    static constexpr int kRowHeight = 54;
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

    void setHighlight (bool on)
    {
        if (highlight != on)
        {
            highlight = on;
            repaint();
        }
    }

    bool highlight = false;
    Inner inner;
    juce::TextButton addButton { "+ Add channel" };
    std::vector<ChannelStrip*> strips;
};
} // namespace sls
