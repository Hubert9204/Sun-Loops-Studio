// Part of MainComponent: patterns and the playlist.
#include "ui/MainComponent.h"

#include "ui/AssignTrackDialog.h"
#include "ui/AudioSettings.h"
#include "ui/MetronomePanel.h"
#include "ui/TextPrompt.h"

#include <functional>
#include <map>
#include <optional>

#include <juce_audio_utils/juce_audio_utils.h>

#include "audio/BuiltinSamples.h"
#include "audio/CyberHub.h"
#include "audio/ClipEdit.h"
#include "audio/MidiImport.h"
#include "audio/OfflineRenderer.h"

namespace sls
{
// ------------------------------------------------------------------------- Patterns and playlist

void MainComponent::commitCurrentPattern()
{
    arrangement.ensureChannelCount (channels.size());
    auto& pattern = arrangement.patterns[static_cast<size_t> (currentPattern)];
    for (size_t c = 0; c < channels.size(); ++c)
        pattern.notes[c] = channels[c].notes;
}

void MainComponent::selectPattern (int index)
{
    const juce::ScopedValueSetter<bool> viewOnly (ignoreDirty, true); // looking at another pattern is not an edit
    index = std::clamp (index, 0, static_cast<int> (arrangement.patterns.size()) - 1);
    commitCurrentPattern();
    currentPattern = index;

    const auto& notes = arrangement.patterns[static_cast<size_t> (index)].notes;
    for (size_t c = 0; c < channels.size(); ++c)
    {
        channels[c].notes = notes[c];
        strips[c]->refreshFromChannel();
    }

    pianoRoll.setNotes (channels[static_cast<size_t> (selected)].notes);
    playlistView.setSelectedPattern (index);
    patternBox.setSelectedId (index + 1, juce::dontSendNotification);
    statusLabel.setText ("Pattern: " + juce::String (arrangement.patterns[static_cast<size_t> (index)].name),
                         juce::dontSendNotification);
    lastNoteState = captureNoteState(); // another pattern is not a note edit
    publishProject();
}

void MainComponent::addPattern (bool cloneCurrent)
{
    if (arrangement.patterns.size() >= static_cast<size_t> (kMaxPatterns))
        return;

    commitCurrentPattern();

    Pattern pattern;
    pattern.name = arrangement.newPatternName();
    if (cloneCurrent)
        pattern.notes = arrangement.patterns[static_cast<size_t> (currentPattern)].notes;
    arrangement.patterns.push_back (std::move (pattern));
    arrangement.ensureChannelCount (channels.size());

    refreshPatternBox();
    selectPattern (static_cast<int> (arrangement.patterns.size()) - 1);
    markDirty();
}

void MainComponent::showNewPatternMenu (juce::Component& target)
{
    juce::PopupMenu menu;
    menu.addItem (1, "New empty pattern");
    menu.addItem (2, "Clone current pattern");
    juce::PopupMenu lengths;
    const int bars = arrangement.patternTicks / (4 * kTicksPerBeat);
    for (const int choice : kPatternBarChoices)
        lengths.addItem (100 + choice, juce::String (choice) + (choice == 1 ? " bar" : " bars"), true, choice == bars);
    menu.addSeparator();
    menu.addSubMenu ("Pattern length (all patterns)", lengths);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result > 100)
                                safe->setPatternLength (result - 100);
                            else
                                safe->addPattern (result == 2);
                        });
}

void MainComponent::showPatternLength()
{
    const int ticks = arrangement.patternTicks;
    pianoRoll.setLoopLengthTicks (ticks);
    for (auto& strip : strips)
    {
        strip->setLoopLengthTicks (ticks);
        strip->refreshFromChannel();
    }
    if (drumWindow != nullptr)
        drumWindow->view().setLoopTicks (ticks);
    noteRecorder.configure (ticks, noteQuantize);
}

