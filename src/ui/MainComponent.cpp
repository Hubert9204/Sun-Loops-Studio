#include "ui/MainComponent.h"

#include <spdlog/spdlog.h>

#include "audio/TimeFormat.h"

#include "ui/AssignTrackDialog.h"
#include "ui/AudioSettings.h"
#include "ui/MetronomePanel.h"
#include "ui/TextPrompt.h"

#include <functional>
#include <map>
#include <optional>

#include <juce_audio_utils/juce_audio_utils.h>

#include "audio/BuiltinSamples.h"
#include "audio/SoundLibrary.h"
#include "library/DemoSong.h"
#include "library/DrumKitFile.h"
#include "audio/ClipEdit.h"
#include "audio/MidiImport.h"
#include "audio/OfflineRenderer.h"

namespace sls
{
namespace
{
constexpr const char* kAudioStateKey = "audio.device"; // the audio device and buffer size, as JUCE saves them
constexpr const char* kDemoShownKey = "demo.shown";    // the demo song was opened at a start (0.53.0: only the first one)
}

// A sampler channel's sample: any format the playlist can import, at most 30 seconds of it.
std::shared_ptr<const SampleData> decodeSample (juce::AudioFormatManager&, const juce::File& file, juce::String& error)
{
    auto imported = importAudioFile (file);
    if (imported.data == nullptr)
    {
        error = imported.error;
        return nullptr;
    }

    std::shared_ptr<const SampleData> sample = imported.data;
    const int maxFrames = static_cast<int> (sample->sampleRate * 30.0);
    if (sample->numFrames() > maxFrames)
    {
        auto cut = std::make_shared<SampleData> (*sample);
        cut->left.resize (static_cast<size_t> (maxFrames));
        cut->right.resize (static_cast<size_t> (maxFrames));
        sample = cut;
    }
    return sample;
}

MainComponent::MainComponent()
{
    formats.registerBasicFormats();

    juce::PropertiesFile::Options settingsOptions;
    settingsOptions.applicationName = "SunLoopsStudio";
    settingsOptions.filenameSuffix = ".settings";
#if JUCE_LINUX
    settingsOptions.folderName = ".config/SunLoopsStudio"; // JUCE puts it directly under ~ on Linux otherwise
#else
    settingsOptions.folderName = "SunLoopsStudio";
#endif
    settingsOptions.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (settingsOptions);

    // The strips and the mixer hold references into this vector, so it must never reallocate:
    // reserve the maximum up front and never grow past it.
    // A blank project to build the window around; the demo song (0.52.0) replaces it once the plugins are known (loadDemoSong).
    auto blank = ProjectDocument::blank();
    channels = blank.channels;
    channels.reserve (static_cast<size_t> (kMaxChannels));
    tracks.reserve (static_cast<size_t> (kMaxTracks)); // grows in place: the mixer strips refer to the tracks
    for (size_t i = 0; i < channels.size(); ++i)
        channels[i].mixerTrack = static_cast<int> (i);

    library.ensureExists(); // ~/Music/Sun Loops Studio/{Samples,Drum Kits,Presets}: where the browser looks
    arrangement = blank.arrangement;

    for (size_t i = 0; i < channels.size(); ++i)
        createStrip (i);

    refreshRackStrips();
    rackView.onAddChannel = [this] { showAddMenu (rackView); };
    rackBody.onOptionsMenu = [this] (juce::Component& target) { showRackOptionsMenu (target); }; // 0.60.0
    rackBody.onGroupChosen = [this] (const juce::String& group) // 0.62.0
    {
        rackGroup = group;
        refreshRackStrips();
    };
    rackBody.stepGraph().onEdited = [this]
    {
        if (selected >= 0 && selected < static_cast<int> (channels.size()))
        {
            pianoRoll.setNotes (channels[static_cast<size_t> (selected)].notes);
            strips[static_cast<size_t> (selected)]->repaintGrid();
        }
        publishProject();
    };

    rackView.onItemDroppedOnEmptyArea = [this] (const drag::Item& item) { applyBrowserItem (-1, item); };

    mixerPanel.setTracks (tracks);
    mixerPanel.onAddTracks = [this] (int howMany) { addMixerTracks (howMany); };
    mixerPanel.onRemoveLastTrack = [this] { removeLastMixerTrack(); };
    mixerPanel.setMaster (masterTrack);
    mixerPanel.setConsole (console);
    mixerPanel.onTrackChanged = [this] (int) { publishProject(); };
    mixerPanel.onAutomate = [this] (const AutomationTarget& t) { createAutomationFor (t); };
    mixerPanel.listEffectPlugins = [this]
    {
        std::vector<FxPanel::PluginChoice> list;
        for (const auto& p : foundPlugins)
            if (! p.isInstrument)
                list.push_back ({ p.name + "  (" + p.format + ")", p.identifier });
        return list;
    };
    mixerPanel.createEffectPlugin = [this] (const juce::String& id) -> std::shared_ptr<HostedPlugin>
    {
        juce::String error;
        auto plugin = createPlugin (id, error);
        if (plugin == nullptr)
            statusLabel.setText ("Plugin not loaded: " + error, juce::dontSendNotification);
        return plugin;
    };
    mixerPanel.openPluginWindow = [this] (const std::shared_ptr<HostedPlugin>& p) { openPluginWindow (p); };
    mixerPanel.onChainMenu = [this] (juce::Component& b, MixerTrack& t, int i) { showEffectChainMenu (b, t, i); };
    mixerPanel.onMasterVolumeChanged = [this] (double v) { volumeSlider.setValue (v); }; // the slider drives the engine
    updateTrackRouting();

    fileButton.setTooltip ("New, open and save projects (Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S)");
    fileButton.onClick = [this] { showFileMenu(); };
    // The menu bar (0.60.0), laid out as FL Studio's.
    editButton.setTooltip ("Undo, redo, clear the selected channel's notes");
    editButton.onClick = [this] { showEditMenu(); };
    addButton.setTooltip ("Add a channel: an instrument or a plugin");
    addButton.onClick = [this] { showAddMenu (addButton); };
    patternsButton.setTooltip ("New, clone, rename, delete patterns, their length, the Loop Starter");
    patternsButton.onClick = [this] { showPatternsMenu(); };
    viewButton.setTooltip ("Open or close the windows, the browser and the meters; reset the layout");
    viewButton.onClick = [this] { showViewMenu(); };
    optionsButton.setTooltip ("Audio and MIDI settings, metronome, recording, typing keyboard, start-up options");
    optionsButton.onClick = [this] { showOptionsMenu(); };
    toolsButton.setTooltip ("Helios, Loop Starter, Slicer, imports, exports, FL Studio projects");
    toolsButton.onClick = [this] { showToolsMenu(); };
    hintLabel.setFont (juce::FontOptions (12.0f));
    hintLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb9bfc9));
    hintLabel.setJustificationType (juce::Justification::centredLeft);
    hintLabel.setMinimumHorizontalScale (0.75f);
    hintLabel.setInterceptsMouseClicks (false, false);

    playButton.setClickingTogglesState (true);
    playButton.setTooltip ("Play (Space)");
    playButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3f9f5f));
    playButton.onClick = [this] { setPlaying (playButton.getToggleState()); };
    stopButton.setTooltip ("Stop (Space while playing): the position goes back to where Play started");
    stopButton.onClick = [this] { setPlaying (false); };
    patternPrevButton.setTooltip ("Previous pattern");
    patternPrevButton.onClick = [this] { if (currentPattern > 0) selectPattern (currentPattern - 1); };
    patternNextButton.setTooltip ("Next pattern");
    patternNextButton.onClick = [this] { if (currentPattern + 1 < static_cast<int> (arrangement.patterns.size())) selectPattern (currentPattern + 1); };
    timePanel.setTooltip ("Song position: bar.beat.tick and minutes:seconds");
    // The global snap (0.60.0): the Piano roll and the Playlist follow it where their own snap says "Main", as in FL Studio.
    snapLabel.setFont (juce::FontOptions (12.0f));
    snapLabel.setJustificationType (juce::Justification::centredRight);
    {
        const char* names[] = { "Bar", "1/2", "Beat (1/4)", "1/8", "1/16", "1/32", "1/64", "Off" };
        for (int i = 0; i < 8; ++i)
            snapBox.addItem (names[i], i + 1);
    }
    snapBox.setSelectedId (settings != nullptr ? std::clamp (settings->getIntValue ("snap.main", 5), 1, 8) : 5, juce::dontSendNotification);
    snapBox.setTooltip ("Global snap: the grid the Piano roll and the Playlist use while their own snap says \"Main\"");
    snapBox.setWantsKeyboardFocus (false);
    snapBox.onChange = [this] { applyMainSnap(); };
    cpuLabel.setFont (juce::FontOptions (11.0f));
    cpuLabel.setJustificationType (juce::Justification::centred);
    cpuLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb9bfc9));
    cpuLabel.setTooltip ("CPU: how much of each audio buffer's time the audio takes. Near 100 % the sound crackles: use a larger buffer.");
    cpuLabel.setText ("CPU 0 %", juce::dontSendNotification);

    // The Plugin picker (0.60.0): instruments and effects in one window, like FL Studio's.
    pluginPicker.onInstrumentChosen = [this] (const PluginPicker::Entry& e) { applyBrowserItem (-1, e.item); };
    pluginPicker.onEffectChosen = [this] (const PluginPicker::Entry& e)
    {
        if (e.pluginId.isNotEmpty())
            addPluginEffectForChannel (selected, e.pluginId);
        else
            addBuiltinEffect (e.effect);
    };
    pluginPicker.onRescan = [this] { startPluginScan(); };
    pluginPickerToggle.setTooltip ("The Plugin picker: every instrument and effect, to add with a double-click or a drag");
    pluginPickerToggle.onClick = [this] { togglePanel (pluginPanel); };
    pianoToolbar.onChannelChosen = [this] (int channel) { if (channel != selected) openPianoRoll (channel); };

    modeButton.setClickingTogglesState (true);
    modeButton.setTooltip ("Pattern: loop the selected pattern. Song: play the playlist.");
    modeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8943a));
    modeButton.onClick = [this] { setSongMode (modeButton.getToggleState()); };

    patternBox.setTooltip ("Current pattern");
    patternBox.onChange = [this] { selectPattern (patternBox.getSelectedId() - 1); };

    patternAddButton.setTooltip ("New pattern, Loop Starter, pattern length");
    patternAddButton.onClick = [this] { showNewPatternMenu (patternAddButton); };

    clearButton.onClick = [this]
    {
        channels[static_cast<size_t> (selected)].notes.clear();
        pianoRoll.setNotes ({});
        strips[static_cast<size_t> (selected)]->repaint();
        publishProject();
    };
    deviceButton.onClick = [this] { openAudioSettings(); };

    // Metronome: click toggles it; right-click opens tempo, volume and tap tempo.
    metronomeButton.setClickingTogglesState (false);
    metronomeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8913a));
    metronomeButton.setTooltip ("Metronome on/off (right-click: tempo, volume, tap tempo)");
    metronomeButton.onClick = [this]
    {
        metronomeOn = ! metronomeOn;
        applyMetronome();
    };
    metronomeButton.onRightClick = [this] { openMetronomePanel(); };

    // Record: click to record the audio input into the chosen playlist track, click again (or Stop) to finish.
    recordButton.setClickingTogglesState (false);
    recordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9433b));
    recordButton.setTooltip ("Record the audio input (microphone, guitar...) into the playlist track marked in red. "
                             "The song plays from the start while you record. Right-click or use the arrow for options.");
    recordButton.onClick = [this] { toggleRecording(); };
    recordButton.onRightClick = [this] { openRecordMenu(); };
    recordMenuButton.setTooltip ("Recording options: track, input, monitoring");
    recordMenuButton.onClick = [this] { openRecordMenu(); };
    playlistView.onTrackSelected = [this] (int track) { selectPlaylistTrack (track); };
    playlistView.onTrackArmToggled = [this] (int track) { toggleRecordArm (track); };
    playlistView.onTrackInputMenu = [this] (int track) { showTrackInputMenu (track); };
    playlistView.onTrackRemoved = [this] (int track)
    {
        recordSetup.removeTrack (track);
        applyRecordSetup();
    };

    // Rec notes: what a MIDI keyboard plays while the song runs is written into the selected channel's pattern.
    noteRecordButton.setClickingTogglesState (false);
    noteRecordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9433b));
    noteRecordButton.setTooltip ("Record the notes of a MIDI keyboard into the piano roll: switch it on, press Play and play. "
                                 "The notes go to the selected channel, in the current pattern, and are quantized "
                                 "(right-click to choose how). Right-click also for the score log: whatever the keyboard "
                                 "played in the last 5 minutes, even without recording, can be put into the pattern.");
    noteRecordButton.onClick = [this] { toggleNoteRecording(); };
    noteRecordButton.onRightClick = [this] { openNoteRecordMenu(); };

    bpmSlider.setRange (40.0, 240.0, 1.0);
    bpmSlider.setValue (kDemoSongBpm, juce::dontSendNotification);
    engine.setBpm (kDemoSongBpm);
    bpmSlider.onValueChange = [this] { engine.setBpm (bpmSlider.getValue()); playlistView.setBpm (bpmSlider.getValue()); markDirty(); recordHistory(); };
    meterButton.onClick = [this] { showTempoMenu(); };
    meterButton.setMouseClickGrabsKeyboardFocus (false);
    meterButton.setWantsKeyboardFocus (false);

    volumeSlider.setRange (0.0, 1.0, 0.01);
    volumeSlider.setValue (0.8, juce::dontSendNotification);
    volumeSlider.onValueChange = [this]
    {
        engine.setGain (static_cast<float> (volumeSlider.getValue()));
        mixerPanel.setMasterVolume (volumeSlider.getValue());
        markDirty();
        recordHistory();
    };
    mixerPanel.setMasterVolume (volumeSlider.getValue());

    // Notes played by hand with the mouse in the piano roll sound through the selected channel. The channel is
    // remembered so the matching "off" goes to the channel that got the "on", even if the selection changes.
    pianoRoll.onNoteStarted = [this] (int pitch)
    {
        liveChannel = selected;
        engine.liveNoteOn (liveChannel, pitch);
    };
    pianoRoll.onNoteStopped = [this] (int pitch) { engine.liveNoteOff (liveChannel, pitch); };

    pianoRoll.setLoopLengthTicks (currentPatternTicks());
    pianoRoll.onSeek = [this] (int tick) { seekPattern (tick); };
    pianoRoll.onStatus = [this] (const juce::String& text) { statusLabel.setText (text, juce::dontSendNotification); };
    if (settings != nullptr) // the velocity lane's height, as the user left it (0.54.0)
        pianoRoll.setVelocityLaneHeight (settings->getIntValue ("pianoroll.velocityHeight", 70));
    pianoRoll.onVelocityLaneHeightChanged = [this] (int height)
    {
        if (settings != nullptr)
        {
            settings->setValue ("pianoroll.velocityHeight", height);
            settings->saveIfNeeded();
        }
    };
    pianoRoll.onLoopChanged = [this] (int start, int end)
    {
        commitCurrentPattern();
        if (currentPattern < 0 || currentPattern >= static_cast<int> (arrangement.patterns.size()))
            return;
        auto& pattern = arrangement.patterns[static_cast<size_t> (currentPattern)];
        pattern.loopStartTick = start;
        pattern.loopEndTick = end;
        if (pattern.hasLoop())
        {
            statusLabel.setText ("Piano roll loop region: 16ths " + juce::String (start / (kTicksPerBeat / 4) + 1) + " to "
                                     + juce::String (end / (kTicksPerBeat / 4)) + " (Pattern mode repeats it)",
                                 juce::dontSendNotification);
            if (! engine.isPlaying())
                seekPattern (start); // stopped: Play starts at the region
            else if (songMode)
                setSongMode (false);
        }
        else
            statusLabel.setText ("Piano roll loop region deleted", juce::dontSendNotification);
        publishProject();
    };
    pianoToolbar.getOtherChannels = [this]
    {
        // the other channels' notes in the current pattern, for a bassline that follows their chords (drum machines left out)
        std::vector<PianoRollToolbar::OtherChannel> list;
        for (size_t i = 0; i < channels.size(); ++i)
            if (static_cast<int> (i) != selected && channels[i].instrument != Instrument::Drums)
                list.push_back ({ juce::String (channels[i].name), channels[i].notes });
        return list;
    };
    pianoRoll.onNotesChanged = [this]
    {
        channels[static_cast<size_t> (selected)].notes = pianoRoll.getNotes();
        strips[static_cast<size_t> (selected)]->repaint();
        // A note written into the spare quarter after the end makes the pattern one quarter longer (0.58.0); setPatternBeats
        // publishes, so the note and the new length are one undo step.
        if (const int wanted = pianoRoll.takeLengthRequest(); wanted > currentPatternTicks())
        {
            const int before = currentPatternTicks();
            setPatternBeats (wanted / kTicksPerBeat);
            if (currentPatternTicks() > before)
                return;
        }
        publishProject();
    };

    playlistView.onChanged = [this]
    {
        markDirty();
        if (songMode)
            publishProject();
    };
    playlistView.onPatternPicked = [this] (int pattern) { selectPattern (pattern); };
    playlistView.onEditSample = [this] (int index) { openSampleEditorForClip (index); };
    playlistView.onEditPitch = [this] (int index) { openPitchEditorForClip (index); };
    playlistView.onSplitStems = [this] (int index) { splitClipIntoStems (index); };
    playlistView.onMakeUnique = [this] (int clip) { makeClipUnique (clip); }; // 0.62.0
    playlistView.onConsolidate = [this] (int track, int from, int to) { consolidateClips (track, from, to); };
    playlistView.onStatus = [this] (const juce::String& text) { statusLabel.setText (text, juce::dontSendNotification); };
    playlistView.onSeek = [this] (int tick) { seekSong (tick); };
    playlistView.onLoopChanged = [this]
    {
        if (! songMode)
            setSongMode (true); // the loop region belongs to the song
        publishProject();
        if (arrangement.hasLoop())
        {
            statusLabel.setText ("Loop region " + juce::String (formatBarsBeatsTicks (arrangement.loopStartTick)) + " to "
                                     + juce::String (formatBarsBeatsTicks (arrangement.loopEndTick)) + " (Song mode repeats it)",
                                 juce::dontSendNotification);
            if (! engine.isPlaying())
                seekSong (arrangement.loopStartTick); // stopped: Play starts at the region
        }
        else
            statusLabel.setText ("Loop region deleted: the whole song plays", juce::dontSendNotification);
    };
    playlistView.setAutomationLabel ([this] (const AutomationTarget& t) { return automationLabelFor (t); });
    playlistView.setAutomationPicked ([this] (int clip) { openAutomationEditor (clip); });
    playlistView.onPatternSelected = [this] (int pattern) { selectPattern (pattern); };
    playlistView.onNewPattern = [this] { showNewPatternMenu (playlistView); };
    playlistView.onRenamePattern = [this] (int pattern) { renamePattern (pattern); };
    playlistView.onDeletePattern = [this] (int pattern) { deletePattern (pattern); };
    playlistView.onItemDropped = [this] (const drag::Item& item, int track, int tick) { dropOnPlaylist (item, track, tick); };
    playlistView.onFilesDropped = [this] (const juce::StringArray& files, int track, int tick) { importMediaFiles (files, track, tick); };
    playlistView.setBpm (bpmSlider.getValue());

    // Panels
    rackPanel.setContent (&rackBody);
    rackBody.onPatternBeats = [this] (int beats) { setPatternBeats (beats); };
    rackBody.setPatternBeats (currentPatternTicks() / kTicksPerBeat, arrangement.minPatternBeats(), arrangement.meter);
    rackPanel.setMinimumSize (420, 130);
    pianoPanel.setContent (&pianoEditor);
    pianoPanel.setMinimumSize (420, 260);
    mixerPanelWindow.setContent (&mixerPanel);
    mixerPanelWindow.setMinimumSize (720, 440);
    playlistPanel.setContent (&playlistView);
    playlistPanel.setMinimumSize (520, 340);
    pluginPanel.setContent (&pluginPicker);
    pluginPanel.setMinimumSize (480, 320);

    for (auto* panel : allPanels())
    {
        workspace.addPanel (*panel);
        panel->onLayoutChanged = [this] (PanelWindow&)
        {
            saveLayout();
            updateToggleButtons();
        };
    }

    browser.onItemActivated = [this] (const drag::Item& item)
    {
        // A double-clicked plugin instrument gets a channel of its own instead of replacing the selected one.
        applyBrowserItem (item.kind == "plugin" && item.payload.startsWith ("instrument|") ? -1 : selected, item);
    };
    browser.onItemPreview = [this] (const drag::Item& item) { previewBrowserItem (item); };
    browser.onAction = [this] (const juce::String& action) { browserAction (action); };
    // 0.62.0: the favourites live in the settings (one per line: kind, payload, name)
    if (settings != nullptr)
    {
        std::vector<BrowserNode> favourites;
        for (const auto& line : juce::StringArray::fromLines (settings->getValue ("browser.favourites")))
        {
            const auto fields = juce::StringArray::fromTokens (line, "\t", "");
            if (fields.size() == 3 && fields[1].isNotEmpty())
                favourites.push_back ({ fields[2].toStdString(), static_cast<NodeKind> (std::clamp (fields[0].getIntValue(), 1, 5)), fields[1].toStdString(), {} });
        }
        browser.setFavourites (std::move (favourites));
        browser.setTab (settings->getIntValue ("browser.tab", 0));
    }
    browser.onFavouritesChanged = [this]
    {
        if (settings == nullptr)
            return;
        juce::StringArray lines;
        for (const auto& f : browser.getFavourites())
            lines.add (juce::String (static_cast<int> (f.kind)) + "\t" + juce::String (f.payload) + "\t" + juce::String (f.name));
        settings->setValue ("browser.favourites", lines.joinIntoString ("\n"));
        settings->saveIfNeeded();
    };
    browser.onTabChanged = [this] (int tab)
    {
        if (settings != nullptr)
            settings->setValue ("browser.tab", tab);
    };
    browser.onRefresh = [this] { rebuildBrowser(); };
    browser.onOpenFolder = [this] { library.ensureExists(); library.root.revealToUser(); };
    browser.onCollapse = [this] { setBrowserOpen (false); };
    browserTab.setTooltip ("Show the browser (F8)");
    browserTab.onClick = [this] { setBrowserOpen (true); };
    browserToggle.onClick = [this] { setBrowserOpen (! browserOpen); };
    browserToggle.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8943a));

    rackToggle.onClick = [this] { togglePanel (rackPanel); };
    playlistToggle.onClick = [this] { togglePanel (playlistPanel); };
    pianoToggle.onClick = [this] { togglePanel (pianoPanel); };
    mixerToggle.onClick = [this] { togglePanel (mixerPanelWindow); };
    metersButton.onClick = [this] { toggleMeters(); };
    helpButton.onClick = [this] { showHelpMenu(); };
    helpButton.setTooltip ("Help: Helios, the user guide, the Discord community, the GitHub page, updates, crash reports, About");
    heliosButton.onClick = [this] { openHelios(); };
    heliosButton.setTooltip ("Helios (F1): ask how to do something; searches the manual on this computer (no AI, no internet)");
    heliosButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff5a3d1e));
    // 0.62.0: the master pitch (as FL Studio's main pitch knob) and step editing
    pitchLabel.setFont (juce::FontOptions (12.0f));
    pitchLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb9bfc9));
    pitchSlider.setRange (-12.0, 12.0, 1.0);
    pitchSlider.setDoubleClickReturnValue (true, 0.0);
    pitchSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    pitchSlider.textFromValueFunction = [] (double v) { return (v > 0.0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; };
    pitchSlider.setTooltip ("Master pitch: every note of the synths, samplers and plugin instruments moves by this many semitones "
                            "(drum channels keep their pitch). Double-click: 0");
    pitchSlider.onValueChange = [this]
    {
        masterPitch = static_cast<float> (pitchSlider.getValue());
        publishProject();
    };
    stepEditButton.setClickingTogglesState (false);
    stepEditButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9433b));
    stepEditButton.setTooltip ("Step editing: while the song is stopped, each note you play (typing keys or a MIDI keyboard) is written into "
                               "the pattern at its start marker, which then moves on by the snap. Keys held together make a chord.");
    stepEditButton.onClick = [this]
    {
        stepEditOn = ! stepEditOn;
        stepEditHeld = 0;
        stepEditButton.setToggleState (stepEditOn, juce::dontSendNotification);
        statusLabel.setText (stepEditOn ? "Step editing is on: stop the song and play; the notes go to the pattern's start marker (click the piano roll's ruler to move it)"
                                        : "Step editing is off",
                             juce::dontSendNotification);
    };
    for (auto* c : std::initializer_list<juce::Component*> { &pitchLabel, &pitchSlider, &stepEditButton })
        addAndMakeVisible (c);

    typingButton.setClickingTogglesState (false);
    typingButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8943a));
    typingButton.setTooltip ("Play the selected channel from the computer keyboard: A S D F G H J K L are the white keys from C5, "
                             "W E T Y U O P the black keys; Z / X an octave down / up, C / V softer / louder");
    typingButton.onClick = [this] { setTypingOn (! typingOn); };
    typingButton.setToggleState (typingOn, juce::dontSendNotification);
    metersButton.setTooltip ("Loudness (LUFS), true peak, spectrum and goniometer of the output");
    resetButton.onClick = [this] { resetLayout(); };
    resetButton.setTooltip ("Restore the default panel layout");

    for (auto* t : { &rackToggle, &playlistToggle, &pianoToggle, &mixerToggle, &pluginPickerToggle })
        t->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8943a));

    deviceLabel.setJustificationType (juce::Justification::centredLeft);
    deviceLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setFont (juce::FontOptions (12.0f));

    // Space and F5/F6/F7/F9 are handled by this component, so these controls must not steal keyboard focus.
    for (juce::Button* b : std::initializer_list<juce::Button*> { &fileButton, &editButton, &addButton, &patternsButton, &viewButton, &optionsButton, &toolsButton,
                     &playButton, &stopButton, &clearButton, &modeButton, &patternPrevButton, &patternNextButton, &patternAddButton, &deviceButton, &metronomeButton, &recordButton, &recordMenuButton, &noteRecordButton,
                     &browserToggle, &browserTab, &rackToggle, &playlistToggle, &pianoToggle, &mixerToggle, &pluginPickerToggle, &resetButton, &metersButton, &typingButton, &heliosButton, &helpButton })
        b->setMouseClickGrabsKeyboardFocus (false);
    patternBox.setMouseClickGrabsKeyboardFocus (false);
    patternBox.setWantsKeyboardFocus (false);

    for (auto* c : std::initializer_list<juce::Component*> { &fileButton, &editButton, &addButton, &patternsButton, &viewButton, &optionsButton, &toolsButton, &helpButton, &hintLabel,
                                                              &playButton, &stopButton, &modeButton, &patternPrevButton, &patternBox, &patternNextButton, &patternAddButton,
                                                              &deviceButton, &metronomeButton, &recordButton, &recordMenuButton, &noteRecordButton,
                                                              &browserToggle, &rackToggle, &playlistToggle, &pianoToggle,
                                                              &mixerToggle, &pluginPickerToggle, &resetButton, &metersButton, &typingButton, &heliosButton, &bpmSlider, &volumeSlider, &meterButton,
                                                              &bpmLabel, &volumeLabel, &deviceLabel, &statusLabel, &meter, &timePanel, &snapLabel, &snapBox, &cpuLabel,
                                                              &browser, &browserTab, &workspace })
        addAndMakeVisible (c);
    applyMainSnap();
    refreshPluginPicker();

    setWantsKeyboardFocus (true);
    browserOpen = settings == nullptr || settings->getBoolValue ("browser.open", true);
    setSize (1280, 860);
    setBrowserOpen (browserOpen);
    rebuildBrowser();

    refreshPatternBox();
    playlistView.setSelectedPattern (currentPattern);
    selectChannel (0);
    restoreLayout();
    publishProject(); // before the device starts, so the first callback already has the project
    projectDirty = false;
    updateWindowTitle();

    std::unique_ptr<juce::XmlElement> savedAudio;
    if (settings != nullptr)
        savedAudio = settings->getXmlValue (kAudioStateKey);
    if (settings != nullptr)
    {
        // Before 0.36.0 one track was recorded, from input 1 or inputs 1+2: that is where a first start of 0.36.0 begins.
        const int oldTrack = std::clamp (settings->getIntValue ("record.track", 0), 0, kDefaultGenericTracks - 1);
        if (settings->containsKey ("record.arms"))
            recordSetup = RecordSetup::fromString (settings->getValue ("record.arms").toStdString(), oldTrack);
        else
        {
            recordSetup.armOnly (oldTrack);
            recordSetup.tracks[static_cast<size_t> (oldTrack)].stereo = settings->getBoolValue ("record.stereo", false);
        }
        recordFromPlayhead = settings->getBoolValue ("record.fromPlayhead", false);
        countInBars = std::clamp (settings->getIntValue ("record.countInBars", 0), 0, 2);
        recordLoopTakes = settings->getBoolValue ("record.loopTakes", true);
        monitorInput = settings->getBoolValue ("record.monitor", false);
        metronomeOn = settings->getBoolValue ("metronome.on", false);
        typingOn = settings->getBoolValue ("typing.on", true);
        typingButton.setToggleState (typingOn, juce::dontSendNotification);
        metronomeVolume = static_cast<float> (settings->getDoubleValue ("metronome.volume", 0.5));
        noteQuantize = std::clamp (settings->getIntValue ("midirec.quantize", kTicksPerBeat / 4), 0, kTicksPerBeat);
        recordTarget = settings->getIntValue ("record.target", 0) == 1 ? RecordTarget::SamplerChannel : RecordTarget::PlaylistTrack;
    }
    applyMetronome();
    applyNoteRecording();
    if (recordSetup.armedTracks().empty())
        recordSetup.armOnly (0);
    applyRecordSetup (false);
    engine.setInputMonitoring (monitorInput);
    engine.setMidiTargetChannel (selected);
    engine.setStartupSound (makeStartupSample());
    initPlugins(); // before the device starts: it prepares the plugins for its rate
    // The demo song opens on the first start only (0.53.0); afterwards the program starts with a new, empty project. A project
    // recovered after a crash replaces either in startSession().
    if (settings == nullptr || ! settings->getBoolValue (kDemoShownKey, false))
    {
        loadDemoSong();
        if (settings != nullptr)
        {
            settings->setValue (kDemoShownKey, true);
            settings->saveIfNeeded();
        }
    }
    else
    {
        juce::StringArray problems;
        applyDocument (ProjectDocument::blank(), {}, problems);
    }
    const auto error = engine.initialise (savedAudio.get());
    if (error.isEmpty() && (settings == nullptr || settings->getBoolValue ("startup.sound", true)))
        engine.playStartupSound();
    engine.getDeviceManager().addChangeListener (this); // from now on the device and buffer size are remembered
    deviceLabel.setText (error.isEmpty() ? engine.describeDevice() : "Audio error: " + error,
                         juce::dontSendNotification);
    noteAudioDevice();
    if (error.isNotEmpty())
        spdlog::warn ("Audio error: {}", error.toStdString());

    lastAutosaveMs = juce::Time::getMillisecondCounter();
    startSession();
    startTimerHz (30);
    // The update check (0.43.0): a few seconds after the start, in the background, unless switched off in the Help menu.
    juce::Timer::callAfterDelay (4000, [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr && safe->checksForUpdatesAutomatically())
            safe->checkForUpdates (false);
    });

    // Space and F5/F6/F7/F9 work from the start, without needing a click first.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
                                     { if (safe != nullptr) safe->grabKeyboardFocus(); });
}

