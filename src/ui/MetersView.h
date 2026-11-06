#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/AudioEngine.h"
#include "audio/Spectrum.h"

namespace sls
{
// Master metering in one window: loudness (momentary, short-term, integrated, range, true peak: ITU-R BS.1770 / EBU R128), a
// spectrum analyser (4096-point FFT, logarithmic frequency axis, with peak hold) and a goniometer with the correlation meter.
// It reads what the program sends to the speakers, 30 times a second, from the engine.
class MetersView : public juce::Component, private juce::Timer
{
public:
    explicit MetersView (AudioEngine& engineToWatch);
    ~MetersView() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateSpectrum();
    void paintLoudness (juce::Graphics&, juce::Rectangle<int>);
    void paintSpectrum (juce::Graphics&, juce::Rectangle<int>);
    void paintGoniometer (juce::Graphics&, juce::Rectangle<int>);
    static juce::String lufsText (float v);
    static juce::String dbText (float v);

    AudioEngine& engine;
    juce::TextButton resetButton { "Reset" };
    juce::TextButton holdButton { "Peak hold" };

    std::unique_ptr<SpectrumAnalyser> analyser = std::make_unique<SpectrumAnalyser>();
    std::vector<float> left, right, mono, bins, shown, held;
    int copied = 0;
    float correlation = 0.0f;

    juce::Rectangle<int> loudnessArea, spectrumArea, gonioArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetersView)
};

class MetersWindow : public juce::DocumentWindow
{
public:
    explicit MetersWindow (AudioEngine& engine);
    ~MetersWindow() override { clearContentComponent(); }

    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    MetersView content;
};
} // namespace sls
