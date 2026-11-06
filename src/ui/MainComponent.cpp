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
namespace
{
constexpr const char* kAudioStateKey = "audio.device"; // the audio device and buffer size, as JUCE saves them
}

namespace
{
constexpr double kDemoBpm = 112.0;

Channel makeSynth (const char* name, float volume)
{
    Channel c;
    c.name = name;
    c.instrument = Instrument::Synth;
    c.volume = volume;
    return c;
}

Channel makeDrum (const char* name, std::shared_ptr<const SampleData> sample, float volume)
{
    Channel c;
    c.name = name;
    c.instrument = Instrument::Sampler;
    c.sample = std::move (sample);
    c.rootPitch = 60;
    c.volume = volume;
    return c;
}

void addStep (Channel& c, int step, float velocity = 0.9f)
{
    c.notes.push_back ({ c.rootPitch, step * kSixteenthTicks, kSixteenthTicks, velocity });
}

// A small groove so there is something to hear and edit on first launch.
std::vector<Channel> demoChannels()
{
    auto lead = makeSynth ("Lead", 0.55f);
    const int am[8] = { 57, 60, 64, 69, 64, 60, 64, 60 };
    const int f[8]  = { 53, 57, 60, 65, 60, 57, 60, 57 };
    for (int bar = 0; bar < kLoopBars; ++bar)
        for (int i = 0; i < 8; ++i)
            lead.notes.push_back ({ (bar == 0 ? am : f)[i], bar * 4 * kTicksPerBeat + i * (kTicksPerBeat / 2),
                                    kTicksPerBeat / 2 - 20, 0.8f });

    auto bass = makeSynth ("Bass", 0.9f);
    const int bar1[3][3] = { { 45, 0, 900 }, { 45, 960, 420 }, { 48, 1440, 420 } };
    const int bar2[3][3] = { { 41, 1920, 900 }, { 41, 2880, 420 }, { 43, 3360, 420 } };
    for (const auto& n : bar1) bass.notes.push_back ({ n[0], n[1], n[2], 0.9f });
    for (const auto& n : bar2) bass.notes.push_back ({ n[0], n[1], n[2], 0.9f });

    // The drums are Cyber-Hub system sounds (see docs/cyber-hub.md), played at their own pitch. If the sounds were not
    // built in, the program's own synthesised drums stand in.
    const auto cyber = [] (const char* id, std::shared_ptr<const SampleData> fallback)
    {
        auto sample = makeCyberHubSample (id);
        return sample != nullptr ? sample : fallback;
    };
    auto kick = makeDrum ("Kick", cyber ("cyber:button-pressed", makeKickSample()), 0.95f);
    auto clap = makeDrum ("Clap", cyber ("cyber:trash-empty", makeSnareSample()), 0.5f);
    auto hat = makeDrum ("Hat", cyber ("cyber:audio-volume-change", makeHatSample()), 0.55f);
    auto tom = makeDrum ("Tom", cyber ("cyber:button-pressed-modifier", makeSnareSample()), 0.6f);
    for (int bar = 0; bar < kLoopBars; ++bar)
        for (int beat = 0; beat < 4; ++beat)
        {
            const int base = bar * 16 + beat * 4;
            addStep (kick, base);
            addStep (hat, base + 2, 0.7f);
            if (beat == 1 || beat == 3)
                addStep (clap, base);
        }
    for (int bar = 0; bar < kLoopBars; ++bar)
    {
        addStep (tom, bar * 16 + 7, 0.8f);
        addStep (tom, bar * 16 + 14, 0.7f);
    }

    return { lead, bass, kick, clap, hat, tom };
}

} // namespace

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
    channels = demoChannels();
    channels.reserve (static_cast<size_t> (kMaxChannels));
    for (size_t i = 0; i < channels.size(); ++i)
        channels[i].mixerTrack = static_cast<int> (i); // every channel on its own mixer track to start with

    library.ensureExists(); // ~/Music/Sun Loops Studio/{Samples,Drum Kits,Presets}: where the browser looks

    // Demo song (Cyber-Hub): "Pattern 1" is the full groove, "Drums" is the same beat without the melodic channels.
    // The playlist is Drums, two times Pattern 1, Drums, with two Cyber-Hub sounds as audio clips on track 8.
    arrangement.patternTicks = kLoopTicks;
    arrangement.patterns.resize (2);
    arrangement.patterns[0].name = "Pattern 1";
    arrangement.patterns[1].name = "Drums";
    arrangement.ensureChannelCount (channels.size());
    commitCurrentPattern();
    for (size_t c = 0; c < channels.size(); ++c)
        if (channels[c].instrument == Instrument::Sampler)
            arrangement.patterns[1].notes[c] = channels[c].notes;
    arrangement.clips = { { 1, 0, 0, 1 }, { 0, 0, 1, 2 }, { 1, 0, 3, 1 } };
    for (const auto& piece : { std::pair<const char*, int> { "cyber:desktop-login", 0 },
                               std::pair<const char*, int> { "cyber:desktop-logout", 3 * kLoopTicks } })
        if (auto data = makeCyberHubSample (piece.first))
        {
            AudioClip clip;
            clip.data = data;
            clip.name = data->name;
            clip.track = 7;
            clip.startTick = piece.second;
            clip.gain = 0.6f;
            clip.lengthTicks = clip.naturalLengthTicks (kDemoBpm);
            arrangement.audioClips.push_back (clip);
        }

    for (size_t i = 0; i < channels.size(); ++i)
        createStrip (i);

    refreshRackStrips();
    rackView.onAddChannel = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Synth channel");
        menu.addItem (2, "Sampler channel (load a sample)");
        menu.showMenuAsync (juce::PopupMenu::Options(),
                            [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                            {
                                if (safe == nullptr || result == 0)
                                    return;
                                safe->addChannel (result == 1 ? Instrument::Synth : Instrument::Sampler);
                            });
    };

    rackView.onItemDroppedOnEmptyArea = [this] (const drag::Item& item) { applyBrowserItem (-1, item); };

    mixerPanel.setTracks (tracks);
    mixerPanel.setMaster (masterTrack);
    mixerPanel.onTrackChanged = [this] (int) { publishProject(); };
    mixerPanel.onMasterVolumeChanged = [this] (double v) { volumeSlider.setValue (v); }; // the slider drives the engine
    updateTrackRouting();

    fileButton.setTooltip ("New, open and save projects (Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S)");
    fileButton.onClick = [this] { showFileMenu(); };

    playButton.setClickingTogglesState (true);
    playButton.onClick = [this] { setPlaying (playButton.getToggleState()); };

    modeButton.setClickingTogglesState (true);
    modeButton.setTooltip ("Pattern: loop the selected pattern. Song: play the playlist.");
    modeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8943a));
    modeButton.onClick = [this] { setSongMode (modeButton.getToggleState()); };

    patternBox.setTooltip ("Current pattern");
    patternBox.onChange = [this] { selectPattern (patternBox.getSelectedId() - 1); };

    patternAddButton.setTooltip ("New pattern");
    patternAddButton.onClick = [this] { showNewPatternMenu (patternAddButton); };

    clearButton.onClick = [this]
    {
        channels[static_cast<size_t> (selected)].notes.clear();
        pianoRoll.setNotes ({});
        strips[static_cast<size_t> (selected)]->repaint();
        publishProject();
        recordNoteChange();
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
    playlistView.onTrackSelected = [this] (int track) { setRecordTrack (track); };

    // Rec notes: what a MIDI keyboard plays while the song runs is written into the selected channel's pattern.
    noteRecordButton.setClickingTogglesState (false);
    noteRecordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9433b));
    noteRecordButton.setTooltip ("Record the notes of a MIDI keyboard into the piano roll: switch it on, press Play and play. "
                                 "The notes go to the selected channel, in the current pattern, and are quantized "
                                 "(right-click to choose how).");
    noteRecordButton.onClick = [this] { toggleNoteRecording(); };
    noteRecordButton.onRightClick = [this] { openNoteRecordMenu(); };

    bpmSlider.setRange (40.0, 240.0, 1.0);
    bpmSlider.setValue (kDemoBpm, juce::dontSendNotification);
    engine.setBpm (kDemoBpm);
    bpmSlider.onValueChange = [this] { engine.setBpm (bpmSlider.getValue()); playlistView.setBpm (bpmSlider.getValue()); markDirty(); };

    volumeSlider.setRange (0.0, 1.0, 0.01);
    volumeSlider.setValue (0.8, juce::dontSendNotification);
    volumeSlider.onValueChange = [this]
    {
        engine.setGain (static_cast<float> (volumeSlider.getValue()));
        mixerPanel.setMasterVolume (volumeSlider.getValue());
        markDirty();
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

    pianoRoll.setLoopLengthTicks (kLoopTicks);
    pianoRoll.onNotesChanged = [this]
    {
        channels[static_cast<size_t> (selected)].notes = pianoRoll.getNotes();
        strips[static_cast<size_t> (selected)]->repaint();
        publishProject();
        recordNoteChange();
    };

    playlistView.onChanged = [this]
    {
        markDirty();
        if (songMode)
            publishProject();
    };
    playlistView.onPatternPicked = [this] (int pattern) { selectPattern (pattern); };
    playlistView.onPatternSelected = [this] (int pattern) { selectPattern (pattern); };
    playlistView.onNewPattern = [this] { showNewPatternMenu (playlistView); };
    playlistView.onRenamePattern = [this] (int pattern) { renamePattern (pattern); };
    playlistView.onDeletePattern = [this] (int pattern) { deletePattern (pattern); };
    playlistView.onItemDropped = [this] (const drag::Item& item, int track, int tick) { dropOnPlaylist (item, track, tick); };
    playlistView.onFilesDropped = [this] (const juce::StringArray& files, int track, int tick) { importMediaFiles (files, track, tick); };
    playlistView.setBpm (bpmSlider.getValue());

    // Panels
    rackPanel.setContent (&rackView);
    rackPanel.setMinimumSize (420, 130);
    pianoPanel.setContent (&pianoRoll);
    pianoPanel.setMinimumSize (360, 220);
    mixerPanelWindow.setContent (&mixerPanel);
    mixerPanelWindow.setMinimumSize (720, 440);
    playlistPanel.setContent (&playlistView);
    playlistPanel.setMinimumSize (520, 290);

    for (auto* panel : allPanels())
    {
        workspace.addPanel (*panel);
        panel->onLayoutChanged = [this] (PanelWindow&)
        {
            saveLayout();
            updateToggleButtons();
        };
    }

    browser.onItemActivated = [this] (const drag::Item& item) { applyBrowserItem (selected, item); };
    browser.onAction = [this] (const juce::String& action) { if (action == "scan-plugins") startPluginScan(); };
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
    resetButton.onClick = [this] { resetLayout(); };
    resetButton.setTooltip ("Restore the default panel layout");

    for (auto* t : { &rackToggle, &playlistToggle, &pianoToggle, &mixerToggle })
        t->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8943a));

    deviceLabel.setJustificationType (juce::Justification::centredLeft);
    deviceLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setFont (juce::FontOptions (12.0f));

    // Space and F5/F6/F7/F9 are handled by this component, so these controls must not steal keyboard focus.
    for (juce::Button* b : std::initializer_list<juce::Button*> { &fileButton, &playButton, &clearButton, &modeButton, &patternAddButton, &deviceButton, &metronomeButton, &recordButton, &recordMenuButton, &noteRecordButton,
                     &browserToggle, &browserTab, &rackToggle, &playlistToggle, &pianoToggle, &mixerToggle, &resetButton })
        b->setMouseClickGrabsKeyboardFocus (false);
    patternBox.setMouseClickGrabsKeyboardFocus (false);
    patternBox.setWantsKeyboardFocus (false);

    for (auto* c : std::initializer_list<juce::Component*> { &fileButton, &playButton, &modeButton, &patternBox, &patternAddButton,
                                                              &clearButton, &deviceButton, &metronomeButton, &recordButton, &recordMenuButton, &noteRecordButton,
                                                              &browserToggle, &rackToggle, &playlistToggle, &pianoToggle,
                                                              &mixerToggle, &resetButton, &bpmSlider, &volumeSlider,
                                                              &bpmLabel, &volumeLabel, &deviceLabel, &statusLabel, &meter,
                                                              &browser, &browserTab, &workspace })
        addAndMakeVisible (c);

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
    projectDirty = false; // the demo song counts as untitled and unchanged
    updateWindowTitle();

    std::unique_ptr<juce::XmlElement> savedAudio;
    if (settings != nullptr)
        savedAudio = settings->getXmlValue (kAudioStateKey);
    if (settings != nullptr)
    {
        recordTrack = std::clamp (settings->getIntValue ("record.track", 0), 0, kPlaylistTracks - 1);
        recordChannels = settings->getBoolValue ("record.stereo", false) ? InputRecorder::Channels::Stereo : InputRecorder::Channels::Mono;
        monitorInput = settings->getBoolValue ("record.monitor", false);
        metronomeOn = settings->getBoolValue ("metronome.on", false);
        metronomeVolume = static_cast<float> (settings->getDoubleValue ("metronome.volume", 0.5));
        noteQuantize = std::clamp (settings->getIntValue ("midirec.quantize", kTicksPerBeat / 4), 0, kTicksPerBeat);
        recordTarget = settings->getIntValue ("record.target", 0) == 1 ? RecordTarget::SamplerChannel : RecordTarget::PlaylistTrack;
    }
    applyMetronome();
    applyNoteRecording();
    playlistView.setArmedTrack (recordTrack);
    engine.setInputMonitoring (monitorInput);
    engine.setMidiTargetChannel (selected);
    engine.setStartupSound (makeStartupSample());
    const auto error = engine.initialise (savedAudio.get());
    if (error.isEmpty() && (settings == nullptr || settings->getBoolValue ("startup.sound", true)))
        engine.playStartupSound();
    engine.getDeviceManager().addChangeListener (this); // from now on the device and buffer size are remembered
    deviceLabel.setText (error.isEmpty() ? engine.describeDevice() : "Audio error: " + error,
                         juce::dontSendNotification);

    resetNoteHistory();
    startTimerHz (30);

    // Space and F5/F6/F7/F9 work from the start, without needing a click first.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
                                     { if (safe != nullptr) safe->grabKeyboardFocus(); });
}

