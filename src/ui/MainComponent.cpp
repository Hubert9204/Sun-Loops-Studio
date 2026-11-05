#include "ui/MainComponent.h"

#include <optional>

#include <juce_audio_utils/juce_audio_utils.h>

#include "audio/BuiltinSamples.h"
#include "audio/OfflineRenderer.h"

namespace sls
{
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

    channels = demoChannels();
    channels.shrink_to_fit(); // fixed size from here on: strips keep references into this vector

    std::vector<ChannelStrip*> stripPointers;
    for (size_t i = 0; i < channels.size(); ++i)
    {
        auto strip = std::make_unique<ChannelStrip> (channels[i], static_cast<int> (i));
        strip->setLoopLengthTicks (kLoopTicks);
        strip->onSelect = [this] (int idx) { selectChannel (idx); };
        strip->onChanged = [this] (int idx)
        {
            if (idx == selected)
                pianoRoll.setNotes (channels[static_cast<size_t> (idx)].notes);
            mixerPanel.refreshStrip (idx);
            publishProject();
        };
        strip->onLoadSample = [this] (int idx) { chooseSampleFor (idx); };
        stripPointers.push_back (strip.get());
        strips.push_back (std::move (strip));
    }
    rackView.setStrips (std::move (stripPointers));

    mixerPanel.setChannels (channels);
    mixerPanel.onSelect = [this] (int idx) { selectChannel (idx); };
    mixerPanel.onChanged = [this] (int idx)
    {
        strips[static_cast<size_t> (idx)]->refreshFromChannel();
        publishProject();
    };

    playButton.setClickingTogglesState (true);
    playButton.onClick = [this] { setPlaying (playButton.getToggleState()); };

    clearButton.onClick = [this]
    {
        channels[static_cast<size_t> (selected)].notes.clear();
        pianoRoll.setNotes ({});
        strips[static_cast<size_t> (selected)]->repaint();
        publishProject();
    };
    exportButton.onClick = [this] { exportWav(); };
    deviceButton.onClick = [this] { openAudioSettings(); };

    bpmSlider.setRange (40.0, 240.0, 1.0);
    bpmSlider.setValue (120.0, juce::dontSendNotification);
    bpmSlider.onValueChange = [this] { engine.setBpm (bpmSlider.getValue()); };

    volumeSlider.setRange (0.0, 1.0, 0.01);
    volumeSlider.setValue (0.8, juce::dontSendNotification);
    volumeSlider.onValueChange = [this] { engine.setGain (static_cast<float> (volumeSlider.getValue())); };

    pianoRoll.setLoopLengthTicks (kLoopTicks);
    pianoRoll.onNotesChanged = [this]
    {
        channels[static_cast<size_t> (selected)].notes = pianoRoll.getNotes();
        strips[static_cast<size_t> (selected)]->repaint();
        publishProject();
    };

    // Panels
    rackPanel.setContent (&rackView);
    rackPanel.setMinimumSize (420, 130);
    pianoPanel.setContent (&pianoRoll);
    pianoPanel.setMinimumSize (360, 220);
    mixerPanelWindow.setContent (&mixerPanel);
    mixerPanelWindow.setMinimumSize (200, 240);

    for (auto* panel : { &rackPanel, &pianoPanel, &mixerPanelWindow })
    {
        workspace.addPanel (*panel);
        panel->onLayoutChanged = [this] (PanelWindow&)
        {
            saveLayout();
            updateToggleButtons();
        };
    }

    rackToggle.onClick = [this] { togglePanel (rackPanel); };
    pianoToggle.onClick = [this] { togglePanel (pianoPanel); };
    mixerToggle.onClick = [this] { togglePanel (mixerPanelWindow); };
    resetButton.onClick = [this] { resetLayout(); };
    resetButton.setTooltip ("Restore the default panel layout");