// Hands the strips to the rack (after strips were created or removed) and enables or disables "add channel".
// 0.62.0: the rack shows the channels of the chosen filter group (all of them without one).
juce::StringArray MainComponent::rackGroups() const
{
    juce::StringArray groups;
    for (const auto& c : channels)
        if (! c.group.empty())
            groups.addIfNotAlreadyThere (juce::String (c.group));
    groups.sortNatural();
    return groups;
}

void MainComponent::refreshRackStrips()
{
    const auto groups = rackGroups();
    if (! groups.contains (rackGroup))
        rackGroup.clear(); // the group is gone (its last channel left it)
    rackBody.setGroups (groups, rackGroup);
    std::vector<ChannelStrip*> stripPointers;
    stripPointers.reserve (strips.size());
    for (size_t i = 0; i < strips.size(); ++i)
        if (rackGroup.isEmpty() || (i < channels.size() && juce::String (channels[i].group) == rackGroup))
            stripPointers.push_back (strips[i].get());
    rackView.setStrips (std::move (stripPointers));
    rackView.setAddChannelEnabled (channels.size() < static_cast<size_t> (kMaxChannels));
    refreshStepGraph();
}

void MainComponent::refreshStepGraph()
{
    const bool valid = selected >= 0 && selected < static_cast<int> (channels.size());
    rackBody.stepGraph().setChannel (valid ? &channels[static_cast<size_t> (selected)] : nullptr, currentPatternTicks());
}

