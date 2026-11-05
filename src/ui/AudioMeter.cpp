#include "ui/AudioMeter.h"

namespace sls
{
void AudioMeter::setLevel (float newPeak)
{
    // Instant attack, smooth decay.
    level = newPeak > level ? newPeak : level * 0.85f;
    repaint();
}

void AudioMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff1b1d22));
    g.fillRoundedRectangle (bounds, 4.0f);

    const float clamped = juce::jlimit (0.0f, 1.0f, level);
    auto bar = bounds.reduced (3.0f);
    bar = bar.removeFromBottom (bar.getHeight() * clamped);

    g.setColour (clamped > 0.9f ? juce::Colours::red
                                : clamped > 0.7f ? juce::Colours::orange
                                                 : juce::Colour (0xff4cc38a));
    g.fillRect (bar);
}
} // namespace sls