    for (auto* t : { &rackToggle, &pianoToggle, &mixerToggle })
        t->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8943a));

    deviceLabel.setJustificationType (juce::Justification::centredLeft);
    deviceLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setFont (juce::FontOptions (12.0f));

    // Space and F6/F7/F9 are handled by this component, so buttons must not steal keyboard focus.
    for (auto* b : { &playButton, &clearButton, &exportButton, &deviceButton, &rackToggle, &pianoToggle,
                     &mixerToggle, &resetButton })
        b->setMouseClickGrabsKeyboardFocus (false);

    for (auto* c : std::initializer_list<juce::Component*> { &playButton, &clearButton, &exportButton, &deviceButton,
                                                              &rackToggle, &pianoToggle, &mixerToggle, &resetButton,
                                                              &bpmSlider, &volumeSlider, &bpmLabel, &volumeLabel,
                                                              &deviceLabel, &statusLabel, &meter, &workspace })
        addAndMakeVisible (c);

    setWantsKeyboardFocus (true);
    setSize (1200, 800);

    selectChannel (0);
    restoreLayout();
    publishProject(); // before the device starts, so the first callback already has the project

    const auto error = engine.initialise();
    deviceLabel.setText (error.isEmpty() ? engine.describeDevice() : "Audio error: " + error,
                         juce::dontSendNotification);

    startTimerHz (30);

    // Space and F6/F7/F9 work from the start, without needing a click first.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
                                     { if (safe != nullptr) safe->grabKeyboardFocus(); });
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveLayout();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2d34));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto top = area.removeFromTop (30);
    playButton.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (8);
    clearButton.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (12);
    bpmLabel.setBounds (top.removeFromLeft (34));
    bpmSlider.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (12);
    volumeLabel.setBounds (top.removeFromLeft (50));
    volumeSlider.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (16);
    rackToggle.setBounds (top.removeFromLeft (78));
    top.removeFromLeft (4);
    pianoToggle.setBounds (top.removeFromLeft (78));
    top.removeFromLeft (4);
    mixerToggle.setBounds (top.removeFromLeft (78));
    top.removeFromLeft (8);
    resetButton.setBounds (top.removeFromLeft (60));

    meter.setBounds (top.removeFromRight (14));
    top.removeFromRight (10);
    deviceButton.setBounds (top.removeFromRight (80));
    top.removeFromRight (8);
    exportButton.setBounds (top.removeFromRight (110));

    area.removeFromTop (6);
    auto info = area.removeFromTop (20);
    statusLabel.setBounds (info.removeFromRight (info.getWidth() / 3));
    deviceLabel.setBounds (info);

    area.removeFromTop (6);
    workspace.setBounds (area);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        setPlaying (! engine.isPlaying());
        return true;
    }
    if (key == juce::KeyPress::F6Key) { togglePanel (rackPanel); return true; }
    if (key == juce::KeyPress::F7Key) { togglePanel (pianoPanel); return true; }
    if (key == juce::KeyPress::F9Key) { togglePanel (mixerPanelWindow); return true; }
    return false;
}

void MainComponent::timerCallback()
{
    meter.setLevel (engine.consumePeak());

    const int playhead = engine.isPlaying() ? engine.getPlayheadTick() : -1;
    pianoRoll.setPlayheadTick (playhead);
    for (auto& strip : strips)
    {
        // consumeChannelPeak resets the peak, so read it once and hand it to both views of the channel.
        const float peak = engine.consumeChannelPeak (strip->getIndex());
        strip->setMeterLevel (peak);
        mixerPanel.setMeterLevel (strip->getIndex(), peak);
        strip->setPlayheadTick (playhead);
    }

    deviceLabel.setText (engine.describeDevice(), juce::dontSendNotification);

    // When the window is active but keyboard focus is outside this component (e.g. on the window
    // itself), take it so Space and F6/F7/F9 work without a click first.
    if (auto* peer = getPeer(); peer != nullptr && peer->isFocused() && ! hasKeyboardFocus (true))
        grabKeyboardFocus();
}

// ------------------------------------------------------------------------- Panels

namespace
{
constexpr const char* kPanelKeys[] = { "panel.rack", "panel.piano", "panel.mixer" };

// Rack on top, piano roll below it, mixer closed (it opens on the right and overlaps the others).
PanelLayout defaultLayoutFor (int panelIndex, int workW, int workH)
{
    constexpr int kRackHeight = 5 * (52 + 2) + 24 + 8 + 4;
    switch (panelIndex)
    {
        case 0:  return { 8, 8, std::min (900, workW - 16), kRackHeight, true };
        case 1:  return { 8, kRackHeight + 16, workW - 16, workH - kRackHeight - 24, true };
        default: return { workW - 440 - 8, 40, 440, 360, false };
    }
}
} // namespace

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
    pianoToggle.setToggleState (pianoPanel.isOpen(), juce::dontSendNotification);
    mixerToggle.setToggleState (mixerPanelWindow.isOpen(), juce::dontSendNotification);
}