MainComponent::~MainComponent()
{
    stopTimer();
    updateChecker.reset(); // cancels a check in progress
    endSession();
    automationWindow.reset();
    sampleEditorWindow.reset();
    pitchEditorWindow.reset(); // before the engine it plays through
    heliosWindow.reset();
    slicerWindow.reset();
    drumWindow.reset();
    samplerWindow.reset();
    synthWindow.reset();
    metersWindow.reset();
    performanceWindow.reset();
    engine.getDeviceManager().removeChangeListener (this);
    audioDialog.deleteAndZero(); // it refers to the engine
    assignDialog.deleteAndZero();
    saveLayout();
    if (pluginScanThread != nullptr)
        pluginScanThread->stopThread (5000);
    pluginWindows.clear(); // editors go before their plugins
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2d34));
}

// The toolbar (0.60.0), laid out as FL Studio's: the menu bar with the hint bar; the transport, pattern, tempo, time, global
// snap, master and CPU panels; the shortcut panel (the windows, in FL Studio's order) with the recording panel's switches; then
// the status line, the browser and the workspace.
void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);
    const auto gap = [] (juce::Rectangle<int>& r, int w) { r.removeFromLeft (w); };

    // --- the menu bar and the hint bar
    auto menus = area.removeFromTop (26);
    const struct { juce::TextButton* b; int w; } menuButtons[] = { { &fileButton, 48 }, { &editButton, 48 }, { &addButton, 46 }, { &patternsButton, 80 },
                                                                  { &viewButton, 52 }, { &optionsButton, 70 }, { &toolsButton, 58 }, { &helpButton, 50 } };
    for (const auto& m : menuButtons)
    {
        m.b->setBounds (menus.removeFromLeft (m.w));
        gap (menus, 2);
    }
    gap (menus, 12);
    hintLabel.setBounds (menus);

    area.removeFromTop (6);

    // --- the transport, pattern, tempo, time, snap, master and CPU panels
    auto top = area.removeFromTop (30);
    playButton.setBounds (top.removeFromLeft (52));
    gap (top, 3);
    stopButton.setBounds (top.removeFromLeft (52));
    gap (top, 3);
    recordButton.setBounds (top.removeFromLeft (62));
    recordMenuButton.setBounds (top.removeFromLeft (22));
    gap (top, 3);
    noteRecordButton.setBounds (top.removeFromLeft (72));
    gap (top, 3);
    modeButton.setBounds (top.removeFromLeft (70));
    gap (top, 12);
    patternPrevButton.setBounds (top.removeFromLeft (22));
    gap (top, 2);
    patternBox.setBounds (top.removeFromLeft (126));
    gap (top, 2);
    patternNextButton.setBounds (top.removeFromLeft (22));
    gap (top, 2);
    patternAddButton.setBounds (top.removeFromLeft (26));
    gap (top, 12);
    bpmLabel.setBounds (top.removeFromLeft (38));
    bpmSlider.setBounds (top.removeFromLeft (96));
    gap (top, 3);
    meterButton.setBounds (top.removeFromLeft (40));
    gap (top, 10);
    timePanel.setBounds (top.removeFromLeft (122));
    gap (top, 10);
    snapLabel.setBounds (top.removeFromLeft (36));
    gap (top, 3);
    snapBox.setBounds (top.removeFromLeft (84));
    gap (top, 10);
    volumeLabel.setBounds (top.removeFromLeft (48));
    volumeSlider.setBounds (top.removeFromLeft (96));
    gap (top, 3);
    meter.setBounds (top.removeFromLeft (14));
    gap (top, 10);
    cpuLabel.setBounds (top.removeFromLeft (64));

    area.removeFromTop (6);

    // --- the shortcut panel (the windows, in FL Studio's order) and the recording panel's switches
    auto views = area.removeFromTop (28);
    deviceButton.setBounds (views.removeFromRight (70));
    views.removeFromRight (4);
    heliosButton.setBounds (views.removeFromRight (64));
    views.removeFromRight (4);
    metersButton.setBounds (views.removeFromRight (64));
    views.removeFromRight (4);
    typingButton.setBounds (views.removeFromRight (90));
    views.removeFromRight (4);
    metronomeButton.setBounds (views.removeFromRight (90));
    views.removeFromRight (4);
    stepEditButton.setBounds (views.removeFromRight (74)); // 0.62.0
    views.removeFromRight (6);
    pitchSlider.setBounds (views.removeFromRight (76).reduced (0, 3));
    pitchLabel.setBounds (views.removeFromRight (36));
    const struct { juce::TextButton* b; int w; } shortcuts[] = { { &playlistToggle, 96 }, { &pianoToggle, 104 }, { &rackToggle, 120 },
                                                                { &mixerToggle, 82 }, { &browserToggle, 94 }, { &pluginPickerToggle, 100 } };
    for (const auto& sc : shortcuts)
    {
        sc.b->setBounds (views.removeFromLeft (sc.w));
        gap (views, 4);
    }
    gap (views, 8);
    resetButton.setBounds (views.removeFromLeft (56));

    area.removeFromTop (6);
    auto info = area.removeFromTop (20);
    statusLabel.setBounds (info.removeFromRight (info.getWidth() / 3));
    deviceLabel.setBounds (info);

    area.removeFromTop (6);
    if (browserOpen)
    {
        browser.setBounds (area.removeFromLeft (kBrowserWidth));
        area.removeFromLeft (6);
    }
    else
    {
        browserTab.setBounds (area.removeFromLeft (kBrowserTabWidth));
        area.removeFromLeft (6);
    }
    workspace.setBounds (area);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown())
    {
        const auto letter = juce::CharacterFunctions::toUpperCase (static_cast<juce::juce_wchar> (key.getKeyCode()));
        const bool shift = key.getModifiers().isShiftDown();
        if (letter == 'S') { if (shift) saveProjectAs(); else saveProject(); return true; }
        if (letter == 'N' && ! shift) { newProject(); return true; }
        if (letter == 'O' && ! shift) { openProject(); return true; }
    }

    // Ctrl+Z / Ctrl+Y undo and redo in the playlist when the mouse is over it, otherwise in the notes.
    const bool overPlaylist = playlistPanel.isOpen() && playlistView.isShowing() && playlistView.isMouseOver (true);
    if (! overPlaylist && key.getModifiers().isCommandDown())
    {
        const auto letter = juce::CharacterFunctions::toUpperCase (static_cast<juce::juce_wchar> (key.getKeyCode()));
        if (letter == 'Z') { if (key.getModifiers().isShiftDown()) redoEdit(); else undoEdit(); return true; }
        if (letter == 'Y') { redoEdit(); return true; }
    }

    // With the mouse over the playlist its keys (Delete, Ctrl+Z/Y/C/X/V/D, 1..5 for the tools, +/- to zoom) work first.
    if (overPlaylist && playlistView.handleKey (key))
        return true;

    if (key == juce::KeyPress::escapeKey && pianoRoll.cancelLoopMode())
        return true;

    // With the mouse over the piano roll its keys (Delete, Ctrl+A/C/X/V/D, arrows, Escape) work on its selection.
    if (pianoPanel.isOpen() && pianoRoll.isShowing() && pianoRoll.isMouseOver (true) && pianoRoll.handleKey (key))
        return true;

    if (handleTypingKey (key))
        return true;

    if (key == juce::KeyPress::spaceKey)
    {
        setPlaying (! engine.isPlaying());
        return true;
    }
    if (key == juce::KeyPress::F5Key) { togglePanel (playlistPanel); return true; }
    if (key == juce::KeyPress::F6Key) { togglePanel (rackPanel); return true; }
    if (key == juce::KeyPress::F7Key) { togglePanel (pianoPanel); return true; }
    if (key == juce::KeyPress::F8Key) { setBrowserOpen (! browserOpen); return true; }
    if (key == juce::KeyPress::F9Key) { togglePanel (mixerPanelWindow); return true; }
    if (key == juce::KeyPress::F1Key) { openHelios(); return true; }
    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    releaseTypingKeys (false);
    return false;
}

