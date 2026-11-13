#include "ui/PerformancePanel.h"

namespace sls
{
PerformancePanel::PerformancePanel()
{
    onButton.setClickingTogglesState (false);
    onButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f7d5b));
    onButton.setTooltip ("On: the clips below are launched by hand and loop; the Playlist's own arrangement does not play. Off: back to the song.");
    onButton.onClick = [this] { if (onToggle) onToggle (! model.on); };
    stopAllButton.setTooltip ("Stop every track at the next boundary");
    stopAllButton.onClick = [this] { if (onStopAll) onStopAll(); };
    quantiseBox.addItem ("the next bar", 1);
    quantiseBox.addItem ("every 2 bars", 2);
    quantiseBox.addItem ("every 4 bars", 3);
    quantiseBox.addItem ("the next beat", 4);
    quantiseBox.addItem ("the next 16th", 5);
    quantiseBox.setSelectedId (1, juce::dontSendNotification);
    quantiseBox.setTooltip ("When a launched clip starts (and the one it replaces stops)");
    quantiseBox.onChange = [this] { if (onQuantise) onQuantise (quantiseBox.getSelectedId()); };
    quantiseLabel.setFont (juce::FontOptions (12.0f));
    quantiseLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb9bfc9));
    for (auto* c : std::initializer_list<juce::Component*> { &onButton, &stopAllButton, &quantiseBox, &quantiseLabel })
        addAndMakeVisible (c);
    startTimerHz (4);
}

void PerformancePanel::setModel (PerformanceModel newModel)
{
    model = std::move (newModel);
    onButton.setToggleState (model.on, juce::dontSendNotification);
    quantiseBox.setSelectedId (model.quantiseId, juce::dontSendNotification);
    repaint();
}

void PerformancePanel::timerCallback()
{
    blink = ! blink;
    for (const auto& row : model.rows)
        if (row.queued >= 0 || row.stopQueued)
        {
            repaint();
            return;
        }
}

int PerformancePanel::columns() const noexcept
{
    size_t most = 0;
    for (const auto& row : model.rows)
        most = std::max (most, row.cells.size());
    return static_cast<int> (most);
}

juce::Rectangle<int> PerformancePanel::cellRect (int row, int column) const noexcept
{
    return { kNameWidth + kStopWidth + column * kCellWidth, kHeader + kSceneRow + row * kRowHeight, kCellWidth, kRowHeight };
}

juce::Rectangle<int> PerformancePanel::stopRect (int row) const noexcept
{
    return { kNameWidth, kHeader + kSceneRow + row * kRowHeight, kStopWidth, kRowHeight };
}

void PerformancePanel::resized()
{
    auto bar = getLocalBounds().removeFromTop (kHeader).reduced (8, 5);
    onButton.setBounds (bar.removeFromLeft (150));
    bar.removeFromLeft (12);
    quantiseLabel.setBounds (bar.removeFromLeft (66));
    quantiseBox.setBounds (bar.removeFromLeft (130));
    bar.removeFromLeft (12);
    stopAllButton.setBounds (bar.removeFromLeft (80));
}

void PerformancePanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1e24));
    g.setColour (juce::Colour (0xff23272e));
    g.fillRect (0, 0, getWidth(), kHeader);

    if (model.rows.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("Nothing to launch yet: put pattern clips on the Playlist's tracks. Each track becomes a row here, each of its clips a cell.",
                          getLocalBounds().withTrimmedTop (kHeader).reduced (30), juce::Justification::centred, 3);
        return;
    }

    // the scene numbers
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (int c = 0; c < columns(); ++c)
    {
        const auto box = juce::Rectangle<int> (kNameWidth + kStopWidth + c * kCellWidth, kHeader, kCellWidth, kSceneRow).reduced (2, 3);
        g.setColour (juce::Colour (0xff2e323b));
        g.fillRoundedRectangle (box.toFloat(), 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb6 ") + "Scene " + juce::String (c + 1), box, juce::Justification::centred);
    }

    for (int r = 0; r < static_cast<int> (model.rows.size()); ++r)
    {
        const auto& row = model.rows[static_cast<size_t> (r)];
        const int y = kHeader + kSceneRow + r * kRowHeight;
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine (y + kRowHeight - 1, 0.0f, static_cast<float> (getWidth()));
        g.setColour (juce::Colour (0xffd0d4dc));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (row.name, 8, y, kNameWidth - 12, kRowHeight, juce::Justification::centredLeft, true);

        // the stop square: lit while a stop waits for the boundary
        const auto stop = stopRect (r).reduced (7, 8).toFloat();
        g.setColour (row.stopQueued && blink ? juce::Colour (0xffd9534f) : juce::Colour (0xff2e323b));
        g.fillRoundedRectangle (stop, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (row.playing >= 0 ? 0.85f : 0.35f));
        g.fillRect (stop.reduced (6.0f, 6.0f));

        for (int c = 0; c < static_cast<int> (row.cells.size()); ++c)
        {
            const auto& cell = row.cells[static_cast<size_t> (c)];
            const auto box = cellRect (r, c).reduced (2, 3).toFloat();
            const bool playing = cell.clip == row.playing, queued = cell.clip == row.queued;
            g.setColour (cell.colour.withAlpha (playing ? 1.0f : 0.38f));
            g.fillRoundedRectangle (box, 4.0f);
            if (queued)
            {
                g.setColour (juce::Colours::white.withAlpha (blink ? 0.95f : 0.3f));
                g.drawRoundedRectangle (box.reduced (1.0f), 4.0f, 2.0f);
            }
            else if (playing)
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.5f);
            }
            g.setColour (playing ? juce::Colours::black : juce::Colours::white.withAlpha (0.9f));
            g.setFont (juce::FontOptions (12.0f, playing ? juce::Font::bold : juce::Font::plain));
            g.drawText ((playing ? juce::String::fromUTF8 ("\xe2\x96\xb6 ") : juce::String()) + cell.name, box.reduced (6.0f, 0.0f), juce::Justification::centredLeft, true);
        }
    }
}

void PerformancePanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.y >= kHeader && e.y < kHeader + kSceneRow && e.x >= kNameWidth + kStopWidth)
    {
        const int column = (e.x - kNameWidth - kStopWidth) / kCellWidth;
        if (column < columns() && onScene)
            onScene (column);
        return;
    }
    const int r = (e.y - kHeader - kSceneRow) / kRowHeight;
    if (e.y < kHeader + kSceneRow || r < 0 || r >= static_cast<int> (model.rows.size()))
        return;
    const auto& row = model.rows[static_cast<size_t> (r)];
    if (stopRect (r).contains (e.getPosition()))
    {
        if (onStopTrack)
            onStopTrack (row.track);
        return;
    }
    const int column = (e.x - kNameWidth - kStopWidth) / kCellWidth;
    if (e.x >= kNameWidth + kStopWidth && column >= 0 && column < static_cast<int> (row.cells.size()) && onLaunch)
        onLaunch (row.cells[static_cast<size_t> (column)].clip);
}

PerformanceWindow::PerformanceWindow()
    : juce::DocumentWindow ("Performance mode", juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (false);
    setContentNonOwned (&content, false);
    setResizable (true, true);
    setResizeLimits (520, 200, 2400, 1400);
    centreWithSize (860, 380);
    setAlwaysOnTop (true);
}
} // namespace sls
