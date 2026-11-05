#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sls
{
// Simple vertical peak meter with decay, fed from the message thread.
class AudioMeter : public juce::Component
{
public:
    void setLevel (float newPeak);
    void paint (juce::Graphics&) override;

private:
    float level = 0.0f;
};
} // namespace sls