void MainComponent::restoreLayout()
{
    restoringLayout = true;

    PanelWindow* panels[] = { &rackPanel, &pianoPanel, &mixerPanelWindow };
    for (int i = 0; i < 3; ++i)
    {
        const auto stored = settings != nullptr
                                ? PanelLayout::parse (settings->getValue (kPanelKeys[i]).toStdString())
                                : std::nullopt;
        panels[i]->applyLayout (stored.value_or (defaultLayoutFor (i, workspace.getWidth(), workspace.getHeight())));
    }

    restoringLayout = false;
    workspace.activate (rackPanel.isOpen() ? &rackPanel : nullptr);
    updateToggleButtons();
}

void MainComponent::resetLayout()
{
    restoringLayout = true;

    PanelWindow* panels[] = { &rackPanel, &pianoPanel, &mixerPanelWindow };
    for (int i = 0; i < 3; ++i)
        panels[i]->applyLayout (defaultLayoutFor (i, workspace.getWidth(), workspace.getHeight()));

    restoringLayout = false;
    workspace.activate (&rackPanel);
    saveLayout();
    updateToggleButtons();
}

void MainComponent::saveLayout()
{
    if (restoringLayout || settings == nullptr)
        return;

    const PanelWindow* panels[] = { &rackPanel, &pianoPanel, &mixerPanelWindow };
    for (int i = 0; i < 3; ++i)
        settings->setValue (kPanelKeys[i], juce::String (panels[i]->currentLayout().toString()));

    settings->saveIfNeeded();
}

void MainComponent::publishProject()
{
    engine.publishProject (std::make_unique<Project> (Project::make (channels, kLoopTicks)));
}

void MainComponent::selectChannel (int index)
{
    selected = std::clamp (index, 0, static_cast<int> (channels.size()) - 1);
    for (auto& strip : strips)
        strip->setSelected (strip->getIndex() == selected);
    mixerPanel.setSelected (selected);

    pianoRoll.setNotes (channels[static_cast<size_t> (selected)].notes);
    statusLabel.setText ("Piano roll: " + juce::String (channels[static_cast<size_t> (selected)].name),
                         juce::dontSendNotification);
}

void MainComponent::setPlaying (bool shouldPlay)
{
    engine.setPlaying (shouldPlay);
    playButton.setToggleState (shouldPlay, juce::dontSendNotification);
    playButton.setButtonText (shouldPlay ? "Stop" : "Play");
}

void MainComponent::openAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(),
                                                             0, 0, 2, 2, false, false, true, false);
    selector->setSize (480, 360);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = "Audio settings";
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = juce::Colour (0xff2a2d34);
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
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
        if (file == juce::File())
            return; // cancelled

        juce::String error;
        auto sample = decodeSample (formats, file, error);
        if (sample == nullptr)
        {
            statusLabel.setText (error, juce::dontSendNotification);
            return;
        }

        auto& channel = channels[static_cast<size_t> (channelIndex)];
        channel.instrument = Instrument::Sampler;
        channel.sample = std::move (sample);
        channel.name = file.getFileNameWithoutExtension().substring (0, 12).toStdString();

        strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
        statusLabel.setText ("Loaded " + file.getFileName(), juce::dontSendNotification);
        publishProject();
    });
}

void MainComponent::exportWav()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Export pattern as WAV",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("sun-loops-pattern.wav"),
        "*.wav");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File())
            return; // cancelled

        file = file.withFileExtension ("wav");

        RenderOptions options;
        options.bpm = bpmSlider.getValue();

        const auto result = renderToWavFile (Project::make (channels, kLoopTicks), options, file);

        statusLabel.setText (result.wasOk() ? "Exported " + file.getFileName()
                                            : "Export failed: " + result.getErrorMessage(),
                             juce::dontSendNotification);
    });
}
} // namespace sls
