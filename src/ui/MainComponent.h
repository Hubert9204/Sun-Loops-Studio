#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Arrangement.h"
#include "audio/AudioEngine.h"
#include "audio/NoteRecorder.h"
#include "audio/RecordSetup.h"
#include "audio/Project.h"
#include "audio/TypingKeyboard.h"
#include "ui/AudioMeter.h"
#include "ui/AutomationEditor.h"
#include "ui/DrumMachine.h"
#include "ui/SamplerPanel.h"
#include "ui/SlicerWindow.h"
#include "ui/PitchEditor.h"
#include "audio/StemSplit.h"
#include "ui/HeliosWindow.h"
#include "ui/SynthPanel.h"
#include "ui/SampleEditor.h"
#include "ui/MetersView.h"
#include "library/ChannelPreset.h"
#include "library/Library.h"
#include "library/ProjectFile.h"
#include "ui/ChannelStrip.h"
#include "ui/FileBrowser.h"
#include "audio/AudioExport.h"
#include "ui/MixerPanel.h"
#include "audio/AudioImport.h"
#include "ui/PanelWindow.h"
#include "ui/RightClickButton.h"
#include "ui/PianoRoll.h"
#include "ui/PianoRollToolbar.h"
#include "ui/PluginWindow.h"
#include "plugins/PluginHost.h"
#include "ui/PlaylistView.h"
#include "ui/RackView.h"

namespace sls
{
namespace loopstarter
{
struct Loop;
}

// A pattern is two bars long: the length of the loop in Pattern mode and of one playlist cell.
constexpr int kLoopBars = 2;
constexpr int kLoopTicks = kLoopBars * 4 * kTicksPerBeat;

// A sampler channel's sample: any format the playlist can import, at most 30 seconds of it. On failure: nullptr and `error`.
std::shared_ptr<const SampleData> decodeSample (juce::AudioFormatManager&, const juce::File& file, juce::String& error);

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
    bool keyStateChanged (bool isKeyDown) override;
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
    // Undo / redo of note edits (piano roll, step sequencer, Clear notes, recorded notes) in the patterns. A state is the notes of
    // every channel in one pattern; the state before each edit is kept (the last 200). Not covered: channel settings, the mixer,
    // patterns added or removed, and the playlist (which has its own undo).
    struct NoteState
    {
        int pattern = 0;
        std::vector<std::vector<Note>> notes; // [channel]
        std::vector<int> steps;               // [channel]: the step sequencer's active lengths (0.31.0)
    };
    NoteState captureNoteState() const;
    static bool sameNoteState (const NoteState& a, const NoteState& b);
    void recordNoteChange();  // call after notes may have changed: remembers the earlier state if they did
    void resetNoteHistory();  // call after the notes were replaced wholesale (a project opened, a pattern deleted, a MIDI import)
    void applyNoteState (const NoteState& state);
    void undoNotes();
    void redoNotes();
    std::vector<NoteState> noteUndo, noteRedo;
    NoteState lastNoteState;

    // Undo / redo of every edit (MainComponentHistory.cpp): channels, instruments, kits, notes, patterns, the playlist,
    // the mixer and its effects, the console, tempo and master volume. The playlist keeps its own finer clip history too.
    struct EditState
    {
        juce::String text; // the project file text: two states with the same text sound and look the same
        std::vector<Channel> channels;
        Arrangement arrangement;
        std::vector<MixerTrack> tracks;
        std::array<EffectSlot, kSlotsPerTrack> masterSlots {};
        Console console = Console::defaults();
        int currentPattern = 0;
        int selected = 0;
        double bpm = 120.0;
        double masterVolume = 0.8;
    };
    static constexpr size_t kMaxHistory = 100;
    EditState captureEditState();
    void recordHistory();   // called by publishProject()
    void clearHistory();    // a project was opened or created
    void applyEditState (const EditState& state);
    void undoEdit();
    void redoEdit();
    std::vector<EditState> undoHistory, redoHistory;
    EditState historyStable;   // the state after the last recorded edit
    bool historyReady = false;
    bool restoringHistory = false;
    bool lastEditMouseDown = false;
    juce::uint32 lastEditMs = 0;

