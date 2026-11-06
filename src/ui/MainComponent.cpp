#include "ui/MainComponent.h"

#include "ui/AssignTrackDialog.h"
#include "ui/AudioSettings.h"
#include "ui/MetronomePanel.h"

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
constexpr int kLoopBars = 2;
constexpr int kLoopTicks = kLoopBars * 4 * kTicksPerBeat;
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
} // namespace

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
    auto* window = new juce::AlertWindow ("Rename channel", "Name:", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", juce::String (channels[static_cast<size_t> (channelIndex)].name));
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
                             juce::ModalCallbackFunction::create (
                                 [safe = juce::Component::SafePointer<MainComponent> (this), window, channelIndex] (int result)
                                 {
                                     if (safe == nullptr || result != 1)
                                         return;

                                     const auto name = window->getTextEditorContents ("name").trim().substring (0, 16);
                                     if (name.isEmpty())
                                         return;

                                     safe->channels[static_cast<size_t> (channelIndex)].name = name.toStdString();
                                     safe->strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
                                     safe->updateTrackRouting();
                                     if (channelIndex == safe->selected)
                                         safe->selectChannel (channelIndex);
                                     safe->publishProject();
                                 }),
                             true);
}

void MainComponent::openPianoRoll (int channelIndex)
{
    selectChannel (channelIndex);
    showPanel (pianoPanel);
    saveLayout();
    updateToggleButtons();
}

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
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                        {
                            if (safe != nullptr && result != 0)
                                safe->addPattern (result == 2);
                        });
}

void MainComponent::renamePattern (int index)
{
    if (index < 0 || index >= static_cast<int> (arrangement.patterns.size()))
        return;

    auto* window = new juce::AlertWindow ("Rename pattern", "Name:", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", juce::String (arrangement.patterns[static_cast<size_t> (index)].name));
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
                             juce::ModalCallbackFunction::create (
                                 [safe = juce::Component::SafePointer<MainComponent> (this), window, index] (int result)
                                 {
                                     if (safe == nullptr || result != 1 || index >= static_cast<int> (safe->arrangement.patterns.size()))
                                         return;

                                     auto name = window->getTextEditorContents ("name").toStdString();
                                     if (! cleanPatternName (name))
                                         return; // an empty name keeps the old one

                                     safe->arrangement.patterns[static_cast<size_t> (index)].name = name;
                                     safe->refreshPatternBox();
                                     safe->markDirty();
                                     safe->statusLabel.setText ("Pattern renamed to " + juce::String (name), juce::dontSendNotification);
                                 }),
                             true);
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
        playlistView.clearHistory();
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
}

