#include "ui/PlaylistView.h"

#include "audio/TimeFormat.h"

namespace sls
{
namespace
{
const juce::Colour kAccent { 0xffe8943a };
const juce::Colour kDim { 0xff8a919d };
} // namespace

PlaylistView::PlaylistView (Arrangement& arrangementToEdit) : timeline (arrangementToEdit)
{
    patternLabel.setFont (juce::FontOptions (12.0f));
    patternBox.setTooltip ("The pattern that a click on the playlist places. Choosing one also opens it in the piano roll.");
    patternBox.setWantsKeyboardFocus (false);
    patternBox.onChange = [this]
    {
        if (patternBox.getSelectedId() > 0 && onPatternSelected)
            onPatternSelected (patternBox.getSelectedId() - 1);
    };

    newButton.setTooltip ("Add a new empty pattern");
    newButton.onClick = [this] { if (onNewPattern) onNewPattern(); };
    renameButton.setTooltip ("Rename the selected pattern");
    renameButton.onClick = [this] { if (onRenamePattern) onRenamePattern (patternBox.getSelectedId() - 1); };
    deleteButton.setTooltip ("Delete the selected pattern, its notes and the playlist clips that play it (the last pattern cannot be deleted)");
    deleteButton.onClick = [this] { if (onDeletePattern) onDeletePattern (patternBox.getSelectedId() - 1); };

    undoButton.setTooltip ("Undo the last change to the playlist (Ctrl+Z)");
    undoButton.onClick = [this] { timeline.undo(); updateUndoButtons(); };
    redoButton.setTooltip ("Redo (Ctrl+Y)");
    redoButton.onClick = [this] { timeline.redo(); updateUndoButtons(); };

    const struct { juce::TextButton* button; PlaylistTool tool; const char* tip; } tools[] = {
        { &selectTool, PlaylistTool::Select, "Select and move clips; drag an edge to trim; click empty space to place a pattern (1)" },
        { &splitTool,  PlaylistTool::Split,  "Split: click a clip to cut it in two at that point (2). The grid decides where." },
        { &cutTool,    PlaylistTool::Cut,    "Cut: drag across a clip to remove that stretch of it (3)" },
        { &eraseTool,  PlaylistTool::Erase,  "Erase: click a clip to delete it, or drag across several (4)" },
        { &zoomTool,   PlaylistTool::Zoom,   "Zoom: click to zoom in, right click to zoom out, drag to zoom to a stretch (5)" } };
    for (const auto& t : tools)
    {
        auto* button = t.button;
        const auto tool = t.tool;
        button->setClickingTogglesState (true);
        button->setRadioGroupId (4711);
        button->setColour (juce::TextButton::buttonOnColourId, kAccent);
        button->setTooltip (t.tip);
        button->onClick = [this, tool] { timeline.setTool (tool); };
        addAndMakeVisible (button);
    }
    timeline.onToolChanged = [this] (PlaylistTool) { syncToolButtons(); };
    selectTool.setToggleState (true, juce::dontSendNotification);

    struct SnapOption { const char* label; int ticks; };
    static constexpr SnapOption options[] = { { "Bar", kTicksPerBar }, { "1/2", kTicksPerBar / 2 }, { "Beat (1/4)", kTicksPerBeat }, { "1/8", kTicksPerBeat / 2 },
                                              { "1/16", kTicksPerBeat / 4 }, { "1/32", kTicksPerBeat / 8 }, { "1/64", kTicksPerBeat / 16 },
                                              { "Off (1 tick)", 1 } };
    for (const auto& o : options)
    {
        snapValues.push_back (o.ticks);
        snapBox.addItem (o.label, static_cast<int> (snapValues.size()));
    }
    snapBox.setSelectedId (3, juce::dontSendNotification); // Beat
    snapBox.setTooltip ("The grid that audio clips snap to when moved, trimmed, split or cut. Hold Alt while dragging to ignore it.");
    snapBox.setWantsKeyboardFocus (false);
    snapBox.onChange = [this]
    {
        const int index = snapBox.getSelectedId() - 1;
        if (index >= 0 && index < static_cast<int> (snapValues.size()))
            timeline.setSnapTicks (snapValues[static_cast<size_t> (index)]);
    };
    snapLabel.setFont (juce::FontOptions (12.0f));

    zoomOutButton.setTooltip ("Zoom out (-)");
    zoomOutButton.onClick = [this] { timeline.zoomBy (1.0 / 1.5); };
    zoomInButton.setTooltip ("Zoom in (+). The mouse wheel zooms in and out around the mouse; Shift+wheel scrolls.");
    zoomInButton.onClick = [this] { timeline.zoomBy (1.5); };
    zoomFitButton.setTooltip ("Show the whole song");
    zoomFitButton.onClick = [this] { timeline.zoomToFit(); };

    positionLabel.setFont (juce::FontOptions (12.0f));
    positionLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd5d9e0));
    positionLabel.setInterceptsMouseClicks (false, false);
    selectionLabel.setFont (juce::FontOptions (12.0f));
    selectionLabel.setColour (juce::Label::textColourId, kDim);
    selectionLabel.setInterceptsMouseClicks (false, false);
    selectionLabel.setMinimumHorizontalScale (0.8f);

    timeline.onReadout = [this] (const juce::String& position, const juce::String& selection)
    {
        positionLabel.setText (position, juce::dontSendNotification);
        selectionLabel.setText (selection.isEmpty() ? juce::String ("No clip selected") : selection, juce::dontSendNotification);
        updateUndoButtons();
    };
    selectionLabel.setText ("No clip selected", juce::dontSendNotification);

    timeline.onChanged = [this] { updateUndoButtons(); if (onChanged) onChanged(); };
    timeline.onPatternPicked = [this] (int p) { if (onPatternPicked) onPatternPicked (p); };
    timeline.onFilesDropped = [this] (const juce::StringArray& f, int track, int tick) { if (onFilesDropped) onFilesDropped (f, track, tick); };
    timeline.onItemDropped = [this] (const drag::Item& item, int track, int tick) { if (onItemDropped) onItemDropped (item, track, tick); };
    timeline.onTrackSelected = [this] (int track) { if (onTrackSelected) onTrackSelected (track); };
    timeline.onEditSample = [this] (int index) { if (onEditSample) onEditSample (index); };
    timeline.onSeek = [this] (int tick) { if (onSeek) onSeek (tick); };
    timeline.onLoopChanged = [this] { if (onLoopChanged) onLoopChanged(); };

    for (juce::Component* c : std::initializer_list<juce::Component*> { &patternLabel, &patternBox, &newButton, &renameButton, &deleteButton,
                                                                          &undoButton, &redoButton, &snapLabel, &snapBox, &zoomOutButton,
                                                                          &zoomInButton, &zoomFitButton, &positionLabel, &selectionLabel, &timeline })
        addAndMakeVisible (c);

    for (juce::Button* b : std::initializer_list<juce::Button*> { &newButton, &renameButton, &deleteButton, &undoButton, &redoButton, &selectTool,
                                                                   &splitTool, &cutTool, &eraseTool, &zoomTool, &zoomOutButton, &zoomInButton, &zoomFitButton })
        b->setMouseClickGrabsKeyboardFocus (false); // Space must keep playing after a click here

    updateUndoButtons();
}