    // MainComponentSafety.cpp
    juce::File autosaveFile() const;
    void autosaveIfNeeded();
    void startSession(); // offers the autosaved project if the program did not close normally last time
    void endSession();
    void exportProjectFolder();
    juce::uint32 lastAutosaveMs = 0;

    void refreshRackStrips();
    void applyMetronome();                                  // button state, engine and saved settings
    void openAssignTrack (int channelIndex);                 // the "Assign free mixer track" window
    void assignChannelToTrack (int channelIndex, int track); // puts the instrument on that mixer track
    void exportAs (ExportFormat format);                    // File > Export
    void chooseSampleFor (int channelIndex);
    bool loadSampleFile (int channelIndex, const juce::File& file);
    void updateTrackRouting();

    // Audio clips in the playlist: files dropped or chosen are decoded on a background thread, then placed.
    struct ImportBatch;
    void importMediaFiles (const juce::StringArray& paths, int track, int tick);
    void chooseMediaToImport();
    void chooseMidiToImport();
    void importMidiFile (const juce::File& file); // new channels and patterns from a Standard MIDI File
    void finishImport (const std::shared_ptr<ImportBatch>& batch, const ImportResult& result, const juce::File& file, int track, int tick);
    bool addAudioClip (const std::shared_ptr<const SampleData>& data, int track, int tick, juce::String& note);
    void loadAudioClipFiles (juce::StringArray& problems); // after a project was opened: read the audio its clips refer to

    // Recording the audio input into the playlist
    void toggleRecording();
    void startRecording();
    void finishRecording (const juce::String& why);
    void openRecordMenu();
    void selectPlaylistTrack (int track); // a click on a track's name: moves the arm there when only one track is armed
    void toggleRecordArm (int track);
    void applyRecordSetup (bool announce = true); // shows the arms on the playlist and saves them
    void showTrackInputMenu (int track);
    void addRecordInputItems (juce::PopupMenu& menu, int track);
    bool handleRecordInputItem (int result);
    juce::String recordInputLabel (int track) const;
    juce::String armedTracksText() const;
    void updateRecordButton();
    void showRecordProgress();

    // Recording what a MIDI keyboard plays into the piano roll (the selected channel, in the current pattern)
    void toggleNoteRecording();
    void applyNoteRecording();
    void openNoteRecordMenu();
    void drainMidiTakes();
    // 0.29.0: the number of mixer tracks (within kDefaultTracks .. kMaxTracks; new ones are default tracks named "Track N").
    // Resizes `tracks` in place (no reallocation) and brings the mixer, the channel strips and the routing up to date.
    void setTrackCount (size_t count);
    void addMixerTracks (int howMany);
    void removeLastMixerTrack(); // only when nothing uses it
    void drainScoreEvents();               // 0.28.0: into scoreLog
    void dumpScoreLog (bool lastPhraseOnly); // the score log's notes into the current pattern
    void endNoteTake();

    // Project files
    // withPluginStates: the plugins' own settings are asked for (saving, autosave); without, a plugin is named by its instance only
    // (the undo history compares documents after every edit and must stay cheap).
    ProjectDocument makeDocument (bool withPluginStates = true);
    void openFlpProject();   // File > Open an FL Studio project (0.34.0)
    void exportFlpProject(); // File > Export as FL Studio project
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

