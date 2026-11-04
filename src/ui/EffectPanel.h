#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// =============================================================================
// EffectPanel: pannello per effetti audio.
// Mostra lista effetti e parametri.
// =============================================================================

class EffectPanel : public juce::Component {
public:
    EffectPanel();
    ~EffectPanel() override;

private:
    void paint(juce::Graphics& g) override;
    void resized() override;

    // --- UI Elements ---
    std::unique_ptr<juce::Label> titleLabel_;
    std::unique_ptr<juce::ListBox> effectsList_;
    std::unique_ptr<juce::GroupComponent> effectParams_; // Parametri effetto selezionato
};