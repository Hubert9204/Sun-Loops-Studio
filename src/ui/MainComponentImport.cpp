// Part of MainComponent: samples, audio and MIDI file import, audio clips.
#include "ui/MainComponent.h"

#include "ui/AssignTrackDialog.h"
#include "ui/AudioSettings.h"
#include "ui/MetronomePanel.h"
#include "ui/TextPrompt.h"

#include <functional>
#include <map>
#include <optional>
#include <tuple>

#include <juce_audio_utils/juce_audio_utils.h>

#include "audio/BuiltinSamples.h"
#include "audio/CyberHub.h"
#include "audio/ClipEdit.h"
#include "audio/MidiImport.h"
#include "audio/OfflineRenderer.h"

namespace sls
{
// ------------------------------------------------------------------------- Audio clips

struct MainComponent::ImportBatch
{
    int remaining = 0;
    int added = 0;
    juce::StringArray errors, notes;
};

void MainComponent::chooseMidiToImport()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Import a MIDI file into new channels and patterns", library.root, "*.mid;*.midi");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& chooser)
                              {
                                  if (safe != nullptr && chooser.getResult() != juce::File())
                                      safe->importMidiFile (chooser.getResult());
                              });
}

// Every MIDI track with notes becomes a new synth channel, the notes are spread over new patterns (two bars each) and the
// patterns are placed on playlist track 1 after whatever the playlist already holds. See planMidiImport.
void MainComponent::importMidiFile (const juce::File& file)
{
    const auto fail = [this, &file] (const juce::String& why)
    {
        statusLabel.setText ("Could not import " + file.getFileName() + ": " + why, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI import", file.getFileName() + "\n\n" + why);
    };

    if (file.getSize() > static_cast<juce::int64> (kMaxMidiFileBytes))
        return fail ("the file is larger than 8 MB");
    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
        return fail ("the file cannot be read");

    const auto parsed = parseMidiFile (bytes.getData(), bytes.getSize());
    if (! parsed.ok)
        return fail (juce::String (parsed.error));

    const int startUnit = arrangement.songLengthUnits();
    const int room = std::min (kMaxPatterns - static_cast<int> (arrangement.patterns.size()), kPlaylistUnits - startUnit);
    if (room <= 0)
        return fail ("there is no room left for new patterns on the playlist");

    const int freeChannels = kMaxChannels - static_cast<int> (channels.size());
    const auto plan = planMidiImport (parsed, freeChannels, arrangement.patternTicks, room);
    if (plan.channelNames.empty() || plan.patternCount() == 0)
        return fail (plan.skippedPercussion > 0 && freeChannels > 0 ? "it only holds drums (General MIDI percussion is not imported)"
                                                                   : "there is no free channel: remove a channel first");

    commitCurrentPattern();
    std::vector<int> newChannels;
    for (const auto& name : plan.channelNames)
    {
        const int index = addChannel (Instrument::Synth, false);
        if (index < 0)
            break;
        auto channelName = name;
        if (channelName.size() > 40)
            channelName.resize (40);
        channels[static_cast<size_t> (index)].name = channelName;
        newChannels.push_back (index);
    }
    arrangement.ensureChannelCount (channels.size());

    auto stem = file.getFileNameWithoutExtension().toStdString();
    if (! cleanPatternName (stem))
        stem = "MIDI";
    if (plan.patternCount() > 1 && stem.size() > 36)
        stem.resize (36);

    const int firstPattern = static_cast<int> (arrangement.patterns.size());
    for (int p = 0; p < plan.patternCount(); ++p)
    {
        Pattern pattern;
        pattern.name = plan.patternCount() == 1 ? stem : stem + " " + std::to_string (p + 1);
        pattern.notes.resize (channels.size());
        for (size_t k = 0; k < newChannels.size() && k < plan.patternNotes[static_cast<size_t> (p)].size(); ++k)
            pattern.notes[static_cast<size_t> (newChannels[k])] = plan.patternNotes[static_cast<size_t> (p)][k];
        arrangement.patterns.push_back (std::move (pattern));
        arrangement.clips.push_back ({ firstPattern + p, 0, startUnit + p, 1 });
    }

    juce::String message = "Imported " + file.getFileName() + ": " + juce::String (static_cast<int> (newChannels.size()))
                         + (newChannels.size() == 1 ? " channel, " : " channels, ") + juce::String (plan.patternCount())
                         + (plan.patternCount() == 1 ? " pattern" : " patterns") + " on playlist track 1";
    if (parsed.bpm > 0.0)
    {
        bpmSlider.setValue (std::clamp (std::round (parsed.bpm), 40.0, 240.0)); // the slider drives the engine
        message += ", tempo " + juce::String (juce::roundToInt (bpmSlider.getValue())) + " BPM";
    }
    if (parsed.tempoChanges)
        message += " (the file changes tempo: only its first tempo is used)";
    if (plan.skippedPercussion > 0)
        message += ". Drum tracks skipped: " + juce::String (plan.skippedPercussion);
    if (plan.skippedTracks > 0)
        message += ". Tracks that did not fit in the free channels: " + juce::String (plan.skippedTracks);
    if (plan.tiedNotes > 0)
        message += ". " + juce::String (plan.tiedNotes) + " long notes cross a pattern boundary and sound again at the start of the next pattern";
    if (plan.cutNotes > 0)
        message += ". " + juce::String (plan.cutNotes) + " notes at the end did not fit on the playlist";

    selectPattern (firstPattern);
    if (! newChannels.empty())
        selectChannel (newChannels.front()); // shows its name and scrolls the piano roll to its notes
    refreshPatternBox();
    resetNoteHistory();
    playlistView.clearHistory();
    playlistView.repaint();
    markDirty();
    publishProject();
    statusLabel.setText (message, juce::dontSendNotification);
}

void MainComponent::chooseMediaToImport()
{
    juce::String wildcard;
    for (const auto& ext : importableAudioExtensions())
        wildcard += "*" + ext + ";";
    for (const auto& ext : importableVideoExtensions())
        wildcard += "*" + ext + ";";

    fileChooser = std::make_unique<juce::FileChooser> ("Import audio or video into the playlist", library.samples, wildcard);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& chooser)
                              {
                                  if (safe == nullptr)
                                      return;
                                  juce::StringArray paths;
                                  for (const auto& f : chooser.getResults())
                                      paths.add (f.getFullPathName());
                                  if (! paths.isEmpty())
                                      safe->importMediaFiles (paths, 0, 0);
                              });
}

