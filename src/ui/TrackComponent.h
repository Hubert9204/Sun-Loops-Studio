#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// =============================================================================
// TrackComponent: componente per una traccia nel mixer.
// Mostra volume, pan, mute/solo e nomi.
// =============================================================================

class TrackComponent : public juce::Component {
public:
    TrackComponent(const std::string& name);
    ~TrackComponent() override;

private:
    void paint(juce::Graphics& g) override;
    void resized() override;

    std::string name_;
    
    // --- UI Elements ---
    std::unique_ptr<juce::Label> nameLabel_;
    std::unique_ptr<juce::Slider> volumeSlider_;
    std::unique_ptr<juce::Slider> panSlider_;
    std::unique_ptr<juce::TextButton> muteButton_;
    std::unique_ptr<juce::TextButton> soloButton_;

    // --- State ---
    float volume_ = 0.7f;
    float pan_ = 0.0f;
    bool muted_ = false;
    bool solo_ = false;
};