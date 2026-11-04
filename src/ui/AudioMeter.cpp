#include "AudioMeter.h"

// =============================================================================
// AudioMeter implementation
// Stile FL Studio: sfondo scuro, barra verde → giallo → rossa,
// gradazioni in dBFS, indicatori di clip.
// =============================================================================

AudioMeter::AudioMeter() {
    startTimerHz(30);
}

void AudioMeter::setPeakLevel(float level) noexcept {
    peakLevel_ = level;
    clipping_ = (level >= 1.0f);
    repaint();
}

void AudioMeter::updateLevel() noexcept {
    // Smoothing esponenziale per evitare flickering
    float raw = std::min(peakLevel_, 2.0f);  // max +6dB per visualizzazione
    smoothedPeak_ = kSmoothingAlpha * raw + (1.0f - kSmoothingAlpha) * smoothedPeak_;
}

void AudioMeter::paint(juce::Graphics& g) {
    updateLevel();

    auto bounds = getLocalBounds();
    const int height = bounds.getHeight();
    const int width = bounds.getWidth();

    // Sfondo scuro (stile FL Studio)
    g.fillAll(juce::Colours::darkgrey.withAlpha(0.8f));

    float levelNorm = std::clamp(smoothedPeak_, 0.0f, 1.0f);

    // Colori stile FL Studio VU meter
    const juce::Colour green   = juce::Colour(0x00ff44);   // < -12 dB
    const juce::Colour yellow  = juce::Colour(0xffcc00);   // -12 a -3 dB
    const juce::Colour red     = juce::Colour(0xff2200);   // > -3 dB
    const juce::Colour clipCol = juce::Colour(0xff0000);   // clip

    // Barra del livello (orizzontale)
    int barWidth = static_cast<int>(levelNorm * width);

    // Gradazioni: verde (0..60%), giallo (60..85%), rosso (85..100%)
    const int greenEnd   = static_cast<int>(width * 0.60f);
    const int yellowEnd  = static_cast<int>(width * 0.85f);

    if (barWidth > 0) {
        // Sezione verde
        int greenWidth = std::min(barWidth, greenEnd);
        if (greenWidth > 0) {
            g.setColour(green.withAlpha(0.9f));
            g.fillRect(0, 0, greenWidth, height);
        }

        // Sezione gialla
        int yellowStart = greenEnd;
        int yellowWidth = std::max(0, std::min(barWidth - yellowStart, yellowEnd - greenEnd));
        if (yellowWidth > 0) {
            g.setColour(yellow.withAlpha(0.9f));
            g.fillRect(yellowStart, 0, yellowWidth, height);
        }

        // Sezione rossa
        int redStart = yellowEnd;
        int redWidth = std::max(0, barWidth - redStart);
        if (redWidth > 0) {
            g.setColour(red.withAlpha(0.9f));
            g.fillRect(redStart, 0, redWidth, height);
        }
    }

    // Clip indicator
    if (clipping_) {
        g.setColour(clipCol.brighter());
        g.fillRect(width - 4, 0, 4, height);
    }

    // Bordo
    g.setColour(juce::Colours::grey.withAlpha(0.5f));
    g.drawRect(bounds, 1);

    // Etichetta dBFS
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.setFont(static_cast<float>(height) * 0.4f);
    int dbX = width - width / 6;
    g.drawText("dBFS", juce::Rectangle<int>(dbX, 0, width / 6, height), juce::Justification::centred);
}

void AudioMeter::timerCallback() {
    repaint();
}

void AudioMeter::resized() {
    // Il meter occupa l'intera area assegnata
}
