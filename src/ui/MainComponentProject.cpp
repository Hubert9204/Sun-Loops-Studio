// Part of MainComponent: project files, recent projects, the File menu.
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
// ------------------------------------------------------------------------- Project files

namespace
{
constexpr int kMaxRecentProjects = 8;
constexpr const char* kRecentKey = "recent.projects";
} // namespace

juce::String MainComponent::projectName() const
{
    return currentProjectFile == juce::File() ? juce::String ("Untitled") : currentProjectFile.getFileNameWithoutExtension();
}

void MainComponent::markDirty()
{
    if (ignoreDirty || projectDirty)
        return;
    projectDirty = true;
    updateWindowTitle();
}

void MainComponent::updateWindowTitle()
{
    if (auto* top = getTopLevelComponent(); top != nullptr && top != this)
        top->setName (projectName() + (projectDirty ? " *" : "") + " - Sun Loops Studio");
}

juce::StringArray MainComponent::recentProjects() const
{
    juce::StringArray list;
    if (settings != nullptr)
        list.addLines (settings->getValue (kRecentKey));
    list.removeEmptyStrings();
    return list;
}

void MainComponent::addRecentProject (const juce::File& file)
{
    if (settings == nullptr)
        return;
    auto list = recentProjects();
    list.removeString (file.getFullPathName());
    list.insert (0, file.getFullPathName());
    while (list.size() > kMaxRecentProjects)
        list.remove (list.size() - 1);
    settings->setValue (kRecentKey, list.joinIntoString ("\n"));
    settings->saveIfNeeded();
}

ProjectDocument MainComponent::makeDocument()
{
    commitCurrentPattern();

    ProjectDocument doc;
    doc.channels = channels;
    for (auto& c : doc.channels)
    {
        doc.sampleSources.push_back (c.sample != nullptr ? c.sample->source : std::string());
        c.notes.clear(); // the notes live in the patterns
        c.sample = nullptr;
    }
    doc.tracks = tracks;
    doc.masterSlots = masterTrack.slots;
    doc.arrangement = arrangement;
    doc.currentPattern = currentPattern;
    doc.songMode = songMode;
    doc.bpm = bpmSlider.getValue();
    doc.masterVolume = static_cast<float> (volumeSlider.getValue());
    return doc;
}

// Puts a loaded document on screen. `problems` collects things worth telling the user (missing sample files).
void MainComponent::applyDocument (ProjectDocument doc, const juce::File& file, juce::StringArray& problems)
{
    const juce::ScopedValueSetter<bool> loading (ignoreDirty, true);

    setPlaying (false);
    pianoRoll.setNotes ({}); // also ends a note that is being played by hand

    // Samples: built-in ones are regenerated, the others are read from the path stored in the file.
    for (size_t i = 0; i < doc.channels.size(); ++i)
    {
        const auto& source = doc.sampleSources[i];
        if (source.empty())
            continue;

        if (source.rfind ("builtin:", 0) == 0)
        {
            doc.channels[i].sample = makeBuiltinSample (source.substr (8));
            if (doc.channels[i].sample == nullptr)
            {
                problems.add ("Built-in sound \"" + juce::String (source.substr (8)) + "\" is not known to this version.");
                doc.channels[i].sample = makeMissingSample (source); // silent, but saving keeps the reference
            }
        }
        else
        {
            juce::String error;
            doc.channels[i].sample = decodeSample (formats, juce::File (juce::String (source)), error);
            if (doc.channels[i].sample == nullptr)
            {
                problems.add ("Sample not found: " + juce::String (source));
                doc.channels[i].sample = makeMissingSample (source); // silent, but saving keeps the reference
            }
        }
    }

    // The strips hold references into `channels`, so tear them down first and refill the vector within its capacity.
    rackView.setStrips ({});
    strips.clear();
    channels.clear();
    for (auto& c : doc.channels)
        channels.push_back (std::move (c));
    jassert (channels.size() <= channels.capacity());

    for (size_t t = 0; t < tracks.size() && t < doc.tracks.size(); ++t)
        tracks[t] = doc.tracks[t]; // in place: the mixer strips refer to these objects
    masterTrack.slots = doc.masterSlots;

    arrangement = std::move (doc.arrangement);
    arrangement.patternTicks = kLoopTicks;
    arrangement.ensureChannelCount (channels.size());
    loadAudioClipFiles (problems);
    currentPattern = std::clamp (doc.currentPattern, 0, static_cast<int> (arrangement.patterns.size()) - 1);

    const auto& notes = arrangement.patterns[static_cast<size_t> (currentPattern)].notes;
    for (size_t c = 0; c < channels.size(); ++c)
        channels[c].notes = notes[c];

    for (size_t i = 0; i < channels.size(); ++i)
        createStrip (i);
    refreshRackStrips();

    for (int t = 0; t < kMaxTracks; ++t)
        mixerPanel.refreshTrack (t);
    mixerPanel.refreshTrack (kMasterIndex);

    bpmSlider.setValue (doc.bpm, juce::dontSendNotification);
    playlistView.setBpm (doc.bpm);
    engine.setBpm (doc.bpm);
    volumeSlider.setValue (doc.masterVolume, juce::dontSendNotification);
    engine.setGain (doc.masterVolume);
    mixerPanel.setMasterVolume (doc.masterVolume);

    refreshPatternBox();
    playlistView.setSelectedPattern (currentPattern);
    playlistView.clearHistory(); // the arrangement was changed from outside: undo could not restore it safely
    resetNoteHistory();
    playlistView.repaint();
    setSongMode (doc.songMode);
    selectChannel (0);
    updateTrackRouting();
    publishProject();

    currentProjectFile = file;
    projectDirty = false;
    updateWindowTitle();
}

