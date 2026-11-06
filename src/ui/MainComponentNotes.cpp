// Part of MainComponent: note undo and redo, recording notes from MIDI keyboards.
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
// ------------------------------------------------------------------------- Undo / redo of note edits

MainComponent::NoteState MainComponent::captureNoteState() const
{
    NoteState s;
    s.pattern = currentPattern;
    s.notes.reserve (channels.size());
    for (const auto& c : channels)
        s.notes.push_back (c.notes);
    return s;
}

bool MainComponent::sameNoteState (const NoteState& a, const NoteState& b)
{
    if (a.pattern != b.pattern || a.notes.size() != b.notes.size())
        return false;
    for (size_t c = 0; c < a.notes.size(); ++c)
    {
        if (a.notes[c].size() != b.notes[c].size())
            return false;
        for (size_t i = 0; i < a.notes[c].size(); ++i)
        {
            const auto& x = a.notes[c][i];
            const auto& y = b.notes[c][i];
            if (x.pitch != y.pitch || x.startTick != y.startTick || x.lengthTicks != y.lengthTicks || std::abs (x.velocity - y.velocity) > 1.0e-6f || x.link != y.link)
                return false;
        }
    }
    return true;
}

void MainComponent::recordNoteChange()
{
    auto now = captureNoteState();
    if (sameNoteState (now, lastNoteState))
        return;

    // The pattern or the number of channels changed since the last state: that is not a note edit, only a new starting point.
    if (now.pattern == lastNoteState.pattern && now.notes.size() == lastNoteState.notes.size())
    {
        noteUndo.push_back (std::move (lastNoteState));
        if (noteUndo.size() > 200)
            noteUndo.erase (noteUndo.begin());
        noteRedo.clear();
    }
    lastNoteState = std::move (now);
}

void MainComponent::resetNoteHistory()
{
    noteUndo.clear();
    noteRedo.clear();
    lastNoteState = captureNoteState();
}

void MainComponent::applyNoteState (const NoteState& state)
{
    if (state.pattern != currentPattern && state.pattern < static_cast<int> (arrangement.patterns.size()))
        selectPattern (state.pattern);

    for (size_t c = 0; c < channels.size() && c < state.notes.size(); ++c)
    {
        channels[c].notes = state.notes[c];
        strips[c]->refreshFromChannel();
    }
    pianoRoll.setNotes (channels[static_cast<size_t> (selected)].notes);
    commitCurrentPattern();
    lastNoteState = captureNoteState();
    markDirty();
    publishProject();
}

void MainComponent::undoNotes()
{
    if (noteUndo.empty())
    {
        statusLabel.setText ("Nothing to undo in the notes", juce::dontSendNotification);
        return;
    }
    auto target = std::move (noteUndo.back());
    noteUndo.pop_back();
    noteRedo.push_back (captureNoteState());
    applyNoteState (target);
    statusLabel.setText ("Undid a note edit", juce::dontSendNotification);
}

void MainComponent::redoNotes()
{
    if (noteRedo.empty())
    {
        statusLabel.setText ("Nothing to redo in the notes", juce::dontSendNotification);
        return;
    }
    auto target = std::move (noteRedo.back());
    noteRedo.pop_back();
    noteUndo.push_back (captureNoteState());
    applyNoteState (target);
    statusLabel.setText ("Redid a note edit", juce::dontSendNotification);
}


// ------------------------------------------------------------------------- Recording notes (MIDI keyboards)

void MainComponent::applyNoteRecording()
{
    noteRecorder.configure (arrangement.patternTicks, noteQuantize);
    noteRecordButton.setToggleState (noteRecordOn, juce::dontSendNotification);
    engine.setNoteRecording (noteRecordOn);
    if (settings != nullptr)
    {
        settings->setValue ("midirec.quantize", noteQuantize);
        settings->saveIfNeeded();
    }
}

