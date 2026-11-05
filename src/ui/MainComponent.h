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
#include "ui/ChannelStrip.h"
#include "ui/MixerPanel.h"
#include "ui/PanelWindow.h"
#include "ui/PianoRoll.h"
#include "ui/PlaylistView.h"
#include "ui/RackView.h"

namespace sls
{
class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void publishProject();
    void selectChannel (int index);
    void setPlaying (bool shouldPlay);
    void openAudioSettings();
    void exportWav();
    void chooseSampleFor (int channelIndex);

    // Channels
    void addChannel (Instrument instrument);
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
    std::vector<std::unique_ptr<ChannelStrip>> strips;
    int selected = 0;

    Arrangement arrangement;
    int currentPattern = 0;
    bool songMode = false;

    // Panel contents first, then the panels that show them, then the workspace holding the panels.
    // Members are destroyed in reverse order, so a panel never outlives the content it points to.
    RackView rackView;
    PianoRoll pianoRoll;
    MixerPanel mixerPanel;
    PlaylistView playlistView { arrangement };

    PanelWindow rackPanel { "Channel Rack" };
    PanelWindow pianoPanel { "Piano Roll" };
    PanelWindow mixerPanelWindow { "Mixer" };
    PanelWindow playlistPanel { "Playlist" };
    Workspace workspace;

    std::unique_ptr<juce::PropertiesFile> settings;
    bool restoringLayout = false;

    juce::TextButton playButton { "Play" };
    juce::TextButton clearButton { "Clear notes" };
    juce::TextButton modeButton { "Pattern" };
    juce::ComboBox patternBox;
    juce::TextButton patternAddButton { "+" };
    juce::TextButton rackToggle { "Rack F6" };
    juce::TextButton playlistToggle { "Playlist F5" };
    juce::TextButton pianoToggle { "Piano F7" };
    juce::TextButton mixerToggle { "Mixer F9" };
    juce::TextButton resetButton { "Reset" };
    juce::TextButton exportButton { "Export WAV..." };
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
