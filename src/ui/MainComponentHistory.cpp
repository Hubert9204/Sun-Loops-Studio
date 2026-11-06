// Part of MainComponent: undo / redo of every edit to the project (0.15.0).
#include "ui/MainComponent.h"

#include "ui/MixerPanel.h"

namespace sls
{
// Every edit reaches publishProject(), which calls recordHistory(). The state before an edit is kept when the edit
// really changed something (compared through the project file text, which holds every setting but no audio). Edits made
// while one mouse button stays down (a knob turned, a note dragged, steps painted) count as one step.

MainComponent::EditState MainComponent::captureEditState()
{
    EditState s;
    s.text = makeDocument().toJson(); // commits the current pattern, so `channels` and `arrangement` agree
    s.channels = channels;
    s.arrangement = arrangement;
    s.tracks = tracks;
    s.masterSlots = masterTrack.slots;
    s.console = console;
    s.currentPattern = currentPattern;
    s.selected = selected;
    s.bpm = bpmSlider.getValue();
    s.masterVolume = volumeSlider.getValue();
    return s;
}

void MainComponent::recordHistory()
{
    if (restoringHistory)
        return;
    auto now = captureEditState();
    if (! historyReady)
    {
        historyStable = std::move (now);
        historyReady = true;
        return;
    }
    if (now.text == historyStable.text)
        return; // nothing that undo could give back (a refresh, a view change already recorded)

    if (! ignoreDirty)
    {
        const bool mouseDown = juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown();
        const auto ms = juce::Time::getMillisecondCounter();
        const bool sameGesture = mouseDown && lastEditMouseDown && ms - lastEditMs < 5000;
        if (! sameGesture)
        {
            undoHistory.push_back (std::move (historyStable));
            if (undoHistory.size() > kMaxHistory)
                undoHistory.erase (undoHistory.begin());
            redoHistory.clear();
        }
        lastEditMs = ms;
        lastEditMouseDown = mouseDown;
    }
    historyStable = std::move (now);
}

void MainComponent::clearHistory()
{
    undoHistory.clear();
    redoHistory.clear();
    historyReady = false;
}

void MainComponent::applyEditState (const EditState& s)
{
    const juce::ScopedValueSetter<bool> restoring (restoringHistory, true);
    pianoRoll.setNotes ({}); // also ends a note played by hand
    closeAutomationEditor();
    sampleEditorWindow.reset();

    rackView.setStrips ({});
    strips.clear();
    channels.clear();
    for (const auto& c : s.channels)
        channels.push_back (c);
    jassert (channels.size() <= channels.capacity());
    for (size_t i = 0; i < channels.size(); ++i)
        createStrip (i);
    refreshRackStrips();

    for (size_t t = 0; t < tracks.size() && t < s.tracks.size(); ++t)
        tracks[t] = s.tracks[t]; // in place: the mixer strips refer to these objects
    masterTrack.slots = s.masterSlots;
    console = s.console;
    for (int t = 0; t < kMaxTracks; ++t)
        mixerPanel.refreshTrack (t);
    mixerPanel.refreshTrack (kMasterIndex);
    mixerPanel.refreshConsole();

    arrangement = s.arrangement;
    currentPattern = std::clamp (s.currentPattern, 0, static_cast<int> (arrangement.patterns.size()) - 1);
    showPatternLength();
    refreshPatternBox();
    playlistView.setSelectedPattern (currentPattern);
    playlistView.clearHistory(); // its own steps would no longer match
    playlistView.refreshTimeline();
    resetNoteHistory();

    if (std::abs (bpmSlider.getValue() - s.bpm) > 1.0e-9)
    {
        bpmSlider.setValue (s.bpm, juce::dontSendNotification);
        engine.setBpm (s.bpm);
        playlistView.setBpm (s.bpm);
    }
    if (std::abs (volumeSlider.getValue() - s.masterVolume) > 1.0e-9)
    {
        volumeSlider.setValue (s.masterVolume, juce::dontSendNotification);
        engine.setGain (static_cast<float> (s.masterVolume));
        mixerPanel.setMasterVolume (static_cast<float> (s.masterVolume));
    }

    if (samplerWindow != nullptr && (samplerWindowChannel < 0 || samplerWindowChannel >= static_cast<int> (channels.size())))
        closeSamplerSettings(); // (otherwise publishProject() below refreshes it)
    if (drumWindow != nullptr)
    {
        if (drumWindowChannel >= 0 && drumWindowChannel < static_cast<int> (channels.size())
            && channels[static_cast<size_t> (drumWindowChannel)].instrument == Instrument::Drums)
            drumWindow->view().refresh();
        else
            closeDrumMachine();
    }

    selectChannel (std::clamp (s.selected, 0, static_cast<int> (channels.size()) - 1));
    updateTrackRouting();
    if (songMode && ! arrangement.hasLoop())
        songStartTick = std::min (songStartTick, arrangement.timelineTicks() - 1);
    publishProject();
}

void MainComponent::undoEdit()
{
    if (undoHistory.empty())
    {
        statusLabel.setText ("Nothing to undo", juce::dontSendNotification);
        return;
    }
    recordHistory(); // an edit that has not been recorded yet (none should be) comes first
    redoHistory.push_back (std::move (historyStable));
    historyStable = std::move (undoHistory.back());
    undoHistory.pop_back();
    applyEditState (historyStable);
    statusLabel.setText ("Undone (" + juce::String (static_cast<int> (undoHistory.size())) + " more step(s) can be undone)", juce::dontSendNotification);
}

void MainComponent::redoEdit()
{
    if (redoHistory.empty())
    {
        statusLabel.setText ("Nothing to redo", juce::dontSendNotification);
        return;
    }
    undoHistory.push_back (std::move (historyStable));
    historyStable = std::move (redoHistory.back());
    redoHistory.pop_back();
    applyEditState (historyStable);
    statusLabel.setText ("Redone", juce::dontSendNotification);
}
} // namespace sls