    // Plugins (0.17.0, MainComponentPlugins.cpp)
    void initPlugins();                           // before the audio device starts: the known list, the device-rate hook
    juce::File pluginSettingsFile (const juce::String& name) const;
    void refreshFoundPlugins();                   // foundPlugins from the host's known list
    bool findPluginDescription (const juce::String& identifier, juce::PluginDescription& out) const;
    std::shared_ptr<JucePlugin> createPlugin (const juce::String& identifier, juce::String& error);
    std::shared_ptr<JucePlugin> duplicatePlugin (const HostedPlugin& original); // a second instance with the same settings
    void setPluginInstrument (int channelIndex, const juce::String& identifier); // -1: a new channel
    void addPluginEffectForChannel (int channelIndex, const juce::String& identifier); // first free slot of the channel's track
    void openPluginWindow (const std::shared_ptr<HostedPlugin>& plugin);
    void openChannelPlugin (int channelIndex);
    void closeUnusedPluginWindows();              // after every edit: a plugin that left the project loses its window
    bool projectUsesPlugin (const HostedPlugin* plugin) const;
    void addPluginSpecs (ProjectDocument& doc, bool withState) const;
    void loadDocumentPlugins (ProjectDocument& doc, juce::StringArray& problems);
    bool projectHasPlugins() const;
    // An export renders with the same plugins: the live engine is suspended and they are prepared for the export's rate meanwhile.
    void beginPluginExport (double sampleRate);
    void endPluginExport();
    void applyBrowserItem (int channelIndex, const drag::Item& item);
    void previewBrowserItem (const drag::Item& item); // one click on a sound in the browser (0.31.0)
    juce::String previewKey;                          // the sound last previewed, kept decoded so clicking it again starts at once
    std::shared_ptr<const SampleData> previewSound;
    bool applyPreset (int channelIndex, const ChannelPreset& preset);
    void savePresetFor (int channelIndex);

    // Channels
    int addChannel (Instrument instrument, bool askForSample = true); // returns the new channel's index, or -1
    void createStrip (size_t channelIndex);
    void setInstrument (int channelIndex, Instrument instrument);
    void renameChannel (int channelIndex);
    void openPianoRoll (int channelIndex);
    void remapChannels (const std::vector<int>& newToOld, int newSelected); // MainComponentChannels.cpp
    void deleteChannel (int channelIndex);
    void moveChannel (int channelIndex, int delta);
    void cloneChannel (int channelIndex);
    // MainComponentSampler.cpp
    void openSamplerSettings (int channelIndex);
    void closeSamplerSettings();
    void applySfz (int channelIndex, const juce::File& file); // 0.41.0
    // The Slicer (0.37.0, MainComponentSlicer.cpp)
    bool canSlice (int channelIndex) const;
    void openSlicer (int channelIndex);
    void closeSlicer();
    void writeSlicerScore (int channelIndex);
    void chooseLoopForSlicer();
    bool sliceLoopFile (const juce::File& file);
    void openSynthSettings (int channelIndex);  // MainComponentSynth.cpp (0.18.0)
    void closeSynthSettings();
    void chooseSoundFontFor (int channelIndex);
    void chooseSoundFontPreset (int channelIndex, const juce::File& file);
    void applySoundFont (int channelIndex, const juce::File& file, int presetIndex);
    juce::String slicesToSampler (const std::shared_ptr<const SampleData>& edited, const std::vector<double>& markers, const juce::String& name);

    // Patterns and the playlist. The channels' `notes` always hold the notes of the current pattern;
    // commitCurrentPattern() copies them into the arrangement before anything is compiled or switched.
    void commitCurrentPattern();
    void selectPattern (int index);
    void addPattern (bool cloneCurrent);
    void showNewPatternMenu (juce::Component& target);
    // 0.30.0: the Loop Starter window, and the loop it made turned into channels (reused by name) and a new pattern
    void openLoopStarter();
    void applyLoop (const loopstarter::Loop& loop, std::uint32_t variation, bool setTempo);
    int loopStarterGenre = 0, loopStarterKey = 9, loopStarterParts = 0; // the last choices (A, all parts)
    std::uint32_t loopStarterVariation = 0;
    void setPatternLength (int bars);
    void setPatternBeats (int beats); // 0.42.0: the Channel Rack's + / - (one quarter at a time)
    void showPatternLength(); // the editors follow arrangement.patternTicks
    void renamePattern (int index);
    void deletePattern (int index);
    void loadPatternIntoChannels (int index);
    void refreshPatternBox();
    void dropOnPlaylist (const drag::Item& item, int track, int tick);
    void setSongMode (bool shouldPlaySong);
    Project compileCurrent();
    void seekSong (int tick); // the playlist's ruler was clicked: move the song position there (switches to Song mode)
    int patternStartTick = 0; // where a pattern starts playing (and Stop returns to) in Pattern mode (0.20.0)
    void seekPattern (int tick);
    int songStartTick = 0;    // where the song starts playing (and Stop returns to) in Song mode