// Every pattern gets the new length (a playlist unit is one pattern long, so pattern clips keep their place in units:
// with longer patterns the song gets longer). Notes past the end of a shorter pattern are kept but not played, and come
// back when the patterns are made longer again. Refused when something would no longer fit in the timeline.
void MainComponent::setPatternLength (int bars)
{
    const int ticks = bars * 4 * kTicksPerBeat;
    if (ticks == arrangement.patternTicks)
        return;
    auto changed = arrangement;
    changed.patternTicks = ticks;
    bool fits = true;
    for (const auto& c : changed.clips)
        fits = fits && c.endUnit() <= changed.timelineUnits();
    for (const auto& a : changed.audioClips)
        fits = fits && a.endTick() <= changed.timelineTicks();
    for (const auto& a : changed.automationClips)
        fits = fits && a.endTick() <= changed.timelineTicks();
    if (! fits)
    {
        statusLabel.setText ("The playlist would not fit in the timeline with " + juce::String (bars) + "-bar patterns: move the last clips first",
                             juce::dontSendNotification);
        return;
    }
    commitCurrentPattern();
    arrangement.patternTicks = ticks;
    if (! arrangement.hasLoop())
        arrangement.clearLoop();
    showPatternLength();
    playlistView.clearHistory();
    playlistView.refreshTimeline();
    pianoRoll.setNotes (channels[static_cast<size_t> (selected)].notes);
    songStartTick = std::min (songStartTick, arrangement.timelineTicks() - 1);
    publishProject();
    statusLabel.setText ("Patterns are " + juce::String (bars) + (bars == 1 ? " bar" : " bars") + " long", juce::dontSendNotification);
}

void MainComponent::renamePattern (int index)
{
    if (index < 0 || index >= static_cast<int> (arrangement.patterns.size()))
        return;

    promptForText (*this, "Rename pattern", "Name:", juce::String (arrangement.patterns[static_cast<size_t> (index)].name), "OK",
                   [safe = juce::Component::SafePointer<MainComponent> (this), index] (const juce::String& typed)
                   {
                       if (safe == nullptr || index >= static_cast<int> (safe->arrangement.patterns.size()))
                           return;

                       auto name = typed.toStdString();
                       if (! cleanPatternName (name))
                           return; // an empty name keeps the old one

                       safe->arrangement.patterns[static_cast<size_t> (index)].name = name;
                       safe->refreshPatternBox();
                       safe->markDirty();
                       safe->statusLabel.setText ("Pattern renamed to " + juce::String (name), juce::dontSendNotification);
                   });
}

// Puts the notes of pattern `index` into the channels (without saving the channels' notes anywhere first).
void MainComponent::loadPatternIntoChannels (int index)
{
    arrangement.ensureChannelCount (channels.size());
    const auto& notes = arrangement.patterns[static_cast<size_t> (index)].notes;
    for (size_t c = 0; c < channels.size(); ++c)
    {
        channels[c].notes = notes[c];
        strips[c]->refreshFromChannel();
    }
    pianoRoll.setNotes (channels[static_cast<size_t> (selected)].notes);
    resetNoteHistory(); // the pattern numbers behind the old states may be gone
}

void MainComponent::deletePattern (int index)
{
    if (index < 0 || index >= static_cast<int> (arrangement.patterns.size()))
        return;
    if (arrangement.patterns.size() <= 1)
    {
        statusLabel.setText ("The last pattern cannot be deleted", juce::dontSendNotification);
        return;
    }

    commitCurrentPattern();
    const auto name = juce::String (arrangement.patterns[static_cast<size_t> (index)].name);
    int used = 0;
    for (const auto& c : arrangement.clips)
        used += c.pattern == index ? 1 : 0;

    auto message = "Delete \"" + name + "\" with all its notes?";
    if (used > 0)
        message += " The " + juce::String (used) + (used == 1 ? " clip" : " clips") + " that play it on the playlist will be removed too.";
    message += "\nThis cannot be undone.";

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete pattern", message, "Delete", "Cancel", nullptr,
                                        juce::ModalCallbackFunction::create (
                                            [safe = juce::Component::SafePointer<MainComponent> (this), index, name] (int result)
                                            {
                                                if (safe == nullptr || result != 1 || index >= static_cast<int> (safe->arrangement.patterns.size()))
                                                    return;

                                                safe->commitCurrentPattern();
                                                if (! safe->arrangement.removePattern (index))
                                                    return;

                                                // Which pattern is open afterwards: the same one if it is still there, else its neighbour.
                                                int now = safe->currentPattern;
                                                if (now == index)
                                                    now = std::min (index, static_cast<int> (safe->arrangement.patterns.size()) - 1);
                                                else if (now > index)
                                                    --now;
                                                safe->currentPattern = now;

                                                safe->loadPatternIntoChannels (now);
                                                safe->playlistView.clearHistory();
                                                safe->refreshPatternBox();
                                                safe->playlistView.setSelectedPattern (now);
                                                safe->statusLabel.setText ("Deleted pattern " + name, juce::dontSendNotification);
                                                safe->publishProject();
                                            }));
}