bool MainComponent::handleTypingKey (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (! typingOn || mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    const auto c = static_cast<char32_t> (key.getTextCharacter());
    const int code = key.getKeyCode();
    for (const auto& h : typingHeld)
        if (h.keyCode == code)
            return true; // the key's auto-repeat

    if (const auto command = typing::commandFor (c); command != typing::Command::None)
    {
        typingHeld.push_back ({ code, -1 });
        if (command == typing::Command::OctaveDown || command == typing::Command::OctaveUp)
            typingOctave = std::clamp (typingOctave + (command == typing::Command::OctaveUp ? 1 : -1), typing::kMinOctave, typing::kMaxOctave);
        else
            typingVelocity = typing::nextVelocity (typingVelocity, command == typing::Command::VelocityUp);
        statusLabel.setText ("Typing keyboard: A = " + juce::String (theory::noteName (typing::pitchFor ('a', typingOctave)))
                                 + ", velocity " + juce::String (typingVelocity),
                             juce::dontSendNotification);
        return true;
    }

    const int pitch = typing::pitchFor (c, typingOctave);
    if (pitch < 0)
        return false;
    engine.handleMidi (juce::MidiMessage::noteOn (1, pitch, static_cast<juce::uint8> (typingVelocity)));
    typingHeld.push_back ({ code, pitch });
    return true;
}

void MainComponent::releaseTypingKeys (bool all)
{
    for (auto it = typingHeld.begin(); it != typingHeld.end();)
    {
        if (all || ! juce::KeyPress::isKeyCurrentlyDown (it->keyCode))
        {
            if (it->pitch >= 0)
                engine.handleMidi (juce::MidiMessage::noteOff (1, it->pitch));
            it = typingHeld.erase (it);
        }
        else
            ++it;
    }
}

void MainComponent::setTypingOn (bool on)
{
    typingOn = on;
    if (! on)
        releaseTypingKeys (true);
    typingButton.setToggleState (on, juce::dontSendNotification);
    if (settings != nullptr)
        settings->setValue ("typing.on", on);
    statusLabel.setText (on ? "Typing keyboard on: A S D F... play the selected channel from C5" : "Typing keyboard off", juce::dontSendNotification);
}

void MainComponent::timerCallback()
{
    flushHistory();
    autosaveIfNeeded();
    logStatusChanges();
    updateToolbarReadouts(); // 0.60.0: the time and CPU panels, the hint bar
    performanceTick(); // 0.62.0
    if (int pausedAt = 0; engine.takeMarkerPause (pausedAt)) // 0.62.0: a Pause marker stopped the transport: stay there
    {
        setPlaying (false);
        seekSong (pausedAt);
        statusLabel.setText ("Paused at a marker: Play goes on from here", juce::dontSendNotification);
    }
    if (! typingHeld.empty())
        releaseTypingKeys (! juce::Process::isForegroundProcess()); // keys let go while another window had the keyboard

    // A plugin's settings changed in its own window: the project needs saving (plugin edits are not in the undo history).
    if (++pluginPollTicks >= 10)
    {
        pluginPollTicks = 0;
        for (const auto& p : pluginHost->livePlugins())
            if (p->takeChanged() && projectUsesPlugin (p.get()))
                markDirty();
    }

    followAutomation();
    const float masterPeak = engine.consumePeak();
    meter.setLevel (masterPeak);

    // Mixer tracks: peak since the last tick, plus RMS. Only drawn while the mixer is open.
    const bool mixerShown = mixerPanelWindow.isOpen();
    mixerPanel.setMasterLevels (mixerShown ? masterPeak : 0.0f, mixerShown ? engine.getMasterRms() : 0.0f);
    for (int t = 0; t < static_cast<int> (tracks.size()); ++t)
    {
        const float trackPeak = engine.consumeTrackPeak (t);
        if (mixerShown)
            mixerPanel.setTrackLevels (t, trackPeak, engine.getTrackRms (t));
    }
    for (int a = 0; a < kAuxBuses; ++a)
    {
        const float auxPeak = engine.consumeAuxPeak (a);
        if (mixerShown)
            mixerPanel.setAuxLevels (a, auxPeak, engine.getAuxRms (a));
    }

    // In pattern mode the playhead is a position inside the pattern (piano roll, rack). In song mode it is a position in the song,
    // on the playlist; the piano roll, the rack and the drum machine show where the current pattern is while one of its clips plays
    // (0.58.0; before, nothing).
    const int playhead = engine.isPlaying() ? engine.getPlayheadTick() : -1;
    const int patternPlayhead = ! songMode ? playhead : playhead >= 0 ? arrangement.patternTickAt (currentPattern, playhead) : -1;
    pianoRoll.setPlayheadTick (patternPlayhead);
    playlistView.setPlayheadTick (songMode ? engine.getPlayheadTick() : -1); // stopped: where Play will start
    if (drumWindow != nullptr)
        drumWindow->view().setPlayheadTick (patternPlayhead);

    for (auto& strip : strips)
    {
        // consumeChannelPeak resets the peak, so read it once and hand it to both views of the channel.
        strip->setMeterLevel (engine.consumeChannelPeak (strip->getIndex()));
        strip->setPlayheadTick (patternPlayhead);
    }

    {
        auto text = engine.describeDevice();
        const auto midi = engine.describeMidiInputs();
        if (midi.isNotEmpty())
            text += "  |  MIDI: " + midi;
        deviceLabel.setText (text, juce::dontSendNotification);
    }

    if (engine.consumeMidiActivity())
    {
        const auto note = "MIDI keyboard plays " + juce::String (channels[static_cast<size_t> (selected)].name);
        if (! recording && note != midiNote)
        {
            midiNote = note;
            statusLabel.setText (note, juce::dontSendNotification);
        }
    }

    if (recording)
        showRecordProgress();

    drainMidiTakes();
    drainScoreEvents();
    engine.serviceEffectMemory();
    mixerPanel.setTimeShaperShortfall (engine.timeShapersWithoutMemory());

    // When the window is active but keyboard focus is outside this component (e.g. on the window
    // itself), take it so Space and F5/F6/F7/F9 work without a click first.
    if (auto* peer = getPeer(); peer != nullptr && peer->isFocused() && ! hasKeyboardFocus (true))
        grabKeyboardFocus();
}

// ------------------------------------------------------------------------- Panels

namespace
{
// "panel2": the layout changed with the browser and the expanded mixer, so layouts stored by older versions are ignored.
constexpr const char* kPanelKeys[] = { "panel2.rack", "panel2.piano", "panel2.mixer", "panel2.playlist", "panel2.plugins" }; // 0.60.0: the picker

constexpr int kRackHeight = 280;

// The channel rack across the top with the playlist below it. The piano roll (closed, as in FL Studio: it opens
// from a channel) takes the playlist's place; the mixer opens as an expanded view over everything (F9).
PanelLayout defaultLayoutFor (int panelIndex, int workW, int workH)
{
    const int fullW = workW - 16;
    const int lowerY = kRackHeight + 16;
    const int lowerH = std::max (200, workH - lowerY - 8);
    switch (panelIndex)
    {
        case 0:  return { 8, 8, fullW, kRackHeight, true };
        case 1:  return { 8, lowerY, fullW, lowerH, false };
        case 2:  return { 8, 8, fullW, std::max (440, workH - 16), false };
        case 4:  return { std::max (8, (workW - 760) / 2), std::max (8, (workH - 480) / 2), std::min (760, fullW), std::min (480, std::max (320, workH - 16)), false }; // the Plugin picker (0.60.0)
        default: return { 8, lowerY, fullW, lowerH, true };
    }
}
} // namespace

std::array<PanelWindow*, 5> MainComponent::allPanels()
{
    return { &rackPanel, &pianoPanel, &mixerPanelWindow, &playlistPanel, &pluginPanel }; // same order as kPanelKeys
}

void MainComponent::togglePanel (PanelWindow& panel)
{
    if (! panel.isOpen())
        showPanel (panel);
    else if (! panel.isActive())
        workspace.activate (&panel);   // open but buried or inactive: bring it up
    else
        panel.setOpen (false);         // open and on top: close it

    saveLayout();
    updateToggleButtons();
}

void MainComponent::showPanel (PanelWindow& panel)
{
    panel.setOpen (true);
    panel.clampToParent();
    workspace.activate (&panel);
}

void MainComponent::updateToggleButtons()
{
    rackToggle.setToggleState (rackPanel.isOpen(), juce::dontSendNotification);
    playlistToggle.setToggleState (playlistPanel.isOpen(), juce::dontSendNotification);
    pianoToggle.setToggleState (pianoPanel.isOpen(), juce::dontSendNotification);
    mixerToggle.setToggleState (mixerPanelWindow.isOpen(), juce::dontSendNotification);
    pluginPickerToggle.setToggleState (pluginPanel.isOpen(), juce::dontSendNotification);
}

void MainComponent::restoreLayout()
{
    restoringLayout = true;

    const auto panels = allPanels();
    for (size_t i = 0; i < panels.size(); ++i)
    {
        const auto stored = settings != nullptr
                                ? PanelLayout::parse (settings->getValue (kPanelKeys[i]).toStdString())
                                : std::nullopt;
        panels[i]->applyLayout (stored.value_or (defaultLayoutFor (static_cast<int> (i), workspace.getWidth(),
                                                                   workspace.getHeight())));
    }

    restoringLayout = false;
    workspace.activate (rackPanel.isOpen() ? &rackPanel : nullptr);
    updateToggleButtons();
}

void MainComponent::resetLayout()
{
    restoringLayout = true;

    const auto panels = allPanels();
    for (size_t i = 0; i < panels.size(); ++i)
        panels[i]->applyLayout (defaultLayoutFor (static_cast<int> (i), workspace.getWidth(), workspace.getHeight()));

    restoringLayout = false;
    workspace.activate (&rackPanel);
    saveLayout();
    updateToggleButtons();
}

void MainComponent::saveLayout()
{
    if (restoringLayout || settings == nullptr)
        return;

    const auto panels = allPanels();
    for (size_t i = 0; i < panels.size(); ++i)
        settings->setValue (kPanelKeys[i], juce::String (panels[i]->currentLayout().toString()));

    settings->saveIfNeeded();
}

// ------------------------------------------------------------------------- Channels

void MainComponent::createStrip (size_t channelIndex)
{
    auto strip = std::make_unique<ChannelStrip> (channels[channelIndex], static_cast<int> (channelIndex));
    strip->setTrackCount (static_cast<int> (tracks.size()));
    strip->setLoopLengthTicks (currentPatternTicks());
    strip->onSelect = [this] (int idx) { selectChannel (idx); };
    strip->onChanged = [this] (int idx)
    {
        if (idx == selected)
            pianoRoll.setNotes (channels[static_cast<size_t> (idx)].notes);
        updateTrackRouting();
        publishProject();
    };
    strip->onAutomate = [this] (int idx, AutomationKind kind) { createAutomationFor ({ kind, idx, 0, kind == AutomationKind::ChannelPlugin ? -1 : 0 }); };
    // 0.62.0: filter groups, colours, the Layer's children
    strip->listGroups = [this] { return rackGroups(); };
    strip->listChannelNames = [this]
    {
        juce::StringArray names;
        for (const auto& c : channels)
            names.add (juce::String (c.name));
        return names;
    };
    strip->onLooksChanged = [this] (int)
    {
        markDirty();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
                                         {
                                             if (safe != nullptr)
                                                 safe->refreshRackStrips(); // (not from inside the strip's own menu callback)
                                         });
    };
    strip->onNewGroup = [this] (int idx)
    {
        promptForText (*this, "New filter group", "Name of the group (for example Drums, Bass, Leads):", {}, "OK",
                       [safe = juce::Component::SafePointer<MainComponent> (this), idx] (const juce::String& typed)
                       {
                           const auto name = typed.trim().substring (0, 24);
                           if (safe == nullptr || name.isEmpty() || idx >= static_cast<int> (safe->channels.size()))
                               return;
                           safe->channels[static_cast<size_t> (idx)].group = name.toStdString();
                           safe->markDirty();
                           safe->refreshRackStrips();
                       });
    };
    strip->onSavePreset = [this] (int idx) { savePresetFor (idx); };
    strip->onAssignTrack = [this] (int idx) { openAssignTrack (idx); };
    strip->onItemDropped = [this] (int idx, const drag::Item& item) { applyBrowserItem (idx, item); };
    strip->onLoadSample = [this] (int idx) { chooseSampleFor (idx); };
    strip->onEditSample = [this] (int idx) { openSampleEditorForChannel (idx); };
    strip->onOpenPianoRoll = [this] (int idx) { openPianoRoll (idx); };
    strip->onOpenDrumMachine = [this] (int idx) { openDrumMachine (idx); };
    strip->onOpenPlugin = [this] (int idx) { openChannelPlugin (idx); };
    strip->listInstrumentPlugins = [this]
    {
        std::vector<std::pair<juce::String, juce::String>> list;
        for (const auto& p : foundPlugins)
            if (p.isInstrument)
                list.emplace_back (p.name + "  (" + p.format + ")", p.identifier);
        return list;
    };
    strip->onPluginChosen = [this] (int idx, const juce::String& id) { setPluginInstrument (idx, id); };
    strip->onInstrumentChosen = [this] (int idx, Instrument instrument)
    {
        setInstrument (idx, instrument);
        if (instrument == Instrument::Drums)
            openDrumMachine (idx);
    };
    strip->onRename = [this] (int idx) { renameChannel (idx); };
    strip->onSamplerSettings = [this] (int idx) { openSamplerSettings (idx); };
    strip->onOpenSlicer = [this] (int idx) { openSlicer (idx); };
    strip->onSynthSettings = [this] (int idx) { openSynthSettings (idx); };
    strip->onLoadSoundFont = [this] (int idx) { chooseSoundFontFor (idx); };
    strip->onDelete = [this] (int idx) { deleteChannel (idx); };
    strip->onClone = [this] (int idx) { cloneChannel (idx); };
    strip->onMove = [this] (int idx, int delta) { moveChannel (idx, delta); };
    strips.push_back (std::move (strip));
}