    // Automation (MainComponentAutomation.cpp).
    EffectType effectTypeFor (const AutomationTarget& t) const;
    float currentValueOf (const AutomationTarget& t) const;
    std::shared_ptr<HostedPlugin> pluginFor (const AutomationTarget& t) const; // the plugin a plugin target points at (0.35.0)
    juce::String automationLabelFor (const AutomationTarget& t) const;
    juce::String automationValueText (const AutomationTarget& t, float v) const;
    void createAutomationFor (const AutomationTarget& target); // right click on a control
    void openAutomationEditor (int clipIndex);
    void closeAutomationEditor();
    void toggleMeters();
    std::array<PanelWindow*, 4> allPanels();

    // Panels
    void togglePanel (PanelWindow& panel);
    void showPanel (PanelWindow& panel);
    void resetLayout();
    void restoreLayout();
    void saveLayout();
    void updateToggleButtons();

    // Declared before the engine and everything that can hold a plugin: plugin instances must go before the formats that made them.
    std::unique_ptr<PluginHost> pluginHost = std::make_unique<PluginHost>();
    AudioEngine engine;
    juce::AudioFormatManager formats;

    // Source of truth for the UI. Never resized after construction, so the strips can hold references.
    std::vector<Channel> channels;
    // The mixer tracks (kDefaultTracks .. kMaxTracks) with their effect chains. Capacity kMaxTracks: the mixer strips hold references.
    std::vector<MixerTrack> tracks = Project::defaultTracks();
    MixerTrack masterTrack;                                    // only its `slots` are used: the master bus's effect chain
    Console console = Console::defaults();                     // the aux buses and the VCA groups
    std::vector<std::unique_ptr<ChannelStrip>> strips;
    int selected = 0;

    Arrangement arrangement;
    std::unique_ptr<AutomationEditorWindow> automationWindow;
    std::unique_ptr<SampleEditorWindow> sampleEditorWindow;
    void openSampleEditorForClip (int audioClipIndex);
    void openPitchEditorForClip (int audioClipIndex);                 // 0.38.0, MainComponentPitch.cpp
    void splitClipIntoStems (int audioClipIndex);                     // 0.39.0, MainComponentStems.cpp
    void useStems (int audioClipIndex, const AudioClip& clip, Stems stems);
    // Helios (0.40.0, MainComponentHelios.cpp): the manual searched offline, and commands it can run.
    void openHelios (const juce::String& question = {});
    const HelpIndex& helpIndex();
    std::vector<HeliosCommand> heliosCommands();
    juce::String pitchNotesToPattern (const std::vector<pitchtrack::Note>& notes);
    void openSampleEditorForChannel (int channelIndex);
    std::unique_ptr<DrumMachineWindow> drumWindow;
    std::unique_ptr<SamplerWindow> samplerWindow;
    int samplerWindowChannel = -1;
    std::unique_ptr<SlicerWindow> slicerWindow;
    std::unique_ptr<PitchEditorWindow> pitchEditorWindow;
    std::unique_ptr<HeliosWindow> heliosWindow;
    std::unique_ptr<HelpIndex> help; // built the first time Helios opens
    int slicerWindowChannel = -1;
    std::unique_ptr<SynthWindow> synthWindow;
    int synthWindowChannel = -1;
    int drumWindowChannel = -1;
    void openDrumMachine (int channelIndex);
    void closeDrumMachine();
    static std::shared_ptr<const DrumKit> defaultDrumKit();
    int automationEditing = -1;     // index of the clip the window shows
    int currentPattern = 0;
    bool songMode = false;

    juce::File currentProjectFile;  // empty while the project is untitled
    bool projectDirty = false;      // changed since it was last saved or opened
    bool ignoreDirty = false;       // set while loading, and for view-only changes such as picking a pattern
    bool discardDialogOpen = false;