// Hands the strips to the rack (after strips were created or removed) and enables or disables "add channel".
void MainComponent::refreshRackStrips()
{
    std::vector<ChannelStrip*> stripPointers;
    stripPointers.reserve (strips.size());
    for (auto& s : strips)
        stripPointers.push_back (s.get());
    rackView.setStrips (std::move (stripPointers));
    rackView.setAddChannelEnabled (channels.size() < static_cast<size_t> (kMaxChannels));
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.getDeviceManager().removeChangeListener (this);
    audioDialog.deleteAndZero(); // it refers to the engine
    assignDialog.deleteAndZero();
    saveLayout();
    if (pluginScanThread != nullptr)
        pluginScanThread->stopThread (5000);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2d34));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto top = area.removeFromTop (30);
    fileButton.setBounds (top.removeFromLeft (56));
    top.removeFromLeft (8);
    playButton.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (8);
    modeButton.setBounds (top.removeFromLeft (76));
    top.removeFromLeft (12);
    patternBox.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (4);
    patternAddButton.setBounds (top.removeFromLeft (30));
    top.removeFromLeft (16);
    bpmLabel.setBounds (top.removeFromLeft (34));
    bpmSlider.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (12);
    volumeLabel.setBounds (top.removeFromLeft (50));
    volumeSlider.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (16);
    metronomeButton.setBounds (top.removeFromLeft (96));
    top.removeFromLeft (8);
    recordButton.setBounds (top.removeFromLeft (76));
    recordMenuButton.setBounds (top.removeFromLeft (26));
    top.removeFromLeft (8);
    noteRecordButton.setBounds (top.removeFromLeft (84));

    meter.setBounds (top.removeFromRight (14));
    top.removeFromRight (10);
    deviceButton.setBounds (top.removeFromRight (80));

    area.removeFromTop (6);
    auto views = area.removeFromTop (30);
    browserToggle.setBounds (views.removeFromLeft (96));
    views.removeFromLeft (4);
    rackToggle.setBounds (views.removeFromLeft (84));
    views.removeFromLeft (4);
    playlistToggle.setBounds (views.removeFromLeft (96));
    views.removeFromLeft (4);
    pianoToggle.setBounds (views.removeFromLeft (84));
    views.removeFromLeft (4);
    mixerToggle.setBounds (views.removeFromLeft (84));
    views.removeFromLeft (12);
    resetButton.setBounds (views.removeFromLeft (60));
    views.removeFromLeft (16);
    clearButton.setBounds (views.removeFromLeft (90));

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
        if (letter == 'Z') { if (key.getModifiers().isShiftDown()) redoNotes(); else undoNotes(); return true; }
        if (letter == 'Y') { redoNotes(); return true; }
    }

    // With the mouse over the playlist its keys (Delete, Ctrl+Z/Y/C/X/V/D, 1..5 for the tools, +/- to zoom) work first.
    if (overPlaylist && playlistView.handleKey (key))
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
    return false;
}

