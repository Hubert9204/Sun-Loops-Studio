#include "ui/MetersView.h"

#include <cmath>

namespace sls
{
namespace
{
const juce::Colour kBg { 0xff1b1e24 };
const juce::Colour kPanel { 0xff22262e };
const juce::Colour kAccent { 0xffe8943a };
const juce::Colour kGood { 0xff6cc08b };
const juce::Colour kBad { 0xffd9534f };
constexpr double kLowHz = 20.0, kHighHz = 20000.0;
constexpr float kFloorDb = -90.0f;
constexpr int kGonioPoints = 1024;
}

MetersView::MetersView (AudioEngine& engineToWatch) : engine (engineToWatch)
{
    left.resize (SpectrumAnalyser::kFftSize);
    right.resize (SpectrumAnalyser::kFftSize);
    mono.resize (SpectrumAnalyser::kFftSize);
    bins.resize (SpectrumAnalyser::kBins);
    shown.assign (SpectrumAnalyser::kBins, kFloorDb);
    held.assign (SpectrumAnalyser::kBins, kFloorDb);

    resetButton.setTooltip ("Start a new loudness measurement (integrated loudness, range and peaks)");
    resetButton.onClick = [this]
    {
        engine.resetLoudness();
        std::fill (held.begin(), held.end(), kFloorDb);
    };
    holdButton.setClickingTogglesState (true);
    holdButton.setToggleState (true, juce::dontSendNotification);
    holdButton.setTooltip ("Show the highest level reached at each frequency");
    holdButton.setMouseClickGrabsKeyboardFocus (false);
    resetButton.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (resetButton);
    addAndMakeVisible (holdButton);
    startTimerHz (30);
}

juce::String MetersView::lufsText (float v)
{
    return v <= LoudnessMeter::kSilence + 0.5f ? juce::String ("-inf") : juce::String (v, 1);
}

juce::String MetersView::dbText (float v)
{
    return v <= LoudnessMeter::kSilence + 0.5f ? juce::String ("-inf") : juce::String (v, 1);
}

void MetersView::resized()
{
    auto area = getLocalBounds().reduced (10);
    loudnessArea = area.removeFromLeft (210);
    area.removeFromLeft (10);
    gonioArea = area.removeFromRight (std::min (220, area.getWidth() / 3));
    area.removeFromRight (10);
    spectrumArea = area;

    auto buttons = loudnessArea.removeFromBottom (26);
    resetButton.setBounds (buttons.removeFromLeft (80));
    loudnessArea.removeFromBottom (6);
    holdButton.setBounds (spectrumArea.getX(), spectrumArea.getBottom() - 24, 90, 24);
    spectrumArea.removeFromBottom (30);
}

void MetersView::timerCallback()
{
    updateSpectrum();
    repaint();
}

void MetersView::updateSpectrum()
{
    copied = engine.getOutputRing().latest (left.data(), right.data(), SpectrumAnalyser::kFftSize);
    for (int i = 0; i < copied; ++i)
        mono[static_cast<size_t> (i)] = 0.5f * (left[static_cast<size_t> (i)] + right[static_cast<size_t> (i)]);

    const int tail = std::min (copied, 2048);
    correlation = tail > 0 ? stereoCorrelation (left.data() + (copied - tail), right.data() + (copied - tail), tail) : 0.0f;

    if (copied < SpectrumAnalyser::kFftSize / 2)
        return;
    // a short history is padded with zeros at the end; analyse() handles it
    analyser->analyse (mono.data(), copied, bins.data());

    const bool hold = holdButton.getToggleState();
    for (size_t b = 0; b < bins.size(); ++b)
    {
        shown[b] = bins[b] > shown[b] ? bins[b] : std::max (bins[b], shown[b] - 1.2f); // falls 36 dB per second
        held[b] = hold ? std::max (held[b], bins[b]) : kFloorDb;
    }
}

void MetersView::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    paintLoudness (g, loudnessArea);
    paintSpectrum (g, spectrumArea);
    paintGoniometer (g, gonioArea);
}

