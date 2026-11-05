#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Project.h"
#include "ui/AudioMeter.h"

namespace sls
{
// Sampler channels: a row of 16th-note step buttons (the FL-style step sequencer).
// Synth channels: a miniature, read-only view of the notes (edit them in the piano roll).
class StepGrid : public juce::Component
{
public:
    explicit StepGrid (Channel& channelToEdit) : channel (channelToEdit) {}

    void setLoopLengthTicks (int ticks) { loopLengthTicks = ticks; repaint(); }
    void setPlayheadTick (int tick);

    std::function<void()> onStepsEdited;   // a step was toggled
    std::function<void()> onClickedPreview; // the preview of a synth channel was clicked

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    static constexpr int kStepTicks = kTicksPerBeat / 4;

    int stepCount() const noexcept { return std::max (1, loopLengthTicks / kStepTicks); }
    int stepAt (int x) const noexcept;

    Channel& channel;
    int loopLengthTicks = 8 * kTicksPerBeat;
    int playheadTick = -1;
};

// One row of the channel rack, doubling as the channel's mixer strip.
class ChannelStrip : public juce::Component
{
public:
    ChannelStrip (Channel& channelToControl, int channelIndex);

    void setSelected (bool shouldBeSelected);
    void setLoopLengthTicks (int ticks) { grid.setLoopLengthTicks (ticks); }
    void setPlayheadTick (int tick) { grid.setPlayheadTick (tick); }
    void setMeterLevel (float level) { meter.setLevel (level); }
    void refreshFromChannel();

    int getIndex() const noexcept { return index; }

    std::function<void (int)> onSelect;        // the strip was clicked
    std::function<void (int)> onChanged;       // anything that changes the sound
    std::function<void (int)> onLoadSample;    // "Load" pressed (sampler channels)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    Channel& channel;
    int index;
    bool selected = false;

    juce::Label nameLabel;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider panSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::TextButton loadButton { "Load" };
    AudioMeter meter;
    StepGrid grid;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};
} // namespace sls