Project MainComponent::compileCurrent()
{
    commitCurrentPattern();
    return songMode ? compileSong (channels, arrangement, tracks, masterTrack.slots)
                    : compilePattern (channels, arrangement, currentPattern, tracks, masterTrack.slots);
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

// After a project was opened its audio clips only know the path of their file: read the audio now. A file that is
// gone leaves a silent clip that keeps the path, so saving the project again does not lose it.
void MainComponent::loadAudioClipFiles (juce::StringArray& problems)
{
    std::map<std::string, std::shared_ptr<const SampleData>> loaded;
    for (auto& clip : arrangement.audioClips)
    {
        if (clip.data == nullptr || clip.data->numFrames() > 0)
            continue;

        const auto source = clip.data->source;
        auto found = loaded.find (source);
        if (found == loaded.end())
        {
            std::shared_ptr<const SampleData> data;
            if (source.rfind ("builtin:", 0) == 0) // a sound built into the program (Cyber-Hub, the intro)
                data = makeBuiltinSample (source.substr (8));
            else
                data = importAudioFile (juce::File (juce::String (source))).data;
            if (data == nullptr)
                problems.add ("Audio file not found or unreadable: " + juce::String (source));
            found = loaded.emplace (source, data != nullptr ? data : clip.data).first;
        }
        clip.data = found->second;
    }
}

// ------------------------------------------------------------------------- Recording

void MainComponent::updateRecordButton()
{
    recordButton.setToggleState (recording, juce::dontSendNotification);
    recordButton.setButtonText (recording ? "Stop rec" : "Record");
}

void MainComponent::setRecordTrack (int track)
{
    recordTrack = std::clamp (track, 0, kPlaylistTracks - 1);
    playlistView.setArmedTrack (recordTrack);
    if (settings != nullptr)
    {
        settings->setValue ("record.track", recordTrack);
        settings->saveIfNeeded();
    }
    if (! recording)
        statusLabel.setText ("Record will write to Track " + juce::String (recordTrack + 1), juce::dontSendNotification);
}

void MainComponent::openRecordMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("The recording becomes");
    menu.addItem (40, "A clip on a playlist track (choose it below)", ! recording, recordTarget == RecordTarget::PlaylistTrack);
    menu.addItem (41, "A new sampler channel (play it from the piano roll)", ! recording, recordTarget == RecordTarget::SamplerChannel);
    menu.addSeparator();
    menu.addSectionHeader ("Playlist track");
    for (int t = 0; t < kPlaylistTracks; ++t)
        menu.addItem (10 + t, "Track " + juce::String (t + 1), ! recording, t == recordTrack);
    menu.addSeparator();
    menu.addSectionHeader ("Input");
    menu.addItem (30, "Mono: first input (microphone, guitar)", ! recording, recordChannels == InputRecorder::Channels::Mono);
    menu.addItem (31, "Stereo: inputs 1 and 2", ! recording, recordChannels == InputRecorder::Channels::Stereo);
    menu.addSeparator();
    menu.addItem (32, "Hear the input while recording (use headphones)", true, monitorInput);
    menu.addItem (33, "Audio and MIDI devices...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recordMenuButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result >= 10 && result < 10 + kPlaylistTracks)
                                safe->setRecordTrack (result - 10);
                            else if (result == 30 || result == 31)
                            {
                                safe->recordChannels = result == 31 ? InputRecorder::Channels::Stereo : InputRecorder::Channels::Mono;
                                if (safe->settings != nullptr)
                                {
                                    safe->settings->setValue ("record.stereo", result == 31);
                                    safe->settings->saveIfNeeded();
                                }
                            }
                            else if (result == 32)
                            {
                                safe->monitorInput = ! safe->monitorInput;
                                safe->engine.setInputMonitoring (safe->monitorInput);
                                if (safe->settings != nullptr)
                                {
                                    safe->settings->setValue ("record.monitor", safe->monitorInput);
                                    safe->settings->saveIfNeeded();
                                }
                            }
                            else if (result == 40 || result == 41)
                            {
                                safe->recordTarget = result == 41 ? RecordTarget::SamplerChannel : RecordTarget::PlaylistTrack;
                                if (safe->settings != nullptr)
                                {
                                    safe->settings->setValue ("record.target", result == 41 ? 1 : 0);
                                    safe->settings->saveIfNeeded();
                                }
                            }
                            else if (result == 33)
                                safe->openAudioSettings();
                        });
}

void MainComponent::toggleRecording()
{
    if (recording)
        finishRecording ({});
    else
        startRecording();
}

