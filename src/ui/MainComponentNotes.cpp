// Part of MainComponent: recording notes from MIDI keyboards.
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
#include "audio/ClipEdit.h"
#include "audio/MidiImport.h"
#include "audio/OfflineRenderer.h"

namespace sls
{
// ------------------------------------------------------------------------- Recording notes (MIDI keyboards)

void MainComponent::applyNoteRecording()
{
    noteRecorder.configure (currentPatternTicks(), noteQuantize);
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
    struct Option { const char* label; int id; int ticks; };
    static constexpr Option options[] = { { "Off (keep the exact timing)", 1, 0 },
                                          { "1/4 note", 2, kTicksPerBeat },
                                          { "1/8 note", 3, kTicksPerBeat / 2 },
                                          { "1/16 note", 4, kTicksPerBeat / 4 },
                                          { "1/32 note", 5, kTicksPerBeat / 8 } };
    juce::PopupMenu menu;
    menu.addSectionHeader ("Quantize the start of recorded notes to");
    for (const auto& o : options)
        menu.addItem (o.id, o.label, true, noteQuantize == o.ticks);
    menu.addSeparator();
    // 0.28.0: what was played without recording
    menu.addSectionHeader ("Score log: what the MIDI keyboard played in the last 5 minutes");
    const bool logged = ! scoreLog.empty();
    menu.addItem (20, "Put the last phrase into the pattern", logged);
    menu.addItem (21, "Put all of it into the pattern", logged);
    menu.addItem (22, "Clear the score log", logged);
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
                            if (result == 20 || result == 21)
                            {
                                safe->dumpScoreLog (result == 20);
                                return;
                            }
                            if (result == 22)
                            {
                                safe->scoreLog.clear();
                                safe->statusLabel.setText ("Score log cleared", juce::dontSendNotification);
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

// Called from the timer: what the MIDI keyboard played goes into the score log (whatever the transport is doing).
void MainComponent::drainScoreEvents()
{
    ScoreEvent e;
    while (engine.popScoreEvent (e))
        scoreLog.add (e);
}

// The score log's notes into the current pattern, each on the channel it was played on, from the pattern's start. The timing is
// kept at the current tempo (starts on the Rec notes grid if one is chosen); what does not fit in the pattern's length is left out.
void MainComponent::dumpScoreLog (bool lastPhraseOnly)
{
    constexpr double kPhraseGapSeconds = 3.0;
    constexpr size_t kMaxNotesPerChannel = 5000;
    const auto logged = scoreLog.notes (bpmSlider.getValue(), noteQuantize, lastPhraseOnly ? kPhraseGapSeconds : 0.0);
    if (logged.empty())
    {
        statusLabel.setText ("The score log is empty: play something on a MIDI keyboard first", juce::dontSendNotification);
        return;
    }

    const int patternLength = currentPatternTicks();
    int added = 0, tooLate = 0, noChannel = 0;
    std::array<bool, kMaxChannels> touched {};
    for (const auto& l : logged)
    {
        if (l.channel < 0 || l.channel >= static_cast<int> (channels.size()))
        {
            ++noChannel;
            continue;
        }
        if (l.note.startTick >= patternLength)
        {
            ++tooLate;
            continue;
        }
        auto n = l.note;
        n.lengthTicks = std::clamp (n.lengthTicks, 1, patternLength - n.startTick);
        auto& notes = channels[static_cast<size_t> (l.channel)].notes;
        if (notes.size() < kMaxNotesPerChannel && addRecordedNote (notes, n))
        {
            ++added;
            touched[static_cast<size_t> (l.channel)] = true;
        }
    }

    if (added > 0)
    {
        for (size_t c = 0; c < channels.size(); ++c)
            if (touched[c])
                strips[c]->repaint();
        pianoRoll.setNotes (channels[static_cast<size_t> (selected)].notes);
        publishProject();
    }
    juce::String text = juce::String (added) + (added == 1 ? " note" : " notes") + " from the score log put into "
                      + juce::String (arrangement.patterns[static_cast<size_t> (currentPattern)].name);
    if (tooLate > 0)
        text << "; " << tooLate << " did not fit in the pattern's length (make the pattern longer, or put in the last phrase only)";
    if (noChannel > 0)
        text << "; " << noChannel << " were played on a channel that no longer exists";
    statusLabel.setText (text, juce::dontSendNotification);
}

// Tells every mixer strip which channels play through it.
void MainComponent::updateTrackRouting()
{
    std::vector<juce::String> names (tracks.size());
    for (const auto& c : channels)
    {
        auto& text = names[static_cast<size_t> (std::clamp (c.mixerTrack, 0, static_cast<int> (tracks.size()) - 1))];
        text += (text.isEmpty() ? "" : ", ") + juce::String (c.name);
    }
    for (int t = 0; t < static_cast<int> (tracks.size()); ++t)
        mixerPanel.setTrackChannels (t, names[static_cast<size_t> (t)]);
}

} // namespace sls