int MainComponent::addChannel (Instrument instrument, bool askForSample)
{
    if (channels.size() >= static_cast<size_t> (kMaxChannels))
        return -1;

    commitCurrentPattern();

    Channel c;
    c.instrument = instrument;
    c.name = (instrument == Instrument::Synth ? "Synth " : instrument == Instrument::Drums ? "Drums " : instrument == Instrument::Plugin ? "Plugin "
              : instrument == Instrument::Layer ? "Layer " : "Sampler ")
           + std::to_string (channels.size() + 1);
    if (instrument == Instrument::Drums)
        c.kit = defaultDrumKit();
    c.mixerTrack = std::min (static_cast<int> (channels.size()), static_cast<int> (tracks.size()) - 1); // next track
    jassert (channels.size() < channels.capacity()); // push_back must not reallocate (strips hold references)
    channels.push_back (std::move (c));
    arrangement.ensureChannelCount (channels.size());

    createStrip (channels.size() - 1);

    refreshRackStrips();

    const int index = static_cast<int> (channels.size()) - 1;
    updateTrackRouting();
    selectChannel (index);
    publishProject();

    if (instrument == Instrument::Sampler && askForSample)
        chooseSampleFor (index);

    return index;
}

void MainComponent::setInstrument (int channelIndex, Instrument instrument)
{
    auto& channel = channels[static_cast<size_t> (channelIndex)];
    channel.instrument = instrument;
    if (instrument != Instrument::Plugin)
        channel.plugin = nullptr; // its window closes with the next publish
    if (instrument == Instrument::Drums && channel.kit == nullptr)
        channel.kit = defaultDrumKit();

    strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
    updateTrackRouting();
    publishProject();

    if (instrument == Instrument::Sampler && channel.sample == nullptr)
        chooseSampleFor (channelIndex); // a sampler without a sample is silent: ask for one right away
}