bool MainComponent::saveProjectTo (const juce::File& file)
{
    const auto error = writeProjectFile (file, makeDocument());
    if (error.isNotEmpty())
    {
        statusLabel.setText ("Could not save: " + error, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not save the project", error);
        return false;
    }

    currentProjectFile = file;
    projectDirty = false;
    addRecentProject (file);
    updateWindowTitle();
    statusLabel.setText ("Saved " + file.getFileName(), juce::dontSendNotification);
    return true;
}

bool MainComponent::saveProject()
{
    if (currentProjectFile != juce::File())
        return saveProjectTo (currentProjectFile);

    // Untitled: save at once under an automatic name, so a quick Ctrl+S never loses work or interrupts the music.
    library.ensureExists();
    return saveProjectTo (automaticProjectFile (library.projects, juce::Time::getCurrentTime()));
}

void MainComponent::saveProjectAs (std::function<void (bool)> whenDone)
{
    library.ensureExists();
    const auto suggested = currentProjectFile != juce::File() ? projectFileForSaveAs (currentProjectFile)
                                                              : library.projects.getChildFile ("My Project" + juce::String (kProjectExtension));

    fileChooser = std::make_unique<juce::FileChooser> ("Save project as", suggested, juce::String ("*") + kProjectExtension);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<MainComponent> (this), whenDone] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr)
            return;
        const auto chosen = chooser.getResult();
        const bool ok = chosen != juce::File() && safe->saveProjectTo (withProjectExtension (chosen));
        if (whenDone)
            whenDone (ok);
    });
}

void MainComponent::newProject()
{
    confirmDiscardChanges ([safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe == nullptr)
            return;
        juce::StringArray problems;
        safe->applyDocument (ProjectDocument::blank(), {}, problems);
        safe->statusLabel.setText ("New project", juce::dontSendNotification);
    });
}

void MainComponent::openProject()
{
    confirmDiscardChanges ([safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe == nullptr)
            return;

        safe->library.ensureExists();
        const auto start = safe->currentProjectFile != juce::File() ? safe->currentProjectFile.getParentDirectory() : safe->library.projects;
        safe->fileChooser = std::make_unique<juce::FileChooser> ("Open a project", start,
                                                                         juce::String ("*") + kProjectExtension + ";*" + kLegacyProjectExtension);
        safe->fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                        [safe] (const juce::FileChooser& chooser)
        {
            if (safe != nullptr && chooser.getResult() != juce::File())
                safe->openProjectFile (chooser.getResult());
        });
    });
}

bool MainComponent::openProjectFile (const juce::File& file)
{
    auto parsed = readProjectFile (file);
    if (! parsed.document.has_value())
    {
        statusLabel.setText ("Could not open " + file.getFileName(), juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Could not open " + file.getFileName(), juce::String (parsed.error));
        return false;
    }

    juce::StringArray problems;
    for (const auto& w : parsed.warnings)
        problems.add (juce::String (w));
    applyDocument (std::move (*parsed.document), file, problems);
    addRecentProject (file);

    if (problems.isEmpty())
    {
        statusLabel.setText ("Opened " + file.getFileName(), juce::dontSendNotification);
    }
    else
    {
        statusLabel.setText ("Opened " + file.getFileName() + " (with problems)", juce::dontSendNotification);
        const auto shown = juce::StringArray (problems.strings.begin(), juce::jmin (problems.size(), 6));
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Opened with problems",
                                                shown.joinIntoString ("\n")
                                                    + (problems.size() > 6 ? "\n... and " + juce::String (problems.size() - 6) + " more" : juce::String()));
    }
    return true;
}

