#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/Sequencer.h"

// =============================================================================
// PianoRoll: editor piano roll stile FL Studio.
// Componente principale per creare sequenze musicali.
// =============================================================================

class PianoRoll : public juce::Component {
public:
    PianoRoll();
    ~PianoRoll() override;

private:
    void paint(juce::Graphics& g) override;
    void resized() override;

    // --- UI Components ---
    std::unique_ptr<juce::Label> titleLabel_;
    std::unique_ptr<Sequencer> sequencer_;
    
    // --- Timeline ---
    class Timeline : public juce::Component {
    public:
        Timeline() = default;
        ~Timeline() override = default;

    private:
        void paint(juce::Graphics& g) override;
        void resized() override;
    } timeline_;
};