void MainComponent::renameChannel (int channelIndex)
{
    promptForText (*this, "Rename channel", "Name:", juce::String (channels[static_cast<size_t> (channelIndex)].name), "OK",
                   [safe = juce::Component::SafePointer<MainComponent> (this), channelIndex] (const juce::String& typed)
                   {
                       const auto name = typed.trim().substring (0, 16);
                       if (safe == nullptr || name.isEmpty())
                           return;

                       auto* self = safe.getComponent();
                       self->channels[static_cast<size_t> (channelIndex)].name = name.toStdString();
                       self->strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
                       self->updateTrackRouting();
                       if (channelIndex == self->selected)
                           self->selectChannel (channelIndex);
                       self->publishProject();
                   });
}

void MainComponent::openPianoRoll (int channelIndex)
{
    selectChannel (channelIndex);
    showPanel (pianoPanel);
    saveLayout();
    updateToggleButtons();
}

// ------------------------------------------------------------------------- Project

void MainComponent::publishProject()
{
    markDirty();
    if (drumWindow != nullptr)
        drumWindow->view().refresh(); // notes or kit may have changed from somewhere else (piano roll, another pattern, undo)
    if (samplerWindow != nullptr)
        samplerWindow->panel().refresh();
    if (synthWindow != nullptr)
        synthWindow->panel().refresh();
    for (auto& strip : strips)
        strip->repaintGrid(); // the notes may have changed from somewhere else (drum machine, recording, undo): squares follow
    refreshStepGraph(); // 0.62.0: the graph editor follows the notes too
    refreshPerformancePanel(); // ... and the performance window the clips
    refreshProjectNode();      // ... and the browser's Current project node (only when what it lists changed)
    auto project = std::make_unique<Project> (compileCurrent());
    if (songMode && ! performanceOn) // 0.62.0: the markers' actions (live playback only, as the loop region)
        project->jumps = arrangement.markerJumps();
    followEvents = songMode ? project->automation : std::vector<AutomationEvent> {};
    if (! songMode && currentPattern >= 0 && currentPattern < static_cast<int> (arrangement.patterns.size()))
    {
        // The piano roll's loop region of this pattern (0.20.0); a pattern made shorter loses a region that no longer fits.
        auto& pattern = arrangement.patterns[static_cast<size_t> (currentPattern)];
        if (! pattern.hasLoop())
            pattern.loopStartTick = pattern.loopEndTick = -1;
        else
        {
            project->loopStartTick = pattern.loopStartTick;
            project->loopEndTick = pattern.loopEndTick;
        }
    }
    if (currentPattern >= 0 && currentPattern < static_cast<int> (arrangement.patterns.size()))
    {
        const auto& pattern = arrangement.patterns[static_cast<size_t> (currentPattern)];
        pianoRoll.setLoopRegion (pattern.loopStartTick, pattern.loopEndTick);
    }
    patternStartTick = std::clamp (patternStartTick, 0, std::max (0, currentPatternTicks() - 1));
    pianoRoll.setStartTick (patternStartTick);
    if (songMode && arrangement.hasLoop())
    {
        // Live playback only (exports use compileCurrent() as it is): the song runs at least to the end of the region.
        project->loopStartTick = arrangement.loopStartTick;
        project->loopEndTick = arrangement.loopEndTick;
        project->lengthTicks = std::max (project->lengthTicks, arrangement.loopEndTick);
    }
    engine.publishProject (std::move (project));
    recordHistory();
    closeUnusedPluginWindows();
    refreshChannelNames(); // 0.60.0: the piano roll's target-channel box and its ghost notes
}