void MainComponent::confirmDiscardChanges (std::function<void()> proceed)
{
    if (! projectDirty)
    {
        proceed();
        return;
    }
    if (discardDialogOpen)
        return;
    discardDialogOpen = true;

    const bool untitled = currentProjectFile == juce::File();
    juce::String message = "Save the changes to \"" + projectName() + "\"?";
    if (untitled)
        message += "\n\nSave stores it as an \"Untitled ...\" file in " + library.projects.getFullPathName()
                 + ". Choose Save As to pick a name.";

    auto* window = new juce::AlertWindow ("Unsaved changes", message, juce::MessageBoxIconType::QuestionIcon);
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    if (untitled)
        window->addButton ("Save As...", 3);
    window->addButton ("Don't save", 2);
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
                             juce::ModalCallbackFunction::create (
                                 [safe = juce::Component::SafePointer<MainComponent> (this), proceed] (int result)
                                 {
                                     if (safe == nullptr)
                                         return;
                                     safe->discardDialogOpen = false;

                                     if (result == 1 && safe->saveProject())
                                         proceed();
                                     else if (result == 3)
                                         safe->saveProjectAs ([proceed] (bool saved) { if (saved) proceed(); });
                                     else if (result == 2)
                                         proceed();
                                 }),
                             true);
}

void MainComponent::showFileMenu()
{
    juce::PopupMenu menu;
    const auto item = [] (int id, const juce::String& text, const juce::String& shortcut, bool enabled = true)
    {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.isEnabled = enabled;
        i.shortcutKeyDescription = shortcut;
        return i;
    };

    menu.addItem (item (1, "New project", "Ctrl+N"));
    menu.addItem (item (2, "Open...", "Ctrl+O"));

    juce::PopupMenu recent;
    auto recentList = recentProjects();
    for (int i = 0; i < recentList.size(); ++i)
        if (juce::File (recentList[i]).existsAsFile())
            recent.addItem (100 + i, juce::File (recentList[i]).getFileName());
    menu.addSubMenu ("Open recent", recent, recent.getNumItems() > 0);

    menu.addSeparator();
    menu.addItem (item (3, "Save", "Ctrl+S"));
    menu.addItem (item (4, "Save As...", "Ctrl+Shift+S"));
    menu.addItem (item (9, "Undo a note edit", "Ctrl+Z", ! noteUndo.empty()));
    menu.addItem (item (10, "Redo a note edit", "Ctrl+Y", ! noteRedo.empty()));
    menu.addSeparator();
    menu.addItem (item (6, "Import audio or video into the playlist...", ""));
    menu.addItem (item (8, "Import a MIDI file into patterns...", ""));
    menu.addSeparator();

    // Export: the same render the engine plays, written as the chosen file type. MP3 and AAC are encoded by ffmpeg.
    const bool haveFfmpeg = findFfmpeg().existsAsFile();
    juce::PopupMenu exportMenu;
    exportMenu.addSectionHeader (songMode ? "Export the song as..." : "Export the pattern as...");
    const auto& exportTypes = exportFormats();
    for (size_t i = 0; i < exportTypes.size(); ++i)
    {
        const bool available = haveFfmpeg || ! exportTypes[i].needsFfmpeg;
        exportMenu.addItem (200 + static_cast<int> (i), juce::String (exportTypes[i].label) + (available ? "" : "  - needs ffmpeg"), available);
    }
    menu.addSubMenu ("Export audio", exportMenu);

    menu.addSeparator();
    menu.addItem (item (5, "Show the projects folder", ""));
    {
        auto startup = item (7, "Play the start-up sound", "");
        startup.isTicked = settings == nullptr || settings->getBoolValue ("startup.sound", true);
        menu.addItem (startup);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this), recentList] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            switch (result)
                            {
                                case 1: safe->newProject(); break;
                                case 2: safe->openProject(); break;
                                case 3: safe->saveProject(); break;
                                case 4: safe->saveProjectAs(); break;
                                case 5: safe->library.ensureExists(); safe->library.projects.revealToUser(); break;
                                case 6: safe->chooseMediaToImport(); break;
                                case 8: safe->chooseMidiToImport(); break;
                                case 9: safe->undoNotes(); break;
                                case 10: safe->redoNotes(); break;
                                case 7:
                                    if (safe->settings != nullptr)
                                    {
                                        safe->settings->setValue ("startup.sound", ! safe->settings->getBoolValue ("startup.sound", true));
                                        safe->settings->saveIfNeeded();
                                    }
                                    break;
                                default:
                                    if (result >= 200 && result - 200 < static_cast<int> (exportFormats().size()))
                                    {
                                        safe->exportAs (exportFormats()[static_cast<size_t> (result - 200)].format);
                                        break;
                                    }
                                    if (result >= 100 && result - 100 < recentList.size())
                                    {
                                        const juce::File file (recentList[result - 100]);
                                        safe->confirmDiscardChanges ([safe, file] { if (safe != nullptr) safe->openProjectFile (file); });
                                    }
                                    break;
                            }
                        });
}
} // namespace sls
