#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/ChannelStrip.h"
#include "ui/StepGraph.h"
#include "ui/DragItems.h"
#include "audio/Arrangement.h"

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

// The Channel Rack panel (0.42.0): a bar with the pattern length and its - / + buttons (one quarter note at a time, at least the
// 4 quarters of one bar of 4/4), and the rack below it. The bar stays in place while the channels scroll.
class RackBody : public juce::Component
{
public:
    explicit RackBody (RackView& rackToShow) : rack (rackToShow)
    {
        addAndMakeVisible (rack);
        lengthLabel.setFont (juce::FontOptions (12.0f));
        lengthLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd0d4dc));
        lengthLabel.setTooltip ("The pattern's length: the step grid shows it all, 4 steps per quarter note");
        addAndMakeVisible (lengthLabel);
        for (auto* b : { &shorterButton, &longerButton })
        {
            b->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }
        shorterButton.setTooltip ("One quarter note shorter (not below one bar of 4/4)");
        longerButton.setTooltip ("One quarter note longer");
        shorterButton.onClick = [this] { if (onPatternBeats) onPatternBeats (beats - 1); };
        longerButton.onClick = [this] { if (onPatternBeats) onPatternBeats (beats + 1); };
        // 0.60.0: the channel options menu at the left of the bar, as in FL Studio's Channel Rack (add, clone, rename, move, delete...).
        optionsButton.setMouseClickGrabsKeyboardFocus (false);
        optionsButton.setTooltip ("Channel options: add, clone, rename, move or delete channels, assign mixer tracks");
        optionsButton.onClick = [this] { if (onOptionsMenu) onOptionsMenu (optionsButton); };
        addAndMakeVisible (optionsButton);
        // 0.62.0: the filter group shown (as the group selector at the top of FL Studio's rack) and the graph editor's switch
        groupBox.setTooltip ("Channel filter: show every channel, or only those of one group (a channel's group is in its menu)");
        groupBox.onChange = [this] { if (onGroupChosen) onGroupChosen (groupBox.getSelectedId() <= 1 ? juce::String() : groupBox.getText()); };
        addAndMakeVisible (groupBox);
        graphButton.setClickingTogglesState (true);
        graphButton.setMouseClickGrabsKeyboardFocus (false);
        graphButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f7d5b));
        graphButton.setTooltip ("Graph editor: velocity, pan, release and fine pitch of each step of the selected channel");
        graphButton.onClick = [this]
        {
            graph.setVisible (graphButton.getToggleState());
            resized();
        };
        addAndMakeVisible (graphButton);
        addChildComponent (graph);
        setGroups ({}, {});
        setPatternBeats (4);
    }

    // 0.62.0
    std::function<void (const juce::String&)> onGroupChosen; // "" = all the channels
    void setGroups (const juce::StringArray& groups, const juce::String& current)
    {
        groupBox.clear (juce::dontSendNotification);
        groupBox.addItem ("All", 1);
        for (int i = 0; i < groups.size(); ++i)
            groupBox.addItem (groups[i], i + 2);
        const int found = groups.indexOf (current);
        groupBox.setSelectedId (current.isEmpty() || found < 0 ? 1 : found + 2, juce::dontSendNotification);
    }
    StepGraph& stepGraph() noexcept { return graph; }
    bool graphShown() const noexcept { return graph.isVisible(); }

    std::function<void (juce::Component&)> onOptionsMenu; // 0.60.0: show the channel options menu under this button

    // `minBeats` and `meter` (0.46.0): the shortest pattern, and the time signature the bars are counted in.
    void setPatternBeats (int newBeats, int minBeats = kMinPatternBeats, TimeSignature meter = {})
    {
        beats = newBeats;
        const int perBar = std::max (1, meter.barQuarters());
        const int bars = beats / perBar, rest = beats % perBar;
        lengthLabel.setText ("Pattern: " + juce::String (beats) + " quarters (" + juce::String (bars) + (bars == 1 ? " bar" : " bars")
                                 + (rest > 0 ? " + " + juce::String (rest) + "/4" : juce::String()) + " of " + juce::String (meter.text())
                                 + ") - " + juce::String (beats * 4) + " steps",
                             juce::dontSendNotification);
        shorterButton.setEnabled (beats > minBeats);
        longerButton.setEnabled (beats < kMaxPatternBeats);
    }

    std::function<void (int)> onPatternBeats; // the length asked for, in quarter notes

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff23272e));
        g.fillRect (getLocalBounds().removeFromTop (kBarHeight));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop (kBarHeight).reduced (6, 3);
        optionsButton.setBounds (bar.removeFromLeft (30));
        bar.removeFromLeft (6);
        groupBox.setBounds (bar.removeFromLeft (120));
        bar.removeFromLeft (6);
        graphButton.setBounds (bar.removeFromLeft (58));
        bar.removeFromLeft (8);
        if (graph.isVisible()) // (as wide as the rows above it, which a scroll bar may have narrowed)
            graph.setBounds (area.removeFromBottom (StepGraph::kHeight).withWidth (std::max (1, rack.getMaximumVisibleWidth())));
        longerButton.setBounds (bar.removeFromRight (28));
        bar.removeFromRight (4);
        shorterButton.setBounds (bar.removeFromRight (28));
        bar.removeFromRight (8);
        lengthLabel.setBounds (bar);
        rack.setBounds (area);
    }

    static constexpr int kBarHeight = 28;

private:
    RackView& rack;
    int beats = 4;
    juce::Label lengthLabel;
    juce::TextButton shorterButton { "-" }, longerButton { "+" };
    juce::TextButton optionsButton { juce::String::fromUTF8 ("\xe2\x89\xa1") }; // "≡" (0.60.0)
    juce::ComboBox groupBox;                   // 0.62.0
    juce::TextButton graphButton { "Graph" };
    StepGraph graph;
};
} // namespace sls