void MainComponent::startRecording()
{
    const auto fail = [this] (const juce::String& message)
    {
        statusLabel.setText (message, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Record", message);
    };

    // A take that goes to the playlist starts at the start of its track, so the track must be free there.
    if (recordTarget == RecordTarget::PlaylistTrack && arrangement.maxAudioLengthAt (recordTrack, 0, arrangement.timelineTicks()) < kTicksPerBeat)
    {
        fail ("Track " + juce::String (recordTrack + 1) + " is not empty at the start of the playlist. Click the name of an empty "
              "track in the playlist to choose where to record.");
        return;
    }

    if (const auto error = engine.enableDefaultInput(); error.isNotEmpty())
    {
        fail (error);
        return;
    }

    library.ensureExists();
    const auto folder = library.samples.getChildFile ("Recordings");
    auto file = folder.getChildFile ("Recording " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".wav");
    for (int n = 2; file.exists(); ++n)
        file = folder.getChildFile ("Recording " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + " (" + juce::String (n) + ").wav");

    if (recordTarget == RecordTarget::PlaylistTrack && ! songMode)
        setSongMode (true); // the playlist is what plays while recording, and the take is part of it
    engine.setInputMonitoring (monitorInput);

    if (const auto error = engine.startRecording (file, recordChannels); error.isNotEmpty())
    {
        fail (error);
        return;
    }

    recording = true;
    playButton.setToggleState (true, juce::dontSendNotification);
    playButton.setButtonText ("Stop");
    updateRecordButton();
    showRecordProgress();
}

void MainComponent::showRecordProgress()
{
    const int seconds = static_cast<int> (engine.getRecordedSeconds());
    const float peak = engine.consumeInputPeak();
    const auto level = peak > 0.0f ? juce::String (juce::Decibels::gainToDecibels (peak), 0) + " dB" : juce::String ("silence");
    statusLabel.setText ("Recording on Track " + juce::String (recordTrack + 1) + "   " + juce::String (seconds / 60) + ":"
                             + juce::String (seconds % 60).paddedLeft ('0', 2) + "   input " + level + (peak >= 0.98f ? "  (clipping!)" : ""),
                         juce::dontSendNotification);

    // A take cannot go past the end of the playlist (or the longest audio the program keeps in memory).
    const double bpm = std::max (1.0, bpmSlider.getValue());
    const double limit = std::min (kMaxImportSeconds, arrangement.timelineTicks() * 60.0 / (bpm * kTicksPerBeat));
    if (engine.getRecordedSeconds() >= limit)
        finishRecording ("The take reached the end of the playlist.");
}

void MainComponent::finishRecording (const juce::String& why)
{
    if (! recording)
        return;
    recording = false;

    const auto take = engine.stopRecording();
    updateRecordButton();
    setPlaying (false);

    if (take.file == juce::File() || take.frames < 2)
    {
        take.file.deleteFile();
        statusLabel.setText ("Nothing was recorded", juce::dontSendNotification);
        return;
    }

    auto result = importAudioFile (take.file);
    if (result.data == nullptr)
    {
        statusLabel.setText ("The take was saved as " + take.file.getFullPathName() + " but could not be placed: " + result.error,
                             juce::dontSendNotification);
        return;
    }

    auto data = std::make_shared<SampleData> (*result.data);
    data->name = take.file.getFileNameWithoutExtension().replace ("Recording ", "Rec ").toStdString();

    // A new sampler channel: the take is the channel's sample, ready to be played from the piano roll.
    juce::String note;
    if (recordTarget == RecordTarget::SamplerChannel)
    {
        const int index = addChannel (Instrument::Sampler, false);
        if (index >= 0)
        {
            auto& channel = channels[static_cast<size_t> (index)];
            channel.sample = data;
            channel.name = juce::Time::getCurrentTime().formatted ("Rec %H-%M-%S").toStdString();
            channel.rootPitch = 60;
            strips[static_cast<size_t> (index)]->refreshFromChannel();
            selectChannel (index);
            publishProject();
            auto status = "Recorded " + juce::String (take.frames / take.sampleRate, 1) + " s into the new sampler channel \""
                        + juce::String (channel.name) + "\": open its piano roll and play it, or draw notes";
            if (take.droppedBlocks > 0)
                status += ". Warning: some audio was lost because the disk was too slow.";
            statusLabel.setText (status, juce::dontSendNotification);
            return;
        }
        note = "There is no free channel, so the take was put on the playlist instead.";
    }

    // Exactly at the start of the track, as long as the take (cut to the free room if the playlist changed meanwhile).
    AudioClip clip;
    clip.data = data;
    clip.name = data->name;
    clip.track = recordTrack;
    clip.startTick = 0;
    const int natural = clip.naturalLengthTicks (bpmSlider.getValue());
    clip.lengthTicks = arrangement.maxAudioLengthAt (recordTrack, 0, natural);

    bool placed = false;
    if (clip.lengthTicks >= 1 && arrangement.audioClips.size() < static_cast<size_t> (kMaxAudioClips))
    {
        arrangement.audioClips.push_back (clip);
        placed = true;
    }
    else
        placed = addAudioClip (data, recordTrack, 0, note);

    auto status = placed ? "Recorded " + juce::String (take.frames / take.sampleRate, 1) + " s on Track " + juce::String (recordTrack + 1)
                         : "The take was saved as " + take.file.getFullPathName() + " but did not fit in the playlist";
    if (why.isNotEmpty())
        status += ". " + why;
    if (take.droppedBlocks > 0)
        status += " Warning: some audio was lost because the disk was too slow.";
    if (note.isNotEmpty())
        status += " " + note;
    statusLabel.setText (status, juce::dontSendNotification);

    if (placed)
    {
        playlistView.clearHistory(); // the arrangement was changed from outside: undo could not restore it safely
        playlistView.repaint();
        publishProject();
    }
}

// ------------------------------------------------------------------------- Recording notes (MIDI keyboards)

void MainComponent::applyNoteRecording()
{
    noteRecorder.configure (kLoopTicks, noteQuantize);
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

// ------------------------------------------------------------------------- Browser

void MainComponent::setBrowserOpen (bool shouldBeOpen)
{
    browserOpen = shouldBeOpen;
    browser.setVisible (browserOpen);
    browserTab.setVisible (! browserOpen);
    browserToggle.setToggleState (browserOpen, juce::dontSendNotification);

    if (settings != nullptr)
    {
        settings->setValue ("browser.open", browserOpen);
        settings->saveIfNeeded();
    }
    resized();
}

void MainComponent::rebuildBrowser()
{
    library.ensureExists();
    browser.setTree (buildBrowserTree (library, foundPlugins, pluginScanState));
}

namespace
{
// Scans for VST3 plugins off the message thread. It loads each candidate in-process to read its description,
// so it is only started when the user asks for it.
class PluginScanThread : public juce::Thread
{
public:
    explicit PluginScanThread (std::function<void (std::vector<PluginInfo>)> whenDone)
        : juce::Thread ("VST3 scan"), done (std::move (whenDone))
    {
    }

    void run() override
    {
        PluginScanner scanner;
        auto found = scanner.scan (PluginScanner::defaultSearchPaths(), true);
        if (threadShouldExit())
            return;

        juce::MessageManager::callAsync ([callback = done, result = std::move (found)]() mutable { callback (std::move (result)); });
    }

private:
    std::function<void (std::vector<PluginInfo>)> done;
};
} // namespace

void MainComponent::startPluginScan()
{
    if (pluginScanState == PluginScanState::Scanning)
        return;

    pluginScanState = PluginScanState::Scanning;
    rebuildBrowser();
    statusLabel.setText ("Scanning for VST3 plugins...", juce::dontSendNotification);

    pluginScanThread = std::make_unique<PluginScanThread> (
        [safe = juce::Component::SafePointer<MainComponent> (this)] (std::vector<PluginInfo> found)
        {
            if (safe == nullptr)
                return;
            safe->foundPlugins = std::move (found);
            safe->pluginScanState = PluginScanState::Done;
            safe->rebuildBrowser();
            safe->statusLabel.setText (juce::String (safe->foundPlugins.size()) + " VST3 plugin(s) found (listed only: plugin hosting is not implemented yet)",
                                       juce::dontSendNotification);
        });
    pluginScanThread->startThread();
}

bool MainComponent::applyPreset (int channelIndex, const ChannelPreset& preset)
{
    auto& channel = channels[static_cast<size_t> (channelIndex)];
    preset.applyTo (channel);

    bool sampleOk = true;
    if (preset.instrument == Instrument::Sampler && ! preset.sampleSource.empty())
    {
        std::shared_ptr<const SampleData> sample;
        if (preset.sampleSource.rfind ("builtin:", 0) == 0)
            sample = makeBuiltinSample (preset.sampleSource.substr (8));
        else
        {
            juce::String error;
            sample = decodeSample (formats, juce::File (juce::String (preset.sampleSource)), error);
        }

        if (sample != nullptr)
            channel.sample = std::move (sample);
        else
            sampleOk = false; // keep whatever sample the channel had
    }

    strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
    updateTrackRouting();
    publishProject();
    return sampleOk;
}

void MainComponent::applyBrowserItem (int channelIndex, const drag::Item& item)
{
    if (channelIndex < 0)
    {
        if (channels.size() >= static_cast<size_t> (kMaxChannels))
        {
            statusLabel.setText ("All " + juce::String (kMaxChannels) + " channels are in use", juce::dontSendNotification);
            return;
        }
        const bool wantsSynth = (item.kind == "instrument" && item.payload == "synth")
                             || (item.kind == "preset" && item.payload == "builtin:lead");
        channelIndex = addChannel (wantsSynth ? Instrument::Synth : Instrument::Sampler, false);
        if (channelIndex < 0)
            return;
    }

    if (channelIndex >= static_cast<int> (channels.size()))
        return;

    selectChannel (channelIndex);
    auto& channel = channels[static_cast<size_t> (channelIndex)];

    if (item.kind == "file")
    {
        loadSampleFile (channelIndex, juce::File (item.payload));
    }
    else if (item.kind == "builtin")
    {
        if (auto sample = makeBuiltinSample (item.payload.toStdString()))
        {
            channel.instrument = Instrument::Sampler;
            channel.name = sample->name;
            channel.sample = std::move (sample);
            strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
            updateTrackRouting();
            publishProject();
            statusLabel.setText ("Loaded " + juce::String (channel.name), juce::dontSendNotification);
        }
    }
    else if (item.kind == "instrument")
    {
        setInstrument (channelIndex, item.payload == "sampler" ? Instrument::Sampler : Instrument::Synth);
    }
    else if (item.kind == "preset")
    {
        std::optional<ChannelPreset> preset;
        if (item.payload.startsWith ("builtin:"))
        {
            const auto id = item.payload.substring (8).toStdString();
            for (const auto& p : factoryPresets())
                if (p.id == id)
                    preset = p;
        }
        else
        {
            preset = ChannelPreset::fromJson (juce::File (item.payload).loadFileAsString());
        }

        if (! preset.has_value())
            statusLabel.setText ("Cannot read preset " + juce::File (item.payload).getFileName(), juce::dontSendNotification);
        else if (applyPreset (channelIndex, *preset))
            statusLabel.setText ("Applied preset " + juce::String (preset->name), juce::dontSendNotification);
        else
            statusLabel.setText ("Applied preset " + juce::String (preset->name) + " (its sample file was not found)",
                                 juce::dontSendNotification);
    }
}

void MainComponent::savePresetFor (int channelIndex)
{
    auto* window = new juce::AlertWindow ("Save channel preset", "Preset name:", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", juce::String (channels[static_cast<size_t> (channelIndex)].name));
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
                             juce::ModalCallbackFunction::create (
                                 [safe = juce::Component::SafePointer<MainComponent> (this), window, channelIndex] (int result)
                                 {
                                     if (safe == nullptr || result != 1)
                                         return;

                                     const auto name = window->getTextEditorContents ("name").trim().substring (0, 16);
                                     const auto fileName = juce::File::createLegalFileName (name);
                                     if (name.isEmpty() || fileName.isEmpty())
                                         return;

                                     const auto preset = ChannelPreset::fromChannel (safe->channels[static_cast<size_t> (channelIndex)],
                                                                                     name.toStdString());
                                     safe->library.ensureExists();
                                     const auto file = safe->library.presets.getChildFile (fileName + kPresetExtension);
                                     const bool ok = file.replaceWithText (preset.toJson());
                                     safe->rebuildBrowser();
                                     safe->statusLabel.setText (ok ? "Saved preset " + name : "Could not write " + file.getFullPathName(),
                                                                juce::dontSendNotification);
                                 }),
                             true);
}

void MainComponent::exportAs (ExportFormat format)
{
    const auto& info = exportFormatInfo (format);
    const juce::String extension (info.extension);
    const auto label = juce::String (info.label).upToFirstOccurrenceOf ("  (", false, false);

    fileChooser = std::make_unique<juce::FileChooser> (
        juce::String (songMode ? "Export song as " : "Export pattern as ") + label,
        juce::File::getSpecialLocation (juce::File::userMusicDirectory)
            .getChildFile (juce::String (songMode ? "sun-loops-song" : "sun-loops-pattern") + extension),
        "*" + extension);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<MainComponent> (this), format, extension] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr)
            return;
        auto file = chooser.getResult();
        if (file == juce::File())
            return; // cancelled

        // "My.Song" -> "My.Song.mp3": append unless the name already ends with the extension.
        if (! file.getFileName().endsWithIgnoreCase (extension))
            file = file.getParentDirectory().getChildFile (file.getFileName() + extension);

        safe->statusLabel.setText ("Exporting " + file.getFileName() + " ...", juce::dontSendNotification);
        safe->statusLabel.repaint();

        RenderOptions options;
        options.bpm = safe->bpmSlider.getValue();

        const auto result = exportAudio (safe->compileCurrent(), options, file, format);

        safe->statusLabel.setText (result.wasOk() ? "Exported " + file.getFileName()
                                                  : "Export failed: " + result.getErrorMessage(),
                                   juce::dontSendNotification);
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export failed",
                                                    result.getErrorMessage(), "OK", safe.getComponent());
    });
}

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