void MainComponent::selectChannel (int index)
{
    selected = std::clamp (index, 0, static_cast<int> (channels.size()) - 1);
    for (auto& strip : strips)
        strip->setSelected (strip->getIndex() == selected);
    mixerPanel.setSelectedTrack (channels[static_cast<size_t> (selected)].mixerTrack);
    engine.setMidiTargetChannel (selected); // a MIDI keyboard plays the selected channel

    refreshStepGraph(); // 0.62.0
    const auto name = juce::String (channels[static_cast<size_t> (selected)].name);
    pianoRoll.setNotes (channels[static_cast<size_t> (selected)].notes);
    pianoRoll.scrollToNotes();
    pianoPanel.setTitle ("Piano Roll - " + name);
    statusLabel.setText ("Piano roll: " + name, juce::dontSendNotification);
    refreshChannelNames();
}

void MainComponent::setPlaying (bool shouldPlay)
{
    if (! shouldPlay && recording)
        finishRecording ({}); // stopping the song ends the take (finishRecording stops the transport itself)
    if (! shouldPlay)
        endNoteTake();
    if (shouldPlay && noteRecordOn)
        statusLabel.setText ("Recording notes into " + juce::String (channels[static_cast<size_t> (selected)].name) + ", "
                                 + juce::String (arrangement.patterns[static_cast<size_t> (currentPattern)].name) + ": play your keyboard",
                             juce::dontSendNotification);
    // 0.62.0: a Start marker is where the song starts from (when the start has not been put somewhere else)
    if (shouldPlay && ! engine.isPlaying() && songMode && ! performanceOn && songStartTick == 0)
        if (const int start = arrangement.startMarkerTick(); start > 0)
            seekSong (start);
    engine.setPlaying (shouldPlay);
    playButton.setToggleState (shouldPlay, juce::dontSendNotification); // 0.60.0: Play stays Play (lit while playing); Stop is its own button
}

