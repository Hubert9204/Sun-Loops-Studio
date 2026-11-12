#include "ui/PlaylistView.h"

#include "audio/TimeSignature.h"

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
    // 0.61.0: time markers
    markerButton.setTooltip ("Add a named time marker at the playhead (click the ruler to move the playhead). Click a marker's flag in the ruler to rename or delete it.");
    markerButton.onClick = [this] { timeline.addMarkerAtPlayhead(); };
    markerPrevButton.setTooltip ("Jump to the previous marker");
    markerPrevButton.onClick = [this] { timeline.jumpToMarker (-1); };
    markerNextButton.setTooltip ("Jump to the next marker");
    markerNextButton.onClick = [this] { timeline.jumpToMarker (1); };

    // The tools in FL Studio's order and with its names (0.60.0); the keys 1..8 still choose them.
    const struct { juce::TextButton* button; PlaylistTool tool; const char* tip; } tools[] = {
        { &drawTool,   PlaylistTool::Draw,   "Draw: click empty space to place a pattern (drag to lengthen it); drag a clip to move it, an edge to trim it (1)" },
        { &paintTool,  PlaylistTool::Paint,  "Paint: click empty space to place a pattern, drag along the track to paint one clip after another (9)" },
        { &eraseTool,  PlaylistTool::Erase,  "Delete: click a clip to delete it, or drag across several (4)" },
        { &muteTool,   PlaylistTool::Mute,   "Mute: click a clip to mute or unmute it (it stays, greyed, and does not play); drag across several (7)" },
        { &slipTool,   PlaylistTool::Slip,   "Slip: drag a clip sideways to move what plays inside it, while the clip stays where it is (8)" },
        { &splitTool,  PlaylistTool::Split,  "Slice: click a clip to cut it in two at that point (2). The grid decides where." },
        { &cutTool,    PlaylistTool::Cut,    "Cut: drag across a clip to remove that stretch of it (3)" },
        { &selectTool, PlaylistTool::Select, "Select: drag from empty space to select every clip in the rectangle (Shift adds), then drag them, "
                                             "Delete, Ctrl+C, Ctrl+D (6)" },
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
    drawTool.setToggleState (true, juce::dontSendNotification);

    struct SnapOption { const char* label; int ticks; };
    // "Bar" and "1/2" follow the time signature (0.46.0): 0 and -1 stand for them. "Main" (0.60.0, -2) follows the toolbar's global
    // snap, as in FL Studio.
    static constexpr SnapOption options[] = { { "Main", -2 }, { "Bar", 0 }, { "1/2", -1 }, { "Beat (1/4)", kTicksPerBeat }, { "1/8", kTicksPerBeat / 2 },
                                              { "1/16", kTicksPerBeat / 4 }, { "1/32", kTicksPerBeat / 8 }, { "1/64", kTicksPerBeat / 16 },
                                              { "Off (1 tick)", 1 } };
    for (const auto& o : options)
    {
        snapValues.push_back (o.ticks);
        snapBox.addItem (o.label, static_cast<int> (snapValues.size()));
    }
    snapBox.setSelectedId (4, juce::dontSendNotification); // Beat
    snapBox.setTooltip ("The grid that clips snap to when moved, trimmed, sliced or cut. \"Main\" follows the toolbar's global snap. "
                        "Hold Alt while dragging to ignore it.");
    snapBox.setWantsKeyboardFocus (false);
    snapBox.onChange = [this]
    {
        applySnap();
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
    timeline.onTrackArmToggled = [this] (int track) { if (onTrackArmToggled) onTrackArmToggled (track); };
    timeline.onTrackInputMenu = [this] (int track) { if (onTrackInputMenu) onTrackInputMenu (track); };
    timeline.onTrackRemoved = [this] (int track) { if (onTrackRemoved) onTrackRemoved (track); };
    timeline.onEditSample = [this] (int index) { if (onEditSample) onEditSample (index); };
    timeline.onEditPitch = [this] (int index) { if (onEditPitch) onEditPitch (index); };
    timeline.onSplitStems = [this] (int index) { if (onSplitStems) onSplitStems (index); };
    timeline.onSeek = [this] (int tick) { if (onSeek) onSeek (tick); };
    timeline.onLoopChanged = [this] { if (onLoopChanged) onLoopChanged(); };

    for (juce::Component* c : std::initializer_list<juce::Component*> { &patternLabel, &patternBox, &newButton, &renameButton, &deleteButton,
                                                                          &undoButton, &redoButton, &markerButton, &markerPrevButton, &markerNextButton,
                                                                          &snapLabel, &snapBox, &zoomOutButton,
                                                                          &zoomInButton, &zoomFitButton, &positionLabel, &selectionLabel, &timeline })
        addAndMakeVisible (c);

    for (juce::Button* b : std::initializer_list<juce::Button*> { &newButton, &renameButton, &deleteButton, &undoButton, &redoButton, &drawTool,
                                                                   &splitTool, &cutTool, &eraseTool, &muteTool, &slipTool, &paintTool, &markerButton, &markerPrevButton, &markerNextButton, &zoomTool, &selectTool,
                                                                   &zoomOutButton, &zoomInButton, &zoomFitButton })
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
    drawTool.setToggleState (timeline.getTool() == PlaylistTool::Draw, juce::dontSendNotification);
    selectTool.setToggleState (timeline.getTool() == PlaylistTool::Select, juce::dontSendNotification);
    splitTool.setToggleState (timeline.getTool() == PlaylistTool::Split, juce::dontSendNotification);
    cutTool.setToggleState (timeline.getTool() == PlaylistTool::Cut, juce::dontSendNotification);
    eraseTool.setToggleState (timeline.getTool() == PlaylistTool::Erase, juce::dontSendNotification);
    muteTool.setToggleState (timeline.getTool() == PlaylistTool::Mute, juce::dontSendNotification);
    slipTool.setToggleState (timeline.getTool() == PlaylistTool::Slip, juce::dontSendNotification);
    paintTool.setToggleState (timeline.getTool() == PlaylistTool::Paint, juce::dontSendNotification);
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
    row1.removeFromLeft (14);
    markerPrevButton.setBounds (row1.removeFromLeft (22)); // 0.61.0
    row1.removeFromLeft (2);
    markerButton.setBounds (row1.removeFromLeft (72));
    row1.removeFromLeft (2);
    markerNextButton.setBounds (row1.removeFromLeft (22));

    auto row2 = area.removeFromTop (kToolbarRowHeight + 4).reduced (4, 2);
    for (auto* b : { &drawTool, &paintTool, &eraseTool, &muteTool, &slipTool, &splitTool, &cutTool, &selectTool, &zoomTool }) // FL Studio's order (0.60.0; Paint 0.61.0)
    {
        b->setBounds (row2.removeFromLeft (52));
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
void PlaylistView::applySnap()
{
    const int index = snapBox.getSelectedId() - 1;
    if (index < 0 || index >= static_cast<int> (snapValues.size()))
        return;
    const int v = snapValues[static_cast<size_t> (index)] == -2 ? mainSnapCode : snapValues[static_cast<size_t> (index)]; // "Main" (0.60.0)
    timeline.setSnapTicks (v == 0 ? interfaceMeter().barTicks() : v == -1 ? interfaceMeter().barTicks() / 2 : std::max (1, v));
}

void PlaylistView::setMainSnap (int code)
{
    mainSnapCode = code;
    applySnap();
}
} // namespace sls
