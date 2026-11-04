#include "MainWindow.h"
#include <iostream>

// =============================================================================
// MainWindow implementation
// Stile FL Studio: tema scuro, controlli minimali ma funzionali,
// layout orizzontale per transport + meters.
// =============================================================================

MainWindow::MainWindow()
    : DocumentWindow("FL Studio Clone — Audio Engine",  // Titolo
                     juce::Colours::darkgrey,
                     juce::DocumentWindow::allButtons) {

    setUsingNativeTitleBar(true);
    centreWithSize(FLStudioClone::kMainWindowWidth,
                   FLStudioClone::kMainWindowHeight);
    setVisible(true);

    // --- Inizializza audio engine ---
    if (!audioEngine_.initialize()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Audio Error",
            "Could not initialize audio device.\n"
            "Check that PipeWire/PulseAudio is running.\n"
            "You can still open the app without audio.");
        return;
    }

    // --- Setup UI layout ---
    setupLayout();

    // --- Timer per aggiornare i meter a ~30fps ---
    startTimer(1000 / 30);  // ~30 fps, non real-time
}

MainWindow::~MainWindow() {
    stopTimer();
    audioEngine_.stop();
}

void MainWindow::closeButtonPressed() {
    audioEngine_.stop();
    JUCEApplicationBase::getInstance()->systemRequestedQuit();
}

void MainWindow::timerCallback() {
    // Aggiorna i VU meter dal thread UI (non real-time)
    float peak = audioEngine_.readPeakLevel();
    if (leftMeter_) leftMeter_->setPeakLevel(peak);
    if (rightMeter_) rightMeter_->setPeakLevel(peak);

    // Aggiorna status bar
    if (statusLabel_) {
        size_t dropped = audioEngine_.getDroppedSamples();
        statusLabel_->setText(
            "Freq: " + juce::String(static_cast<int>(audioEngine_.getFrequency())) + " Hz | "
            "Buffer: 128 samples | "
            "Dropped: " + std::to_string(dropped),
            juce::dontRedraw);
    }

    // Aggiorna nome plugin
    if (pluginNameLabel_) {
        pluginNameLabel_->setText(
            audioEngine_.hasPlugin() ? 
                "Plugin: " + audioEngine_.getPluginName() : 
                "No Plugin Loaded",
            juce::dontRedraw);
    }
}

