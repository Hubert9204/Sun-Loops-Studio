#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// =============================================================================
// NoteComponent: componente disegnabile per una nota.
// Usato nel piano roll editor.
// =============================================================================

class NoteComponent : public juce::Component {
public:
    NoteComponent(int pitch, int start, int duration);

    /** @return posizione in pixel (X) della nota */
    int getX() const noexcept { return x_; }

    /** @return altezza in pixel della nota */
    int getHeight() const noexcept { return height_; }

    /** @return pitch MIDI della nota */
    int getPitch() const noexcept { return pitch_; }

    /** @return durata in samples della nota */
    int getDuration() const noexcept { return duration_; }

private:
    void paint(juce::Graphics& g) override;
    void resized() override;

    int x_ = 0;
    int y_ = 0;
    int width_ = 20;
    int height_ = 10;
    int pitch_ = 60; // Middle C
    int duration_ = 1000; // samples

    static constexpr int kNoteHeight = 10;
    static constexpr int kNoteWidth = 20;
};