void MetersView::paintLoudness (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& m = engine.getLoudness();
    g.setColour (kPanel);
    g.fillRoundedRectangle (area.toFloat(), 5.0f);
    area = area.reduced (10, 8);

    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Loudness (LUFS)", area.removeFromTop (20), juce::Justification::centredLeft);

    const auto row = [&] (const juce::String& name, const juce::String& value, juce::Colour colour, float bar)
    {
        auto r = area.removeFromTop (42);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (name, r.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (colour);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText (value, r.removeFromTop (22), juce::Justification::centredLeft);
        if (bar >= 0.0f)
        {
            auto b = r.removeFromBottom (4).toFloat();
            g.setColour (juce::Colour (0xff343841));
            g.fillRect (b);
            g.setColour (colour);
            g.fillRect (b.withWidth (b.getWidth() * juce::jlimit (0.0f, 1.0f, bar)));
        }
        area.removeFromTop (2);
    };
    const auto barFor = [] (float lufs) { return lufs <= LoudnessMeter::kSilence + 0.5f ? 0.0f : (lufs + 60.0f) / 60.0f; };

    const float momentary = m.momentary(), shortTerm = m.shortTerm(), integrated = m.integrated();
    const auto target = [] (float v) { return v <= LoudnessMeter::kSilence + 0.5f ? juce::Colours::white.withAlpha (0.5f)
                                                                                   : (std::abs (v + 23.0f) <= 1.0f ? kGood : kAccent); };
    row ("Momentary (400 ms)", lufsText (momentary), kAccent, barFor (momentary));
    row ("Short-term (3 s)", lufsText (shortTerm), kAccent, barFor (shortTerm));
    row ("Integrated", lufsText (integrated), target (integrated), barFor (integrated));
    row ("Loudness range (LU)", juce::String (m.range(), 1), juce::Colours::white, -1.0f);
    const float tp = m.truePeakDb();
    row ("True peak (dBTP)", dbText (tp), tp > -1.0f ? kBad : juce::Colours::white, -1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.setFont (juce::FontOptions (10.5f));
    g.drawFittedText ("EBU R128 target: -23 LUFS (green within 1 LU), true peak at most -1 dBTP. The reading is of the output, "
                      "after the master volume.", area.removeFromTop (60), juce::Justification::topLeft, 4);
}

void MetersView::paintSpectrum (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (area.toFloat(), 5.0f);
    auto plot = area.reduced (36, 22).withTrimmedBottom (4);

    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Spectrum", area.getX() + 10, area.getY() + 2, 120, 18, juce::Justification::centredLeft);

    const double sr = std::max (8000.0, engine.getDeviceManager().getCurrentAudioDevice() != nullptr
                                            ? engine.getDeviceManager().getCurrentAudioDevice()->getCurrentSampleRate() : 48000.0);
    const auto xFor = [&] (double hz) { return static_cast<float> (plot.getX() + std::log (hz / kLowHz) / std::log (kHighHz / kLowHz) * plot.getWidth()); };
    const auto yFor = [&] (float db) { return static_cast<float> (plot.getBottom()) - juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / (0.0f - kFloorDb)) * static_cast<float> (plot.getHeight()); };

    g.setFont (juce::FontOptions (10.0f));
    for (const float db : { 0.0f, -20.0f, -40.0f, -60.0f, -80.0f })
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine (static_cast<int> (yFor (db)), static_cast<float> (plot.getX()), static_cast<float> (plot.getRight()));
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawText (juce::String (static_cast<int> (db)), area.getX() + 2, static_cast<int> (yFor (db)) - 7, 30, 14, juce::Justification::centredRight);
    }
    for (const double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawVerticalLine (static_cast<int> (xFor (hz)), static_cast<float> (plot.getY()), static_cast<float> (plot.getBottom()));
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawText (hz >= 1000.0 ? juce::String (static_cast<int> (hz) / 1000) + "k" : juce::String (static_cast<int> (hz)),
                    static_cast<int> (xFor (hz)) - 20, plot.getBottom() + 2, 40, 14, juce::Justification::centred);
    }

    // For every pixel column: the loudest bin that falls in it (or the interpolated value between two bins at low frequencies).
    const auto valueAt = [&] (const std::vector<float>& v, double hzLow, double hzHigh)
    {
        const double binWidth = sr / SpectrumAnalyser::kFftSize;
        const int b0 = static_cast<int> (std::floor (hzLow / binWidth)), b1 = static_cast<int> (std::floor (hzHigh / binWidth));
        if (b1 <= b0)
        {
            const double pos = 0.5 * (hzLow + hzHigh) / binWidth;
            const int i = juce::jlimit (0, SpectrumAnalyser::kBins - 2, static_cast<int> (pos));
            const float f = static_cast<float> (pos - i);
            return v[static_cast<size_t> (i)] + f * (v[static_cast<size_t> (i + 1)] - v[static_cast<size_t> (i)]);
        }
        float best = kFloorDb;
        for (int b = std::max (1, b0); b <= std::min (SpectrumAnalyser::kBins - 1, b1); ++b)
            best = std::max (best, v[static_cast<size_t> (b)]);
        return best;
    };

    juce::Path fill, line;
    bool started = false;
    for (int x = plot.getX(); x <= plot.getRight(); ++x)
    {
        const double t0 = (x - 0.5 - plot.getX()) / plot.getWidth(), t1 = (x + 0.5 - plot.getX()) / plot.getWidth();
        const double hz0 = kLowHz * std::pow (kHighHz / kLowHz, std::clamp (t0, 0.0, 1.0));
        const double hz1 = kLowHz * std::pow (kHighHz / kLowHz, std::clamp (t1, 0.0, 1.0));
        const float y = yFor (valueAt (shown, hz0, hz1));
        if (! started)
        {
            fill.startNewSubPath (static_cast<float> (x), static_cast<float> (plot.getBottom()));
            fill.lineTo (static_cast<float> (x), y);
            line.startNewSubPath (static_cast<float> (x), y);
            started = true;
        }
        else
        {
            fill.lineTo (static_cast<float> (x), y);
            line.lineTo (static_cast<float> (x), y);
        }
    }
    fill.lineTo (static_cast<float> (plot.getRight()), static_cast<float> (plot.getBottom()));
    fill.closeSubPath();
    g.setColour (kAccent.withAlpha (0.25f));
    g.fillPath (fill);
    g.setColour (kAccent);
    g.strokePath (line, juce::PathStrokeType (1.4f));

    if (holdButton.getToggleState())
    {
        juce::Path peak;
        bool first = true;
        for (int x = plot.getX(); x <= plot.getRight(); x += 2)
        {
            const double t0 = (x - 1.0 - plot.getX()) / plot.getWidth(), t1 = (x + 1.0 - plot.getX()) / plot.getWidth();
            const double hz0 = kLowHz * std::pow (kHighHz / kLowHz, std::clamp (t0, 0.0, 1.0));
            const double hz1 = kLowHz * std::pow (kHighHz / kLowHz, std::clamp (t1, 0.0, 1.0));
            const float y = yFor (valueAt (held, hz0, hz1));
            if (first) { peak.startNewSubPath (static_cast<float> (x), y); first = false; }
            else peak.lineTo (static_cast<float> (x), y);
        }
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.strokePath (peak, juce::PathStrokeType (1.0f));
    }
}

