#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/AudioEngine.h"
#include "audio/SampleEdit.h"
#include "audio/Spectrum.h"

namespace sls
{
// The sample editor: the waveform of one sample with a selection, a preview that plays through the audio engine, the edits of
// SampleEdit.h (cut, crop, silence, fades, reverse, invert, gain, normalise, DC offset, noise reduction) with undo and redo,
// a spectrum analyser that follows the playback, and slice markers (found by detecting where sounds start, or equal parts).
// It works on its own copy: nothing outside changes until the owner's `onApply` accepts the result.
class SampleEditor : public juce::Component, private juce::Timer
{
public:
    explicit SampleEditor (AudioEngine& engineToUse);
    ~SampleEditor() override;

    // Loads the sample to edit. `canSlice`: the owner can cut the result into pieces at the markers. `applyText`: label of the OK button.
    void setSample (SamplePtr sample, bool canSlice, const juce::String& applyText);

    // Called when the user presses the apply button: the edited sample and the slice positions (seconds, ascending, strictly inside).
    // Returns an error message, or an empty string when it worked (the owner then closes the editor).
    std::function<juce::String (SamplePtr edited, const std::vector<double>& slices)> onApply;
    std::function<void()> onClose;
    // Set (before setSample) to offer "Slices to sampler channel": the edited sample and the marker positions (seconds). Returns an error, or ""
    // when it worked (the editor then closes). Markers can then be placed even when `canSlice` is false.
    std::function<juce::String (SamplePtr edited, const std::vector<double>& markers)> onSlicesToSampler;

    bool hasChanges() const noexcept { return ! undoStack.empty() || ! markers.empty(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Peaks
    {
        static constexpr int kFrames = 64; // frames per entry
        std::vector<float> lowL, highL, lowR, highR;
    };

    void timerCallback() override;
    void rebuildPeaks();
    void changeSample (SamplePtr next, const juce::String& message, bool lengthChanged);
    void edit (const juce::String& what, const std::function<SamplePtr (const SamplePtr&, FrameRange)>& op, bool changesLength = false);
    void undo();
    void redo();
    void trimUndoMemory();
    void play();
    void stopPlayback();
    void updateSpectrum();
    void updateButtons();
    void setStatus (const juce::String& text);
    void ask (const juce::String& title, const juce::String& text, const juce::StringArray& fieldNames, const juce::StringArray& defaults,
              std::function<void (const juce::StringArray&)> then);
    void reduceNoiseDialog();
    void sliceByEqualParts();
    void findHits();
    void toggleMarkerAt (double frame, double tolerancePixels);
    void applyAndClose();

    FrameRange selectionOrAll() const;
    bool hasSelection() const noexcept { return selEnd > selBegin; }
    double frameAtX (int x) const noexcept;
    int xAtFrame (double frame) const noexcept;
    void zoomTo (double newFramesPerPixel, int anchorX);
    void fitView();
    void clampView();
    juce::String timeText (double frame) const;

    juce::Rectangle<int> waveArea, rulerArea, spectrumArea;

    AudioEngine& engine;
    SamplePtr current;
    std::vector<SamplePtr> undoStack, redoStack;
    Peaks peaks;
    bool canSlice = false;
    bool canMark = false; // markers can be placed (for slicing a clip, or for slices to a sampler channel)

    // view and selection, in frames
    double viewStart = 0.0, framesPerPixel = 100.0;
    double cursor = 0.0;
    int selBegin = 0, selEnd = 0;
    int dragAnchorX = 0;
    double dragAnchorFrame = 0.0;
    bool dragging = false;
    std::vector<double> markers; // seconds, ascending

    // spectrum
    std::unique_ptr<SpectrumAnalyser> analyser = std::make_unique<SpectrumAnalyser>();
    std::vector<float> mono, bins, shown;
    bool wasPlaying = false;

    juce::String status;
    juce::Label statusLabel;
    juce::TextButton playButton { "Play" }, loopButton { "Loop" }, undoButton { "Undo" }, redoButton { "Redo" }, allButton { "Select all" };
    juce::TextButton cutButton { "Cut" }, cropButton { "Crop" }, silenceButton { "Silence" }, reverseButton { "Reverse" }, invertButton { "Invert" },
        gainButton { "Gain..." }, normaliseButton { "Normalise..." }, dcButton { "Remove DC" }, fadeInButton { "Fade in" }, fadeOutButton { "Fade out" },
        noiseButton { "Reduce noise..." };
    juce::ComboBox fadeShapeBox;
    juce::Slider sensitivitySlider;
    juce::TextButton hitsButton { "Find hits" }, partsButton { "Equal parts..." }, clearMarkersButton { "Clear markers" };
    juce::TextButton applyButton { "Apply" }, cancelButton { "Cancel" };
    juce::TextButton toSamplerButton { "Slices to sampler channel" };
    juce::Label sensitivityLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleEditor)
};

class SampleEditorWindow : public juce::DocumentWindow
{
public:
    explicit SampleEditorWindow (AudioEngine& engine);
    ~SampleEditorWindow() override { clearContentComponent(); }

    SampleEditor& editor() noexcept { return content; }
    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    SampleEditor content;
};
} // namespace sls
