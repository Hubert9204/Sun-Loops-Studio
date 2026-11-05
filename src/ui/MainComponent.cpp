#include "ui/MainComponent.h"

#include "ui/AssignTrackDialog.h"
#include "ui/AudioSettings.h"
#include "ui/MetronomePanel.h"

#include <functional>
#include <optional>

#include <juce_audio_utils/juce_audio_utils.h>

#include "audio/BuiltinSamples.h"
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
constexpr int kStep = kTicksPerBeat / 4;

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
    c.notes.push_back ({ c.rootPitch, step * kStep, kStep, velocity });
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

    auto kick = makeDrum ("Kick", makeKickSample(), 0.95f);
    auto snare = makeDrum ("Snare", makeSnareSample(), 0.7f);
    auto hat = makeDrum ("Hat", makeHatSample(), 0.5f);
    for (int bar = 0; bar < kLoopBars; ++bar)
        for (int beat = 0; beat < 4; ++beat)
        {
            const int base = bar * 16 + beat * 4;
            addStep (kick, base);
            addStep (hat, base + 2, 0.7f);
            if (beat == 1 || beat == 3)
                addStep (snare, base);
        }

    return { lead, bass, kick, snare, hat };
}

std::shared_ptr<const SampleData> decodeSample (juce::AudioFormatManager& formats, const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Cannot read " + file.getFileName();
        return nullptr;
    }

    const auto maxFrames = static_cast<juce::int64> (reader->sampleRate * 30.0); // at most 30 seconds
    const int frames = static_cast<int> (std::min (reader->lengthInSamples, maxFrames));
    if (frames < 2)
    {
        error = file.getFileName() + " is empty";
        return nullptr;
    }

    juce::AudioBuffer<float> buffer (2, frames);
    reader->read (&buffer, 0, frames, 0, true, true);
    if (reader->numChannels == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, frames);

    auto data = std::make_shared<SampleData>();
    data->left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames);
    data->right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + frames);
    data->sampleRate = reader->sampleRate;
    data->name = file.getFileNameWithoutExtension().toStdString();
    data->source = file.getFullPathName().toStdString();
    return data;
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

    // Demo song: "Pattern 1" is the full groove, "Drums" is the same beat without the melodic channels.
    arrangement.patternTicks = kLoopTicks;
    arrangement.patterns.resize (2);
    arrangement.patterns[0].name = "Pattern 1";
    arrangement.patterns[1].name = "Drums";
    arrangement.ensureChannelCount (channels.size());
    commitCurrentPattern();
    for (size_t c = 0; c < channels.size(); ++c)
        if (channels[c].instrument == Instrument::Sampler)
            arrangement.patterns[1].notes[c] = channels[c].notes;
    arrangement.clips = { { 1, 0, 0, 1 }, { 0, 0, 1, 2 } };

    for (size_t i = 0; i < channels.size(); ++i)
        createStrip (i);

    std::vector<ChannelStrip*> stripPointers;
    for (auto& s : strips)
        stripPointers.push_back (s.get());
    rackView.setStrips (std::move (stripPointers));
    rackView.setAddChannelEnabled (channels.size() < static_cast<size_t> (kMaxChannels));
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
    patternAddButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "New empty pattern");
        menu.addItem (2, "Clone current pattern");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternAddButton),
                            [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                            {
                                if (safe != nullptr && result != 0)
                                    safe->addPattern (result == 2);
                            });
    };

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

    bpmSlider.setRange (40.0, 240.0, 1.0);
    bpmSlider.setValue (120.0, juce::dontSendNotification);
    bpmSlider.onValueChange = [this] { engine.setBpm (bpmSlider.getValue()); markDirty(); };

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
    };

    playlistView.onChanged = [this]
    {
        markDirty();
        if (songMode)
            publishProject();
    };
    playlistView.onPatternPicked = [this] (int pattern) { selectPattern (pattern); };

    // Panels
    rackPanel.setContent (&rackView);
    rackPanel.setMinimumSize (420, 130);
    pianoPanel.setContent (&pianoRoll);
    pianoPanel.setMinimumSize (360, 220);
    mixerPanelWindow.setContent (&mixerPanel);
    mixerPanelWindow.setMinimumSize (720, 440);
    playlistPanel.setContent (&playlistView);
    playlistPanel.setMinimumSize (380, 200);

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
    for (juce::Button* b : std::initializer_list<juce::Button*> { &fileButton, &playButton, &clearButton, &modeButton, &patternAddButton, &deviceButton, &metronomeButton,
                     &browserToggle, &browserTab, &rackToggle, &playlistToggle, &pianoToggle, &mixerToggle, &resetButton })
        b->setMouseClickGrabsKeyboardFocus (false);
    patternBox.setMouseClickGrabsKeyboardFocus (false);
    patternBox.setWantsKeyboardFocus (false);

    for (auto* c : std::initializer_list<juce::Component*> { &fileButton, &playButton, &modeButton, &patternBox, &patternAddButton,
                                                              &clearButton, &deviceButton, &metronomeButton,
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
        metronomeOn = settings->getBoolValue ("metronome.on", false);
        metronomeVolume = static_cast<float> (settings->getDoubleValue ("metronome.volume", 0.5));
    }
    applyMetronome();
    const auto error = engine.initialise (savedAudio.get());
    engine.getDeviceManager().addChangeListener (this); // from now on the device and buffer size are remembered
    deviceLabel.setText (error.isEmpty() ? engine.describeDevice() : "Audio error: " + error,
                         juce::dontSendNotification);

    startTimerHz (30);

    // Space and F5/F6/F7/F9 work from the start, without needing a click first.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
                                     { if (safe != nullptr) safe->grabKeyboardFocus(); });
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

    deviceLabel.setText (engine.describeDevice(), juce::dontSendNotification);

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

    std::vector<ChannelStrip*> stripPointers;
    for (auto& s : strips)
        stripPointers.push_back (s.get());
    rackView.setStrips (std::move (stripPointers));
    rackView.setAddChannelEnabled (channels.size() < static_cast<size_t> (kMaxChannels));

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
    publishProject();
}