// The files go on consecutive tracks starting at `track`, each at the first free beat from `tick` on.
void MainComponent::importMediaFiles (const juce::StringArray& allPaths, int track, int tick)
{
    // MIDI files are not audio: they become channels and patterns, right away.
    juce::StringArray paths;
    for (const auto& path : allPaths)
    {
        if (isMidiFileName (path.toStdString()))
            importMidiFile (juce::File (path));
        else
            paths.add (path);
    }
    if (paths.isEmpty())
        return;

    auto batch = std::make_shared<ImportBatch>();
    batch->remaining = paths.size();
    statusLabel.setText ("Importing " + (paths.size() == 1 ? juce::File (paths[0]).getFileName() : juce::String (paths.size()) + " files") + "...",
                         juce::dontSendNotification);

    library.ensureExists();
    const auto videoFolder = library.samples.getChildFile ("From videos");

    for (int i = 0; i < paths.size(); ++i)
    {
        const juce::File file (paths[i]);
        const int target = (std::clamp (track, 0, kPlaylistTracks - 1) + i) % kPlaylistTracks;
        importPool.addJob ([safe = juce::Component::SafePointer<MainComponent> (this), batch, file, videoFolder, target, tick]
                           {
                               auto result = std::make_shared<ImportResult> (importMediaFile (file, videoFolder));
                               juce::MessageManager::callAsync ([safe, batch, result, file, target, tick]
                                                                {
                                                                    if (safe != nullptr)
                                                                        safe->finishImport (batch, *result, file, target, tick);
                                                                });
                           });
    }
}

bool MainComponent::addAudioClip (const std::shared_ptr<const SampleData>& data, int track, int tick, juce::String& note)
{
    if (arrangement.audioClips.size() >= static_cast<size_t> (kMaxAudioClips))
    {
        note = "The playlist already holds the maximum of " + juce::String (kMaxAudioClips) + " audio clips.";
        return false;
    }

    AudioClip clip;
    clip.data = data;
    clip.name = data->name;
    clip.track = std::clamp (track, 0, kPlaylistTracks - 1);

    const int natural = clip.naturalLengthTicks (bpmSlider.getValue());
    int start = arrangement.findFreeAudioStart (clip.track, tick, natural);
    int length = natural;
    if (start < 0)
    {
        // Not all of it fits anywhere on the track: use the first free spot and cut the clip to what is free there.
        start = arrangement.findFreeAudioStart (clip.track, tick, kTicksPerBeat);
        length = start >= 0 ? arrangement.maxAudioLengthAt (clip.track, start, natural) : 0;
        if (start < 0 || length < 1)
        {
            note = "There is no free room on Track " + juce::String (clip.track + 1) + " for " + juce::String (data->name) + ".";
            return false;
        }
        note = juce::String (data->name) + " does not fit in full and was cut at the end of the free space.";
    }

    clip.startTick = start;
    clip.lengthTicks = length;
    arrangement.audioClips.push_back (std::move (clip));
    return true;
}

void MainComponent::finishImport (const std::shared_ptr<ImportBatch>& batch, const ImportResult& result, const juce::File& file,
                                  int track, int tick)
{
    juce::String note;
    if (result.data == nullptr)
        batch->errors.add (result.error.isNotEmpty() ? result.error : "Could not import " + file.getFileName());
    else if (addAudioClip (result.data, track, tick, note))
    {
        ++batch->added;
        if (note.isNotEmpty())
            batch->notes.add (note);
        if (result.warning.isNotEmpty())
            batch->notes.add (result.warning);
    }
    else
        batch->errors.add (note);

    if (--batch->remaining > 0)
        return;

    if (batch->added > 0)
    {
        playlistView.clearHistory(); // the arrangement was changed from outside: undo could not restore it safely
        playlistView.repaint();
        if (! songMode)
        {
            setSongMode (true); // audio clips only play in song mode
            batch->notes.add ("Switched to Song mode: audio clips play with the whole playlist.");
        }
        publishProject();
    }

    juce::String status = batch->added == 0 ? "Nothing was imported"
                                            : "Added " + juce::String (batch->added) + (batch->added == 1 ? " audio clip" : " audio clips") + " to the playlist";
    if (batch->notes.size() > 0)
        status += " - " + batch->notes.joinIntoString (" ");
    statusLabel.setText (status, juce::dontSendNotification);

    if (! batch->errors.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import",
                                                batch->errors.joinIntoString ("\n"));
}

// After a project was opened its audio clips only know the path of their file: read the audio now.
void MainComponent::loadAudioClipFiles (juce::StringArray& problems)
{
    loadAudioForClips (arrangement, problems);
}

} // namespace sls