void MainComponent::timerCallback()
{
    const float masterPeak = engine.consumePeak();
    meter.setLevel (masterPeak);

    // Mixer tracks: peak since the last tick, plus RMS. Only drawn while the mixer is open.
    const bool mixerShown = mixerPanelWindow.isOpen();
    mixerPanel.setMasterLevels (mixerShown ? masterPeak : 0.0f, mixerShown ? engine.getMasterRms() : 0.0f);
    for (int t = 0; t < kMaxTracks; ++t)
    {
        const float trackPeak = engine.consumeTrackPeak (t);
        if (mixerShown)
            mixerPanel.setTrackLevels (t, trackPeak, engine.getTrackRms (t));
    }

    // In pattern mode the playhead is a position inside the pattern (piano roll, rack); in song mode it is
    // a position in the song, shown on the playlist only.
    const int playhead = engine.isPlaying() ? engine.getPlayheadTick() : -1;
    const int patternPlayhead = songMode ? -1 : playhead;
    pianoRoll.setPlayheadTick (patternPlayhead);
    playlistView.setPlayheadTick (songMode ? playhead : -1);

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

    // When the window is active but keyboard focus is outside this component (e.g. on the window
    // itself), take it so Space and F5/F6/F7/F9 work without a click first.
    if (auto* peer = getPeer(); peer != nullptr && peer->isFocused() && ! hasKeyboardFocus (true))
        grabKeyboardFocus();
}