void MainComponent::addPattern (bool cloneCurrent)
{
    if (arrangement.patterns.size() >= static_cast<size_t> (kMaxPatterns))
        return;

    commitCurrentPattern();

    Pattern pattern;
    pattern.name = "Pattern " + std::to_string (arrangement.patterns.size() + 1);
    if (cloneCurrent)
        pattern.notes = arrangement.patterns[static_cast<size_t> (currentPattern)].notes;
    arrangement.patterns.push_back (std::move (pattern));
    arrangement.ensureChannelCount (channels.size());

    refreshPatternBox();
    selectPattern (static_cast<int> (arrangement.patterns.size()) - 1);
    markDirty();
}

void MainComponent::refreshPatternBox()
{
    patternBox.clear (juce::dontSendNotification);
    for (size_t i = 0; i < arrangement.patterns.size(); ++i)
        patternBox.addItem (juce::String (arrangement.patterns[i].name), static_cast<int> (i) + 1);
    patternBox.setSelectedId (currentPattern + 1, juce::dontSendNotification);
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

    const auto name = juce::String (channels[static_cast<size_t> (selected)].name);
    pianoRoll.setNotes (channels[static_cast<size_t> (selected)].notes);
    pianoRoll.scrollToNotes();
    pianoPanel.setTitle ("Piano Roll - " + name);
    statusLabel.setText ("Piano roll: " + name, juce::dontSendNotification);
}

void MainComponent::setPlaying (bool shouldPlay)
{
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
    currentPattern = std::clamp (doc.currentPattern, 0, static_cast<int> (arrangement.patterns.size()) - 1);

    const auto& notes = arrangement.patterns[static_cast<size_t> (currentPattern)].notes;
    for (size_t c = 0; c < channels.size(); ++c)
        channels[c].notes = notes[c];

    for (size_t i = 0; i < channels.size(); ++i)
        createStrip (i);
    std::vector<ChannelStrip*> stripPointers;
    for (auto& s : strips)
        stripPointers.push_back (s.get());
    rackView.setStrips (std::move (stripPointers));
    rackView.setAddChannelEnabled (channels.size() < static_cast<size_t> (kMaxChannels));

    for (int t = 0; t < kMaxTracks; ++t)
        mixerPanel.refreshTrack (t);
    mixerPanel.refreshTrack (kMasterIndex);

    bpmSlider.setValue (doc.bpm, juce::dontSendNotification);
    engine.setBpm (doc.bpm);
    volumeSlider.setValue (doc.masterVolume, juce::dontSendNotification);
    engine.setGain (doc.masterVolume);
    mixerPanel.setMasterVolume (doc.masterVolume);

    refreshPatternBox();
    playlistView.setSelectedPattern (currentPattern);
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
