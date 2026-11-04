#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "audio/AudioEngine.h"
#include "ui/AudioMeter.h"

// =============================================================================
// MainWindow: finestra principale stile FL Studio.
// Layout:
//   [Header bar]  — titolo, controlli globali
//   [Transport]   — Play/Stop, BPM, Frequency
//   [VU Meters]   — Stereo peak meters (L/R)
//   [Plugin Panel]— Plugin load button, name display
//   [Status bar]  — Sample rate, buffer size, dropped samples
// =============================================================================

class MainWindow : public juce::DocumentWindow,
                   public juce::Timer {
public:
    MainWindow();
    ~MainWindow() override;

private:
    // --- JUCE Component overrides ---
    void closeButtonPressed() override;
    void timerCallback() override;  // Aggiornamento UI a ~30fps

    // --- Layout ---
    void setupLayout();

    // --- Callback pulsanti ---
    void onPlayClicked();
    void onStopClicked();
    void onFrequencyChanged(juce::ValueTree);  // Slider value change
    void onVolumeChanged(juce::ValueTree);
    void onLoadPluginClicked();

    // --- Audio Engine ---
    AudioEngine audioEngine_;

    // --- UI Components ---
    std::unique_ptr<juce::TextButton> playButton_;
    std::unique_ptr<juce::TextButton> stopButton_;
    std::unique_ptr<juce::Slider> frequencySlider_;
    std::unique_ptr<juce::Label> frequencyLabel_;
    std::unique_ptr<juce::Slider> volumeSlider_;
    std::unique_ptr<juce::Label> volumeLabel_;
    std::unique_ptr<AudioMeter> leftMeter_;
    std::unique_ptr<AudioMeter> rightMeter_;
    std::unique_ptr<juce::Label> statusLabel_;
    std::unique_ptr<juce::TextButton> loadPluginButton_;
    std::unique_ptr<juce::Label> pluginNameLabel_;

    // --- Drag & drop per file audio (futuro) ---
    class AudioDragAndDropContainer : public juce::Component,
                                       public juce::DragAndDropContainer {
    public:
        AudioDragAndDropContainer() {
            setOpaque(true);
            setInterceptsMouseClicks(false, false);
        }

        void paint(juce::Graphics& g) override {
            g.fillAll(juce::Colours::black.withAlpha(0.3f));
            g.setColour(juce::Colours::white.withAlpha(0.4f));
            g.setFont(16.0f);
            g.drawFittedText("Drop audio files here (future)",
                             getLocalBounds(), juce::Justification::centred, 1);
        }
    } dragContainer_;
};