#pragma once

#include <functional>
#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>

#include "audio/AudioEngine.h"
#include "audio/BufferTuning.h"

namespace sls
{
// The "Audio..." dialog: the device selector, plus the buffer size (a menu and an Auto button) and a live read-out
// of how hard the computer is working and whether blocks are being dropped.
//
// Auto plays the current song at 256, 512, 1024, ... samples (see BufferTuner) with extra load added to every
// block (AudioEngine::setLoadTest), watching the audio system's dropout count and the longest time a block took,
// and keeps the first size that stays clean with one step to spare.
class AudioSettingsComponent : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    // `setPlaying` starts or stops the transport (so Auto can play the song while it measures and restore it after).
    AudioSettingsComponent (AudioEngine& engine, std::function<void (bool)> setPlaying);
    ~AudioSettingsComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int kWidth = 520;
    static constexpr int kHeight = 570;

private:
    enum class Phase { idle, settling, measuring };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refreshSizes();           // fills the menu from what the current device offers
    void refreshReadout();
    void chooseSize (int size);    // sets the device's buffer size; returns after the device has restarted
    void startAuto();
    void stopAuto (bool restore);
    void beginTrial();
    void finishTrial();
    int deviceBufferSize() const;
    double deviceSampleRate() const;

    AudioEngine& engine;
    std::function<void (bool)> setPlaying;

    juce::AudioDeviceSelectorComponent selector;
    juce::Label bufferLabel { {}, "Buffer size" };
    juce::ComboBox sizeBox;
    juce::Label delayLabel;
    juce::TextButton autoButton { "Auto: set for this computer" };
    juce::Label autoStatus;
    juce::Label loadLabel;
    juce::Label helpLabel;

    std::unique_ptr<BufferTuner> tuner;
    Phase phase = Phase::idle;
    int phaseTicks = 0;
    int trialXrunBase = 0;
    float trialPeak = 0.0f;
    int sizeBeforeAuto = 0;
    bool wasPlaying = false;
    bool updatingMenu = false;
    float shownPeak = 0.0f;
    int sinceReadout = 0;
};
} // namespace sls