void MainComponent::refreshPatternBox()
{
    patternBox.clear (juce::dontSendNotification);
    std::vector<std::string> names;
    for (size_t i = 0; i < arrangement.patterns.size(); ++i)
    {
        patternBox.addItem (juce::String (arrangement.patterns[i].name), static_cast<int> (i) + 1);
        names.push_back (arrangement.patterns[i].name);
    }
    patternBox.setSelectedId (currentPattern + 1, juce::dontSendNotification);
    playlistView.setPatterns (names, currentPattern);
}

// A sound dragged from the browser onto the playlist becomes an audio clip there.
void MainComponent::dropOnPlaylist (const drag::Item& item, int track, int tick)
{
    if (item.kind == "file")
    {
        importMediaFiles (juce::StringArray (item.payload), track, tick);
        return;
    }
    if (item.kind != "builtin")
        return;

    const auto data = makeBuiltinSample (item.payload.toStdString());
    if (data == nullptr)
    {
        statusLabel.setText ("That sound is not available", juce::dontSendNotification);
        return;
    }

    juce::String note;
    if (addAudioClip (data, track, tick, note))
    {
        playlistView.clearHistory(); // the arrangement was changed from outside: undo could not restore it safely
        playlistView.repaint();
        publishProject();
        statusLabel.setText ("Placed " + juce::String (data->name) + " on Track " + juce::String (track + 1) + (note.isEmpty() ? juce::String() : ". " + note),
                             juce::dontSendNotification);
    }
    else
        statusLabel.setText (note, juce::dontSendNotification);
}

void MainComponent::setSongMode (bool shouldPlaySong)
{
    const juce::ScopedValueSetter<bool> viewOnly (ignoreDirty, true); // Pattern/Song is a way of listening
    songMode = shouldPlaySong;
    modeButton.setToggleState (songMode, juce::dontSendNotification);
    modeButton.setButtonText (songMode ? "Song" : "Pattern");
    statusLabel.setText (songMode ? "Playing the playlist" : "Looping the selected pattern", juce::dontSendNotification);
    publishProject();
    if (! engine.isPlaying())
        engine.requestSeek (songMode ? songStartTick : patternStartTick); // where the ruler of the piano roll or of the playlist put it
}

void MainComponent::seekPattern (int tick)
{
    if (songMode)
        setSongMode (false); // the ruler of the piano roll is the pattern's
    patternStartTick = std::clamp (tick, 0, arrangement.patternTicks - 1);
    engine.requestSeek (patternStartTick);
    pianoRoll.setStartTick (patternStartTick);
}

void MainComponent::seekSong (int tick)
{
    if (! songMode)
        setSongMode (true); // the ruler of the playlist is the song's
    songStartTick = std::max (0, tick);
    engine.requestSeek (songStartTick);
    playlistView.setPlayheadTick (songStartTick);
}

Project MainComponent::compileCurrent()
{
    commitCurrentPattern();
    return songMode ? compileSong (channels, arrangement, tracks, masterTrack.slots, console)
                    : compilePattern (channels, arrangement, currentPattern, tracks, masterTrack.slots, console);
}

} // namespace sls