// ------------------------------------------------------------------------- Panels

namespace
{
// "panel2": the layout changed with the browser and the expanded mixer, so layouts stored by older versions are ignored.
constexpr const char* kPanelKeys[] = { "panel2.rack", "panel2.piano", "panel2.mixer", "panel2.playlist" };

constexpr int kRackHeight = 336;

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
        default: return { 8, lowerY, fullW, lowerH, true };
    }
}
} // namespace

std::array<PanelWindow*, 4> MainComponent::allPanels()
{
    return { &rackPanel, &pianoPanel, &mixerPanelWindow, &playlistPanel }; // same order as kPanelKeys
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
    strip->setLoopLengthTicks (kLoopTicks);
    strip->onSelect = [this] (int idx) { selectChannel (idx); };
    strip->onChanged = [this] (int idx)
    {
        if (idx == selected)
            pianoRoll.setNotes (channels[static_cast<size_t> (idx)].notes);
        updateTrackRouting();
        publishProject();
        recordNoteChange(); // (does nothing unless a step was edited)
    };
    strip->onSavePreset = [this] (int idx) { savePresetFor (idx); };
    strip->onAssignTrack = [this] (int idx) { openAssignTrack (idx); };
    strip->onItemDropped = [this] (int idx, const drag::Item& item) { applyBrowserItem (idx, item); };
    strip->onLoadSample = [this] (int idx) { chooseSampleFor (idx); };
    strip->onOpenPianoRoll = [this] (int idx) { openPianoRoll (idx); };
    strip->onInstrumentChosen = [this] (int idx, Instrument instrument) { setInstrument (idx, instrument); };
    strip->onRename = [this] (int idx) { renameChannel (idx); };
    strips.push_back (std::move (strip));
}