    // Panel contents first, then the panels that show them, then the workspace holding the panels.
    // Members are destroyed in reverse order, so a panel never outlives the content it points to.
    RackView rackView;
    RackBody rackBody { rackView }; // the rack with its pattern-length bar (0.42.0)
    PianoRoll pianoRoll;
    PianoRollToolbar pianoToolbar { pianoRoll };
    PianoRollEditor pianoEditor { pianoToolbar, pianoRoll }; // the toolbar above the roll: the piano roll panel's content
    int liveChannel = 0; // the channel that is sounding a note played by hand
    MixerPanel mixerPanel;
    PlaylistView playlistView { arrangement };

    FileBrowser browser;
    LibraryPaths library = LibraryPaths::defaults();
    std::vector<PluginInfo> foundPlugins;
    PluginScanState pluginScanState = PluginScanState::NotScanned;
    std::unique_ptr<juce::Thread> pluginScanThread;
    std::vector<std::unique_ptr<PluginWindow>> pluginWindows;
    int pluginPollTicks = 0;
    bool browserOpen = true;

    PanelWindow rackPanel { "Channel Rack" };
    PanelWindow pianoPanel { "Piano Roll" };
    PanelWindow mixerPanelWindow { "Mixer" };
    PanelWindow playlistPanel { "Playlist" };
    Workspace workspace;

    std::unique_ptr<juce::PropertiesFile> settings;
    juce::Component::SafePointer<juce::DialogWindow> audioDialog;
    juce::Component::SafePointer<juce::DialogWindow> assignDialog;
    bool recording = false;
    RecordSetup recordSetup;                                         // armed playlist tracks and their inputs (0.36.0)
    bool recordFromPlayhead = false;                                 // takes start where the song is (punch in), not at its start
    std::vector<int> recordingTracks;                                // the take being recorded: a track per engine stream
    bool monitorInput = false;
    juce::String midiNote;                                           // last status text set for MIDI activity
    enum class RecordTarget { PlaylistTrack, SamplerChannel };
    RecordTarget recordTarget = RecordTarget::PlaylistTrack;        // where a recorded take goes
    bool noteRecordOn = false;                                       // MIDI keyboard notes are written into the pattern
    int noteQuantize = kTicksPerBeat / 4;                            // ticks; 0 = do not quantize
    NoteRecorder noteRecorder;
    ScoreLog scoreLog; // what a MIDI keyboard played in the last five minutes (0.28.0)
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
    juce::TextButton metersButton { "Meters" };
    juce::TextButton heliosButton { "Helios" };
    // The computer keyboard as a MIDI keyboard (0.32.0, see TypingKeyboard.h): notes go to the selected channel like a MIDI
    // keyboard's (so Rec notes and the score log take them too).
    juce::TextButton typingButton { "Typing keys" };
    bool typingOn = true;
    int typingOctave = 0;
    int typingVelocity = 100;
    struct HeldTypingKey
    {
        int keyCode;
        int pitch; // -1 for a command key (octave, velocity): held so that the key's repeat does nothing
    };
    std::vector<HeldTypingKey> typingHeld;
    bool handleTypingKey (const juce::KeyPress& key);
    void releaseTypingKeys (bool all); // note off for the keys let go (all: every key)
    void setTypingOn (bool on);
    std::unique_ptr<MetersWindow> metersWindow;
    RightClickButton metronomeButton { "Metronome" };
    RightClickButton recordButton { "Record" };
    RightClickButton noteRecordButton { "Rec notes" };
    juce::TextButton recordMenuButton { juce::String::fromUTF8 ("\xe2\x96\xbe") };
    juce::TextButton deviceButton { "Audio..." };
    juce::Slider bpmSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label bpmLabel { {}, "BPM" };
    juce::Label volumeLabel { {}, "Master" };
    juce::Label deviceLabel;
    juce::Label statusLabel;
    AudioMeter meter;

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::ThreadPool importPool { 1 }; // declared last: it is stopped first when the window closes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace sls