void MainComponent::toggleNoteRecording()
{
    noteRecordOn = ! noteRecordOn;
    if (! noteRecordOn)
        endNoteTake();
    applyNoteRecording();

    if (noteRecordOn)
    {
        const auto midi = engine.describeMidiInputs();
        statusLabel.setText (midi.isEmpty() ? "Rec notes is on, but no MIDI keyboard is connected (see Audio...)"
                                            : (engine.isPlaying() ? "Recording notes: play your keyboard" : "Rec notes is on: press Play, then play your keyboard"),
                             juce::dontSendNotification);
    }
    else
        statusLabel.setText ("Rec notes is off", juce::dontSendNotification);
}

void MainComponent::openNoteRecordMenu()
{
    struct Option { int id; const char* label; int ticks; };
    static constexpr Option options[] = { { 1, "Off (keep the exact timing)", 0 },
                                          { 2, "1/4 note", kTicksPerBeat },
                                          { 3, "1/8 note", kTicksPerBeat / 2 },
                                          { 4, "1/16 note", kTicksPerBeat / 4 },
                                          { 5, "1/32 note", kTicksPerBeat / 8 } };
    juce::PopupMenu menu;
    menu.addSectionHeader ("Quantize the start of recorded notes to");
    for (const auto& o : options)
        menu.addItem (o.id, o.label, true, noteQuantize == o.ticks);
    menu.addSeparator();
    menu.addItem (10, "Audio and MIDI devices...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&noteRecordButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == 10)
                            {
                                safe->openAudioSettings();
                                return;
                            }
                            for (const auto& o : options)
                                if (o.id == result)
                                    safe->noteQuantize = o.ticks;
                            safe->applyNoteRecording();
                        });
}

// Ends the notes that are still held when recording stops (key down at the moment the song stops).
void MainComponent::endNoteTake()
{
    bool added = false;
    noteRecorder.flush (kTicksPerBeat / 2, [this, &added] (int channel, const Note& n)
    {
        if (channel >= 0 && channel < static_cast<int> (channels.size()))
            added = addRecordedNote (channels[static_cast<size_t> (channel)].notes, n) || added;
    });
    if (added)
    {
        pianoRoll.setNotes (channels[static_cast<size_t> (selected)].notes);
        publishProject();
    }
    recordNoteChange(); // everything recorded since the take began is one undo step
}

// Called from the timer: the notes that the audio thread stamped since the last tick become notes of the pattern.
void MainComponent::drainMidiTakes()
{
    constexpr size_t kMaxNotesPerChannel = 5000;
    MidiTake take;
    std::array<bool, kMaxChannels> touched {};
    bool any = false;

    while (engine.popMidiTake (take))
    {
        if (! noteRecordOn)
            continue; // switched off after the event was queued

        noteRecorder.process (take, [this, &touched, &any] (int channel, const Note& n)
        {
            if (channel < 0 || channel >= static_cast<int> (channels.size()))
                return;
            auto& notes = channels[static_cast<size_t> (channel)].notes;
            if (notes.size() < kMaxNotesPerChannel && addRecordedNote (notes, n))
            {
                touched[static_cast<size_t> (channel)] = true;
                any = true;
            }
        });
    }

    if (! any)
        return;

    for (size_t c = 0; c < channels.size(); ++c)
        if (touched[c])
            strips[c]->repaint();
    pianoRoll.setNotes (channels[static_cast<size_t> (selected)].notes);
    publishProject();
}

// Tells every mixer strip which channels play through it.
void MainComponent::updateTrackRouting()
{
    std::array<juce::String, kMaxTracks> names;
    for (const auto& c : channels)
    {
        auto& text = names[static_cast<size_t> (std::clamp (c.mixerTrack, 0, kMaxTracks - 1))];
        text += (text.isEmpty() ? "" : ", ") + juce::String (c.name);
    }
    for (int t = 0; t < kMaxTracks; ++t)
        mixerPanel.setTrackChannels (t, names[static_cast<size_t> (t)]);
}

} // namespace sls
