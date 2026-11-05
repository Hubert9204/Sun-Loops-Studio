#include "ui/VuMeter.h"

#include <cmath>

namespace sls
{
namespace
{
constexpr float kFloorDb = -60.0f;
constexpr float kCeilDb = 6.0f;
constexpr int kHoldFrames = 45; // 1.5 s at 30 Hz

const juce::Colour kGreen { 0xff4cc38a };
const juce::Colour kYellow { 0xffe0b030 };
const juce::Colour kRed { 0xffd9534f };
} // namespace

float VuMeter::levelToPosition (float linear) noexcept
{
    if (! (linear > 0.0f))
        return 0.0f;

    const float db = 20.0f * std::log10 (linear);
    return juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / (kCeilDb - kFloorDb));
}

void VuMeter::setLevels (float peak, float rms)
{
    peak = std::isfinite (peak) ? std::max (0.0f, peak) : 0.0f;
    rms = std::isfinite (rms) ? std::max (0.0f, rms) : 0.0f;

    peakShown = peak > peakShown ? peak : peakShown * 0.85f;
    rmsShown = rms;

    if (peak >= peakHold)
    {
        peakHold = peak;
        holdFrames = kHoldFrames;
    }
    else if (holdFrames > 0)
        --holdFrames;
    else
        peakHold *= 0.9f;

    if (peak >= 1.0f)
        clipped = true;

    repaint();
}

void VuMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff15181c));
    g.fillRoundedRectangle (bounds, 3.0f);

    auto inner = bounds.reduced (2.0f);
    inner.removeFromTop (6.0f); // clip lamp

    auto colourAt = [] (float position)
    {
        const float db = kFloorDb + position * (kCeilDb - kFloorDb);
        return db > -3.0f ? kRed : (db > -12.0f ? kYellow : kGreen);
    };

    auto barTo = [&] (float level) { return inner.getHeight() * levelToPosition (level); };

    // peak (lighter, behind)
    {
        const float h = barTo (peakShown);
        g.setColour (colourAt (levelToPosition (peakShown)).withAlpha (0.35f));
        g.fillRect (inner.getX(), inner.getBottom() - h, inner.getWidth(), h);
    }
    // RMS (solid), coloured in bands so the bar shows where it enters the hot zone
    {
        const float h = barTo (rmsShown);
        const float bottom = inner.getBottom();
        const float yellowStart = inner.getHeight() * ((-12.0f - kFloorDb) / (kCeilDb - kFloorDb));
        const float redStart = inner.getHeight() * ((-3.0f - kFloorDb) / (kCeilDb - kFloorDb));

        auto band = [&] (float from, float to, juce::Colour c)
        {
            const float top = std::min (h, to);
            if (top > from)
            {
                g.setColour (c);
                g.fillRect (inner.getX(), bottom - top, inner.getWidth(), top - from);
            }
        };
        band (0.0f, yellowStart, kGreen);
        band (yellowStart, redStart, kYellow);
        band (redStart, inner.getHeight(), kRed);
    }

    // scale ticks at 0, -6, -12, -24, -48 dB
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
    {
        const float y = inner.getBottom() - inner.getHeight() * (db - kFloorDb) / (kCeilDb - kFloorDb);
        g.drawHorizontalLine (juce::roundToInt (y), inner.getX(), inner.getX() + (db > -1.0f ? inner.getWidth() : 3.0f));
    }

    // peak hold
    if (peakHold > 0.0f)
    {
        g.setColour (peakHold >= 1.0f ? kRed : juce::Colours::white);
        g.fillRect (inner.getX(), inner.getBottom() - barTo (peakHold) - 1.0f, inner.getWidth(), 2.0f);
    }

    // clip lamp
    g.setColour (clipped ? kRed : juce::Colour (0xff3a2024));
    g.fillRect (bounds.getX() + 2.0f, bounds.getY() + 2.0f, bounds.getWidth() - 4.0f, 3.0f);
}
} // namespace sls
