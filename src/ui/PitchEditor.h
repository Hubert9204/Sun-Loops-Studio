#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/PitchTrack.h"

namespace sls
{
class AudioEngine;

// The pitch editor (0.38.0), as FL Studio's Newtone: a monophonic recording shown as notes over its pitch curve. Notes can be
// dragged up and down (by semitones; Alt: freely), double-clicked to bring them in tune, or corrected all at once (towards the
// nearest key, or the nearest key of a scale). Apply writes a new WAV; Notes to pattern turns the line into notes for a channel.
class PitchEditor : public juce::Component
{
public:
    explicit PitchEditor (AudioEngine& engine);
    ~PitchEditor() override;

    void setSample (std::shared_ptr<const SampleData> sample); // analyses it (a second or two for a few minutes of audio)

    // Apply: the edited audio. Returns an error message, or "" when it was used (the window then closes).
    std::function<juce::String (std::shared_ptr<const SampleData>)> onApply;
    // Notes to pattern: the notes (MIDI key, start and end in seconds from the start of the audio).
    std::function<juce::String (const std::vector<pitchtrack::Note>&)> onNotesToPattern;
    std::function<void()> onClose;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    class View;
    void correctSelected (double amount);
    void play();
    void stopPlayback();
    std::shared_ptr<const SampleData> edited(); // rendered on demand, kept until the notes change
    void notesChanged();
    void updateInfo();

    AudioEngine& engine;
    std::shared_ptr<const SampleData> original, cache;
    std::vector<pitchtrack::Frame> frames;
    std::vector<pitchtrack::Note> notes;
    std::vector<bool> selected;
    std::unique_ptr<View> view;

    juce::Slider amount;
    juce::Label amountLabel { {}, "Amount" }, info;
    juce::ComboBox scaleBox, rootBox;
    juce::TextButton correctButton { "Correct" }, resetButton { "Reset" }, playButton { "Play" }, applyButton { "Save as new file and use" },
        midiButton { "Notes to pattern" }, cancelButton { "Cancel" };
};

class PitchEditorWindow : public juce::DocumentWindow
{
public:
    explicit PitchEditorWindow (AudioEngine& engine);
    ~PitchEditorWindow() override { clearContentComponent(); }
    PitchEditor& editor() noexcept { return content; }
    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    PitchEditor content;
};
} // namespace sls
