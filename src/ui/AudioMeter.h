#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// =============================================================================
// AudioMeter: VU Meter stile FL Studio (barra orizzontale con gradazioni)
// Aggiornato dal timer a ~30fps, non nel callback audio.
// =============================================================================

class AudioMeter : public juce::Component {
public:
    AudioMeter();

    /** Imposta il livello di picco da visualizzare (0.0..1.0+) */
    void setPeakLevel(float level) noexcept;

    /** @return true se c'è clip (>= 0 dBFS) */
    bool isClipping() const noexcept { return clipping_; }

private:
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Timer per aggiornare il meter a ~30fps (non real-time)
    void timerCallback() override;

    float peakLevel_ = 0.0f;
    bool clipping_ = false;

    // Smoothing: media mobile esponenziale per evitare flickering
    float smoothedPeak_ = 0.0f;
    static constexpr float kSmoothingAlpha = 0.15f;  // ~150ms decay

    void updateLevel() noexcept;
};