void PlaylistView::setPatterns (const std::vector<std::string>& names, int selectedIndex)
{
    patternBox.clear (juce::dontSendNotification);
    for (size_t i = 0; i < names.size(); ++i)
        patternBox.addItem (juce::String (names[i]), static_cast<int> (i) + 1);
    patternBox.setSelectedId (selectedIndex + 1, juce::dontSendNotification);
    deleteButton.setEnabled (names.size() > 1);
    timeline.setSelectedPattern (selectedIndex);
}

void PlaylistView::setSelectedPattern (int patternIndex)
{
    patternBox.setSelectedId (patternIndex + 1, juce::dontSendNotification);
    timeline.setSelectedPattern (patternIndex);
}

void PlaylistView::updateUndoButtons()
{
    undoButton.setEnabled (timeline.canUndo());
    redoButton.setEnabled (timeline.canRedo());
}

void PlaylistView::syncToolButtons()
{
    selectTool.setToggleState (timeline.getTool() == PlaylistTool::Select, juce::dontSendNotification);
    splitTool.setToggleState (timeline.getTool() == PlaylistTool::Split, juce::dontSendNotification);
    cutTool.setToggleState (timeline.getTool() == PlaylistTool::Cut, juce::dontSendNotification);
    eraseTool.setToggleState (timeline.getTool() == PlaylistTool::Erase, juce::dontSendNotification);
    zoomTool.setToggleState (timeline.getTool() == PlaylistTool::Zoom, juce::dontSendNotification);
}

void PlaylistView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff22252b));
}

void PlaylistView::resized()
{
    auto area = getLocalBounds();

    auto row1 = area.removeFromTop (kToolbarRowHeight + 4).reduced (4, 2);
    patternLabel.setBounds (row1.removeFromLeft (56));
    patternBox.setBounds (row1.removeFromLeft (150));
    row1.removeFromLeft (6);
    newButton.setBounds (row1.removeFromLeft (46));
    row1.removeFromLeft (3);
    renameButton.setBounds (row1.removeFromLeft (64));
    row1.removeFromLeft (3);
    deleteButton.setBounds (row1.removeFromLeft (58));
    row1.removeFromLeft (14);
    undoButton.setBounds (row1.removeFromLeft (52));
    row1.removeFromLeft (3);
    redoButton.setBounds (row1.removeFromLeft (52));

    auto row2 = area.removeFromTop (kToolbarRowHeight + 4).reduced (4, 2);
    for (auto* b : { &selectTool, &splitTool, &cutTool, &eraseTool, &zoomTool })
    {
        b->setBounds (row2.removeFromLeft (58));
        row2.removeFromLeft (2);
    }
    row2.removeFromLeft (14);
    snapLabel.setBounds (row2.removeFromLeft (34));
    snapBox.setBounds (row2.removeFromLeft (110));
    row2.removeFromLeft (14);
    zoomOutButton.setBounds (row2.removeFromLeft (30));
    row2.removeFromLeft (3);
    zoomInButton.setBounds (row2.removeFromLeft (30));
    row2.removeFromLeft (3);
    zoomFitButton.setBounds (row2.removeFromLeft (40));

    auto status = area.removeFromBottom (kStatusHeight);
    positionLabel.setBounds (status.removeFromLeft (std::min (300, status.getWidth() / 2)).reduced (4, 0));
    selectionLabel.setBounds (status.reduced (4, 0));

    timeline.setBounds (area);
}
} // namespace sls
