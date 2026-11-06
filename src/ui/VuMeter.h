#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sls
{
// Vertical level meter with a dB scale (-60 .. +6 dB): a solid bar for the RMS level, a lighter bar behind it
// for the peak, a peak-hold line and a clip lamp at the top (click the meter to clear it).
class VuMeter : public juce::Component
{
public:
    // Call at the display rate with the peak since the last call and the current RMS (both linear, 1.0 = 0 dBFS).
    void setLevels (float peak, float rms);

    // 0 at -60 dB and below, 1 at +6 dB and above.
    static float levelToPosition (float linear) noexcept;


    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { clipped = false; peakHold = 0.0f; repaint(); }

private:
    float peakShown = 0.0f;
    float rmsShown = 0.0f;
    float peakHold = 0.0f;
    int holdFrames = 0;
    bool clipped = false;
};
} // namespace sls
