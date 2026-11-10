#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/SamplerProgram.h"

namespace sls
{
// The Slicer (0.37.0), as FL Studio's Slicex: the whole loop with its slice markers, and the settings of the selected slice.
// Click a slice to hear it (it plays on its key through the channel); drag a marker to move it, double-click to add one,
// right-click a marker to delete it. Slicing again: by sound (where sounds start, with a sensitivity), by the grid at the
// loop's tempo, or in equal parts. Every change is applied at once (onChanged); "Write the loop into the pattern" puts one note
// per slice into the current pattern, timed at the loop's tempo.
struct SlicerState
{
    std::shared_ptr<const SampleData> whole; // the loop
    std::vector<int> starts { 0 };           // slice starts (frames), sorted, the first 0
    std::vector<SliceSettings> settings;     // per slice (may be shorter)
    bool cut = true;
    double loopBpm = 0.0;
    int firstKey = 60;
};

class SlicerPanel : public juce::Component
{
public:
    SlicerPanel();
    ~SlicerPanel() override;

    void setState (SlicerState s); // shows a channel's slices (keeps the selected slice when it still exists)
    const SlicerState& state() const noexcept { return current; }

    std::function<void (const SlicerState&)> onChanged; // after every edit
    std::function<void (int key)> onPlayKey;            // a slice clicked: play its key...
    std::function<void (int key)> onStopKey;            // ...until the mouse is released
    std::function<void (const SlicerState&)> onWriteScore;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    class Wave;
    void select (int slice);
    void changed (bool reshaped);
    void showSlice();
    void sliceBySound();
    void sliceByGrid();
    void sliceEqually (int parts);
    SliceSettings& settingsOf (int slice);

    SlicerState current;
    int selected = 0;
    std::unique_ptr<Wave> wave;

    juce::TextButton bySoundButton { "Slice by sound" }, byGridButton { "Slice by grid" }, equalButton { "Equal parts..." };
    juce::Slider sensitivity, tempo;
    juce::Label sensitivityLabel { {}, "Sensitivity" }, tempoLabel { {}, "Loop tempo" };
    juce::ComboBox gridBox;
    juce::ToggleButton cutButton { "A slice cuts the one before" };

    juce::Label sliceLabel;
    juce::Slider pitch, gain, pan;
    juce::Label pitchLabel { {}, "Pitch" }, gainLabel { {}, "Level" }, panLabel { {}, "Pan" };
    juce::ToggleButton reverseButton { "Reverse" };
    juce::TextButton resetSliceButton { "Reset slice" };
    juce::TextButton writeButton { "Write the loop into the pattern" };
    juce::Label hint;
};

class SlicerWindow : public juce::DocumentWindow
{
public:
    SlicerWindow();
    ~SlicerWindow() override { clearContentComponent(); }
    SlicerPanel& panel() noexcept { return content; }
    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    SlicerPanel content;
};
} // namespace sls
