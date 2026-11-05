#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Arrangement.h"
#include "audio/AudioEngine.h"
#include "audio/Project.h"
#include "ui/AudioMeter.h"
#include "library/ChannelPreset.h"
#include "library/Library.h"
#include "library/ProjectFile.h"
#include "ui/ChannelStrip.h"
#include "ui/FileBrowser.h"
#include "audio/AudioExport.h"
#include "ui/MixerPanel.h"
#include "ui/PanelWindow.h"
#include "ui/RightClickButton.h"
#include "ui/PianoRoll.h"
#include "ui/PlaylistView.h"
#include "ui/RackView.h"

namespace sls
{
class MainComponent : public juce::Component, public juce::DragAndDropContainer, private juce::Timer,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    static constexpr int kBrowserWidth = 250;
    static constexpr int kBrowserTabWidth = 28;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void parentHierarchyChanged() override { updateWindowTitle(); } // the title can be set once there is a window

    // Projects. A project is "untitled" until it is saved with a name or opened from a file.
    //   saveProject():   writes to the current file; an untitled project is written at once, without asking, as
    //                    "Untitled <date time>.slp" in the Projects folder.
    //   saveProjectAs(): asks for a name (and folder).
    bool saveProject();
    void saveProjectAs (std::function<void (bool)> whenDone = {});
    void newProject();
    void openProject();                          // asks for a file
    bool openProjectFile (const juce::File& file); // returns false (after telling the user) if it cannot be opened
    bool hasUnsavedChanges() const noexcept { return projectDirty; }

    // Runs `proceed` straight away if there is nothing to lose; otherwise asks "Save / Don't save / Cancel" first.
    void confirmDiscardChanges (std::function<void()> proceed);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override; // the audio device changed: remember it
    void publishProject();
    void selectChannel (int index);
    void setPlaying (bool shouldPlay);
    void openAudioSettings();
    void openMetronomePanel();
    void applyMetronome();                                  // button state, engine and saved settings
    void openAssignTrack (int channelIndex);                 // the "Assign free mixer track" window
    void assignChannelToTrack (int channelIndex, int track); // puts the instrument on that mixer track
    void exportAs (ExportFormat format);                    // File > Export
    void chooseSampleFor (int channelIndex);
    bool loadSampleFile (int channelIndex, const juce::File& file);
    void updateTrackRouting();

    // Project files
    ProjectDocument makeDocument();
    void applyDocument (ProjectDocument document, const juce::File& file, juce::StringArray& problems);
    bool saveProjectTo (const juce::File& file);
    void showFileMenu();
    void markDirty();
    void updateWindowTitle();
    juce::StringArray recentProjects() const;
    void addRecentProject (const juce::File& file);
    juce::String projectName() const;

    // Browser sidebar: sounds dragged (or double-clicked) onto channels. channelIndex -1 = a new channel.
    void setBrowserOpen (bool shouldBeOpen);
    void rebuildBrowser();
    void startPluginScan();
    void applyBrowserItem (int channelIndex, const drag::Item& item);
    bool applyPreset (int channelIndex, const ChannelPreset& preset);
    void savePresetFor (int channelIndex);

    // Channels
    int addChannel (Instrument instrument, bool askForSample = true); // returns the new channel's index, or -1
    void createStrip (size_t channelIndex);
    void setInstrument (int channelIndex, Instrument instrument);
    void renameChannel (int channelIndex);
    void openPianoRoll (int channelIndex);

    // Patterns and the playlist. The channels' `notes` always hold the notes of the current pattern;
    // commitCurrentPattern() copies them into the arrangement before anything is compiled or switched.
    void commitCurrentPattern();
    void selectPattern (int index);
    void addPattern (bool cloneCurrent);
    void refreshPatternBox();
    void setSongMode (bool shouldPlaySong);
    Project compileCurrent();
    std::array<PanelWindow*, 4> allPanels();

    // Panels
    void togglePanel (PanelWindow& panel);
    void showPanel (PanelWindow& panel);
    void resetLayout();
    void restoreLayout();
    void saveLayout();
    void updateToggleButtons();

    AudioEngine engine;
    juce::AudioFormatManager formats;

    // Source of truth for the UI. Never resized after construction, so the strips can hold references.
    std::vector<Channel> channels;
    std::vector<MixerTrack> tracks = Project::defaultTracks(); // the mixer: kMaxTracks tracks with their effect chains
    MixerTrack masterTrack;                                    // only its `slots` are used: the master bus's effect chain
    std::vector<std::unique_ptr<ChannelStrip>> strips;
    int selected = 0;

    Arrangement arrangement;
    int currentPattern = 0;
    bool songMode = false;

    juce::File currentProjectFile;  // empty while the project is untitled
    bool projectDirty = false;      // changed since it was last saved or opened
    bool ignoreDirty = false;       // set while loading, and for view-only changes such as picking a pattern
    bool discardDialogOpen = false;

    // Panel contents first, then the panels that show them, then the workspace holding the panels.
    // Members are destroyed in reverse order, so a panel never outlives the content it points to.
    RackView rackView;
    PianoRoll pianoRoll;
    int liveChannel = 0; // the channel that is sounding a note played by hand
    MixerPanel mixerPanel;
    PlaylistView playlistView { arrangement };

    FileBrowser browser;
    LibraryPaths library = LibraryPaths::defaults();
    std::vector<PluginInfo> foundPlugins;
    PluginScanState pluginScanState = PluginScanState::NotScanned;
    std::unique_ptr<juce::Thread> pluginScanThread;
    bool browserOpen = true;

    PanelWindow rackPanel { "Channel Rack" };
    PanelWindow pianoPanel { "Piano Roll" };
    PanelWindow mixerPanelWindow { "Mixer" };
    PanelWindow playlistPanel { "Playlist" };
    Workspace workspace;

    std::unique_ptr<juce::PropertiesFile> settings;
    juce::Component::SafePointer<juce::DialogWindow> audioDialog;
    juce::Component::SafePointer<juce::DialogWindow> assignDialog;
    bool metronomeOn = false;
    float metronomeVolume = 0.5f;
    bool restoringLayout = false;

    juce::TextButton fileButton { "File" };
    juce::TextButton playButton { "Play" };
    juce::TextButton clearButton { "Clear notes" };
    juce::TextButton modeButton { "Pattern" };
    juce::ComboBox patternBox;
    juce::TextButton patternAddButton { "+" };
    juce::TextButton browserToggle { "Browser F8" };
    juce::TextButton browserTab { juce::String::fromUTF8 ("\xe2\x96\xb6") };
    juce::TextButton rackToggle { "Rack F6" };
    juce::TextButton playlistToggle { "Playlist F5" };
    juce::TextButton pianoToggle { "Piano F7" };
    juce::TextButton mixerToggle { "Mixer F9" };
    juce::TextButton resetButton { "Reset" };
    RightClickButton metronomeButton { "Metronome" };
    juce::TextButton deviceButton { "Audio..." };
    juce::Slider bpmSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label bpmLabel { {}, "BPM" };
    juce::Label volumeLabel { {}, "Master" };
    juce::Label deviceLabel;
    juce::Label statusLabel;
    AudioMeter meter;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace sls
