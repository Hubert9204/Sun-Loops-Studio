#pragma once

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

    int getContentHeight() const noexcept { return static_cast<int> (strips.size()) * (kRowHeight + kGap); }

    void resized() override
    {
        Viewport::resized();
        layoutInner();
    }

    static constexpr int kRowHeight = 52;
    static constexpr int kGap = 2;

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
    }

    Inner inner;
    std::vector<ChannelStrip*> strips;
};
} // namespace sls