int MainComponent::addChannel (Instrument instrument, bool askForSample)
{
    if (channels.size() >= static_cast<size_t> (kMaxChannels))
        return -1;

    commitCurrentPattern();

    Channel c;
    c.instrument = instrument;
    c.name = (instrument == Instrument::Synth ? "Synth " : "Sampler ") + std::to_string (channels.size() + 1);
    c.mixerTrack = std::min (static_cast<int> (channels.size()), kMaxTracks - 1); // next free track
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
    engine.publishProject (std::make_unique<Project> (compileCurrent()));
}

void MainComponent::selectChannel (int index)
{
    selected = std::clamp (index, 0, static_cast<int> (channels.size()) - 1);
    for (auto& strip : strips)
        strip->setSelected (strip->getIndex() == selected);
    mixerPanel.setSelectedTrack (channels[static_cast<size_t> (selected)].mixerTrack);
    engine.setMidiTargetChannel (selected); // a MIDI keyboard plays the selected channel

    const auto name = juce::String (channels[static_cast<size_t> (selected)].name);
    pianoRoll.setNotes (channels[static_cast<size_t> (selected)].notes);
    pianoRoll.scrollToNotes();
    pianoPanel.setTitle ("Piano Roll - " + name);
    statusLabel.setText ("Piano roll: " + name, juce::dontSendNotification);
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
    engine.setPlaying (shouldPlay);
    playButton.setToggleState (shouldPlay, juce::dontSendNotification);
    playButton.setButtonText (shouldPlay ? "Stop" : "Play");
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
    options.content.setOwned (new AssignTrackComponent (channels, channelIndex,
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

void MainComponent::assignChannelToTrack (int channelIndex, int track)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()) || track < 0 || track >= kMaxTracks)
        return;

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
    channel.name = file.getFileNameWithoutExtension().substring (0, 12).toStdString();

    strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
    updateTrackRouting();
    statusLabel.setText ("Loaded " + file.getFileName(), juce::dontSendNotification);
    publishProject();
    return true;
}

} // namespace sls