void MainWindow::setupLayout() {
    // Tema scuro stile FL Studio
    setColour(juce::Window::backgroundColourId,
              juce::Colours::black.withAlpha(0.95f));

    // --- Header / Transport area ---
    auto transportArea = std::make_unique<juce::Component>();
    transportArea->setOpaque(true);
    transportArea->addColour(juce::ResizableWindow::backgroundColourId,
                             juce::Colours::grey.withAlpha(0.3f));

    // Pulsante Play
    playButton_ = std::make_unique<juce::TextButton>("playBtn");
    playButton_->setButtonText("▶ PLAY");
    playButton_->setColour(juce::TextButton::buttonColourId,
                           juce::Colour(0x00aa44));
    playButton_->setColour(juce::TextButton::buttonOnColourId,
                           juce::Colour(0x00cc55));
    playButton_->onClick = [this] { onPlayClicked(); };

    // Pulsante Stop
    stopButton_ = std::make_unique<juce::TextButton>("stopBtn");
    stopButton_->setButtonText("■ STOP");
    stopButton_->setColour(juce::TextButton::buttonColourId,
                           juce::Colour(0xaa2200));
    stopButton_->setColour(juce::TextButton::buttonOnColourId,
                           juce::Colour(0xcc3300));
    stopButton_->onClick = [this] { onStopClicked(); };

    // Slider frequenza (20 Hz — 20 kHz)
    frequencySlider_ = std::make_unique<juce::Slider>("freqSlider");
    frequencySlider_->setRange(20.0, 20000.0, 1.0);
    frequencySlider_->setValue(440.0);
    frequencySlider_->setSliderStyle(juce::Slider::SliderStyle::LinearBarVertical);
    frequencySlider_->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    frequencySlider_->onChange = [this] { onFrequencyChanged({}); };

    frequencyLabel_ = std::make_unique<juce::Label>();
    frequencyLabel_->setText("Frequency (A4)", juce::NotificationType::dontSendNotification);
    frequencyLabel_->setColour(juce::Label::textColourId,
                               juce::Colours::white.withAlpha(0.7f));

    // Slider volume
    volumeSlider_ = std::make_unique<juce::Slider>("volSlider");
    volumeSlider_->setRange(0.0, 1.0, 0.01);
    volumeSlider_->setValue(0.5);
    volumeSlider_->setSliderStyle(juce::Slider::SliderStyle::LinearBarVertical);
    volumeSlider_->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    volumeSlider_->onChange = [this] { onVolumeChanged({}); };

    volumeLabel_ = std::make_unique<juce::Label>();
    volumeLabel_->setText("Master Vol", juce::NotificationType::dontSendNotification);
    volumeLabel_->setColour(juce::Label::textColourId,
                            juce::Colours::white.withAlpha(0.7f));

    // --- Plugin Panel ---
    loadPluginButton_ = std::make_unique<juce::TextButton>("loadPluginBtn");
    loadPluginButton_->setButtonText("Load VST3 Plugin...");
    loadPluginButton_->setColour(juce::TextButton::buttonColourId,
                                 juce::Colour(0x2266aa));
    loadPluginButton_->onClick = [this] { onLoadPluginClicked(); };

    pluginNameLabel_ = std::make_unique<juce::Label>();
    pluginNameLabel_->setText("No Plugin Loaded", juce::NotificationType::dontSendNotification);
    pluginNameLabel_->setColour(juce::Label::textColourId,
                                juce::Colours::white.withAlpha(0.7f));

    // --- VU Meters (Stereo L/R) ---
    leftMeter_ = std::make_unique<AudioMeter>();
    rightMeter_ = std::make_unique<AudioMeter>();

    // --- Status bar ---
    statusLabel_ = std::make_unique<juce::Label>();
    statusLabel_->setColour(juce::Label::textColourId,
                            juce::Colours::grey.withAlpha(0.5f));
    statusLabel_->setFont(12.0f);

    // --- Layout con Grid/Flex ---
    auto mainContent = std::make_unique<juce::Component>();
    mainContent->setOpaque(true);

    // Usiamo un layout semplice: top bar + meters area
    addAndMakeVisible(*transportArea);
    addAndMakeVisible(*leftMeter_);
    addAndMakeVisible(*rightMeter_);
    addAndMakeVisible(statusLabel_);
    addAndMakeVisible(*loadPluginButton_);
    addAndMakeVisible(*pluginNameLabel_);

    // Posizionamento manuale (più controllabile di Grid per questo caso)
    setResizeLimits(800, 500, 2000, 1200);
}

void MainWindow::onPlayClicked() {
    if (!audioEngine_.isRunning()) {
        audioEngine_.start();
        playButton_->setButtonText("▶ PLAYING...");
        playButton_->setEnabled(false);
        stopButton_->setEnabled(true);
        frequencySlider_->setEnabled(false);
    }
}

void MainWindow::onStopClicked() {
    if (audioEngine_.isRunning()) {
        audioEngine_.stop();
        playButton_->setButtonText("▶ PLAY");
        playButton_->setEnabled(true);
        stopButton_->setEnabled(false);
        frequencySlider_->setEnabled(true);
    }
}

void MainWindow::onFrequencyChanged(juce::ValueTree /*tree*/) {
    float freq = frequencySlider_->getValue();
    audioEngine_.setFrequency(static_cast<float>(freq));
    if (frequencyLabel_) {
        // Formatta: < 100 Hz → intero, >= 100 Hz → 1 decimale
        if (freq < 100.0f) {
            frequencyLabel_->setText(
                std::to_string(static_cast<int>(freq)) + " Hz",
                juce::dontRedraw);
        } else {
            frequencyLabel_->setText(
                juce::String(freq, 1) + " Hz",
                juce::dontRedraw);
        }
    }
}

void MainWindow::onVolumeChanged(juce::ValueTree /*tree*/) {
    float vol = volumeSlider_->getValue();
    audioEngine_.setMasterVolume(static_cast<float>(vol));
    if (volumeLabel_) {
        volumeLabel_->setText(
            juce::String(static_cast<int>(vol * 100)) + "%",
            juce::dontRedraw);
    }
}

void MainWindow::onLoadPluginClicked() {
    // Finestra di selezione file VST3
    juce::FileChooser fc("Seleziona un plugin VST3 da caricare",
                          juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                          "*.vst3");

    if (fc.browseForFileToOpen()) {
        auto file = fc.getResult();
        if (audioEngine_.loadPlugin(file)) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Plugin Caricato",
                "Plugin caricato con successo: " + file.getFileName());
        } else {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Errore di caricamento",
                "Impossibile caricare il plugin: " + file.getFileName());
        }
    }
}