void MainComponent::openAudioSettings()
{
    if (audioDialog != nullptr)
    {
        audioDialog->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new AudioSettingsComponent (engine, [this] (bool play) { setPlaying (play); }));
    options.dialogTitle = "Audio settings";
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = juce::Colour (0xff2a2d34);
    options.useNativeTitleBar = true;
    options.resizable = false;
    audioDialog = options.launchAsync();
}

void MainComponent::openAssignTrack (int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()) || assignDialog != nullptr)
        return;

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new AssignTrackComponent (channels, static_cast<int> (tracks.size()), channelIndex,
                                                        [safe = juce::Component::SafePointer<MainComponent> (this), channelIndex] (int track)
                                                        {
                                                            if (safe != nullptr)
                                                                safe->assignChannelToTrack (channelIndex, track);
                                                        }));
    options.dialogTitle = "Assign free mixer track - " + juce::String (channels[static_cast<size_t> (channelIndex)].name);
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = juce::Colour (0xff2a2d34);
    options.useNativeTitleBar = true;
    options.resizable = false;
    assignDialog = options.launchAsync();
}

void MainComponent::setTrackCount (size_t count)
{
    count = std::clamp (count, static_cast<size_t> (kDefaultTracks), static_cast<size_t> (kMaxTracks));
    jassert (tracks.capacity() >= static_cast<size_t> (kMaxTracks)); // no reallocation: the strips refer to the tracks
    if (count == tracks.size())
        return;
    if (count < tracks.size())
        mixerPanel.setTracks (tracks, count); // the strips of the tracks that go are removed first
    while (tracks.size() > count)
        tracks.pop_back();
    while (tracks.size() < count)
        tracks.emplace_back().name = "Track " + std::to_string (tracks.size());
    mixerPanel.setTracks (tracks);
    for (auto& strip : strips)
    {
        strip->setTrackCount (static_cast<int> (tracks.size()));
        strip->refreshFromChannel();
    }
    for (auto& c : channels)
        c.mixerTrack = std::clamp (c.mixerTrack, 0, static_cast<int> (tracks.size()) - 1);
    updateTrackRouting();
}

void MainComponent::addMixerTracks (int howMany)
{
    const int room = kMaxTracks - static_cast<int> (tracks.size());
    if (room <= 0)
    {
        statusLabel.setText ("The mixer already has " + juce::String (kMaxTracks) + " tracks, the most it can have", juce::dontSendNotification);
        return;
    }
    const int added = std::clamp (howMany, 1, room);
    setTrackCount (tracks.size() + static_cast<size_t> (added));
    mixerPanel.setSelectedTrack (static_cast<int> (tracks.size()) - 1); // the newest, with the "+" next to it
    statusLabel.setText (juce::String (added) + (added == 1 ? " mixer track" : " mixer tracks") + " added: " + juce::String (tracks.size()) + " in all",
                         juce::dontSendNotification);
    publishProject();
}

void MainComponent::removeLastMixerTrack()
{
    const int last = static_cast<int> (tracks.size()) - 1;
    juce::String reason;
    if (last < kDefaultTracks)
        reason = "the mixer keeps at least " + juce::String (kDefaultTracks) + " tracks";
    for (const auto& c : channels)
        if (reason.isEmpty() && c.mixerTrack == last)
            reason = juce::String (c.name) + " plays through it";
    for (const auto& s : tracks.back().slots)
        if (reason.isEmpty() && s.type != EffectType::None)
            reason = "it has effects";
    for (const auto& a : arrangement.automationClips)
        if (reason.isEmpty() && (a.target.kind == AutomationKind::TrackVolume || a.target.kind == AutomationKind::TrackPan
                                 || a.target.kind == AutomationKind::TrackEffect || a.target.kind == AutomationKind::TrackPlugin
                                 || a.target.kind == AutomationKind::TrackSend)
            && a.target.index == last)
            reason = "an automation clip controls it";
    if (reason.isNotEmpty())
    {
        statusLabel.setText ("Track " + juce::String (last + 1) + " was kept: " + reason, juce::dontSendNotification);
        return;
    }
    setTrackCount (tracks.size() - 1);
    statusLabel.setText ("Track " + juce::String (last + 1) + " removed: " + juce::String (tracks.size()) + " tracks", juce::dontSendNotification);
    publishProject();
}

void MainComponent::assignChannelToTrack (int channelIndex, int track)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()) || track < 0 || track >= kMaxTracks)
        return;
    if (track >= static_cast<int> (tracks.size())) // "a new track" in the dialog
        setTrackCount (static_cast<size_t> (track) + 1);

    auto& channel = channels[static_cast<size_t> (channelIndex)];
    if (channel.mixerTrack == track)
    {
        statusLabel.setText (juce::String (channel.name) + " is already on mixer track " + juce::String (track + 1),
                             juce::dontSendNotification);
        return;
    }

    channel.mixerTrack = track;
    strips[static_cast<size_t> (channelIndex)]->refreshFromChannel(); // the Track N display
    updateTrackRouting();
    mixerPanel.setSelectedTrack (track);
    statusLabel.setText (juce::String (channel.name) + " now plays through mixer track " + juce::String (track + 1),
                         juce::dontSendNotification);
    publishProject();
}

void MainComponent::applyMetronome()
{
    metronomeVolume = std::clamp (metronomeVolume, 0.0f, 1.0f);
    metronomeButton.setToggleState (metronomeOn, juce::dontSendNotification);
    engine.setMetronome (metronomeOn, metronomeVolume);
    if (settings != nullptr)
    {
        settings->setValue ("metronome.on", metronomeOn);
        settings->setValue ("metronome.volume", static_cast<double> (metronomeVolume));
        settings->saveIfNeeded();
    }
}

void MainComponent::openMetronomePanel()
{
    auto* panel = new MetronomePanel (bpmSlider.getValue(), metronomeVolume,
                                      [safe = juce::Component::SafePointer<MainComponent> (this)] (double bpm)
                                      {
                                          if (safe != nullptr)
                                              safe->bpmSlider.setValue (bpm); // the toolbar slider drives the engine
                                      },
                                      [safe = juce::Component::SafePointer<MainComponent> (this)] (float volume)
                                      {
                                          if (safe != nullptr)
                                          {
                                              safe->metronomeVolume = volume;
                                              safe->applyMetronome();
                                          }
                                      });
    juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (panel), metronomeButton.getScreenBounds(), nullptr);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (settings == nullptr)
        return;
    if (const auto state = engine.getDeviceManager().createStateXml())
    {
        settings->setValue (kAudioStateKey, state.get());
        settings->saveIfNeeded();
    }
    deviceLabel.setText (engine.describeDevice(), juce::dontSendNotification);
    noteAudioDevice();
}

void MainComponent::chooseSampleFor (int channelIndex)
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load a sample",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.flac;*.ogg;*.aif;*.aiff");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, channelIndex] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (file != juce::File()) // not cancelled
            loadSampleFile (channelIndex, file);
    });
}

bool MainComponent::loadSampleFile (int channelIndex, const juce::File& file)
{
    juce::String error;
    auto sample = decodeSample (formats, file, error);
    if (sample == nullptr)
    {
        statusLabel.setText (error, juce::dontSendNotification);
        return false;
    }

    auto& channel = channels[static_cast<size_t> (channelIndex)];
    channel.instrument = Instrument::Sampler;
    channel.sample = std::move (sample);
    channel.program = nullptr; // a sample chosen by hand replaces a SoundFont or slice program
    channel.name = file.getFileNameWithoutExtension().substring (0, 12).toStdString();

    strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
    updateTrackRouting();
    statusLabel.setText ("Loaded " + file.getFileName(), juce::dontSendNotification);
    publishProject();
    return true;
}

} // namespace sls

namespace sls
{
void MainComponent::toggleMeters()
{
    if (metersWindow != nullptr)
    {
        metersWindow.reset();
        return;
    }
    metersWindow = std::make_unique<MetersWindow> (engine);
    metersWindow->onClose = [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->metersWindow.reset(); });
    };
    metersWindow->setVisible (true);
    metersWindow->toFront (true);
}
} // namespace sls