void MetersView::paintGoniometer (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (area.toFloat(), 5.0f);
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Goniometer", area.getX() + 10, area.getY() + 2, 140, 18, juce::Justification::centredLeft);

    auto inner = area.reduced (10).withTrimmedTop (18);
    auto meterRow = inner.removeFromBottom (44);
    const int side = std::min (inner.getWidth(), inner.getHeight());
    const auto square = juce::Rectangle<int> (side, side).withCentre (inner.getCentre());
    const auto centre = square.getCentre().toFloat();
    const float radius = side * 0.5f;

    g.setColour (juce::Colour (0xff15171c));
    g.fillRect (square);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawVerticalLine (static_cast<int> (centre.x), static_cast<float> (square.getY()), static_cast<float> (square.getBottom()));
    g.drawHorizontalLine (static_cast<int> (centre.y), static_cast<float> (square.getX()), static_cast<float> (square.getRight()));
    g.drawLine (centre.x - radius, centre.y + radius, centre.x + radius, centre.y - radius);
    g.drawLine (centre.x - radius, centre.y - radius, centre.x + radius, centre.y + radius);
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("M", square.getX(), square.getY(), 12, 12, juce::Justification::centred);
    g.drawText ("L", square.getX() + 2, square.getBottom() - 14, 12, 12, juce::Justification::centred);
    g.drawText ("R", square.getRight() - 14, square.getBottom() - 14, 12, 12, juce::Justification::centred);

    // The newest samples: x = side signal, y = mid signal (a mono signal is a vertical line; left only leans left).
    const int count = std::min (copied, kGonioPoints);
    for (int i = 0; i < count; ++i)
    {
        const size_t idx = static_cast<size_t> (copied - count + i);
        const float l = left[idx], r = right[idx];
        const float x = centre.x + (r - l) * 0.5f * radius; // a mono signal at full scale reaches the top or the bottom edge
        const float y = centre.y - (l + r) * 0.5f * radius;
        const float age = static_cast<float> (i) / static_cast<float> (count);
        g.setColour (kGood.withAlpha (0.15f + 0.6f * age));
        g.fillRect (juce::jlimit (static_cast<float> (square.getX()), static_cast<float> (square.getRight() - 1), x),
                    juce::jlimit (static_cast<float> (square.getY()), static_cast<float> (square.getBottom() - 1), y), 1.6f, 1.6f);
    }

    // Correlation: -1 (out of phase) .. +1 (mono)
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Correlation  " + juce::String (correlation, 2), meterRow.removeFromTop (16), juce::Justification::centredLeft);
    auto bar = meterRow.removeFromTop (10).toFloat();
    g.setColour (juce::Colour (0xff343841));
    g.fillRect (bar);
    const float zero = bar.getCentreX();
    const float pos = zero + correlation * bar.getWidth() * 0.5f;
    g.setColour (correlation < 0.0f ? kBad : kGood);
    g.fillRect (juce::Rectangle<float> (std::min (zero, pos), bar.getY(), std::abs (pos - zero), bar.getHeight()));
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawVerticalLine (static_cast<int> (zero), bar.getY(), bar.getBottom());
}

MetersWindow::MetersWindow (AudioEngine& engine)
    : juce::DocumentWindow ("Master meters", juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton), content (engine)
{
    setUsingNativeTitleBar (false);
    setContentNonOwned (&content, false);
    setResizable (true, true);
    setResizeLimits (640, 360, 1800, 1000);
    centreWithSize (900, 430);
    setAlwaysOnTop (true);
}
} // namespace sls
