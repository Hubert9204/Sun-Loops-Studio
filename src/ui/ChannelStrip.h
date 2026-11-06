#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Automation.h"
#include "audio/Project.h"
#include "ui/AutomatableSlider.h"
#include "ui/AudioMeter.h"
#include "ui/DragItems.h"
#include "ui/Knob.h"
#include "ui/TrackSelector.h"

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
    std::function<void()> onDoubleClicked;  // the preview of a synth channel was double-clicked
    std::function<void()> onContextMenu;    // right click

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    static constexpr int kStepTicks = kSixteenthTicks;

    int stepCount() const noexcept { return std::max (1, loopLengthTicks / kStepTicks); }
    int stepAt (int x) const noexcept;

    Channel& channel;
    int loopLengthTicks = 8 * kTicksPerBeat;
    int playheadTick = -1;
};

// One row of the channel rack, doubling as the channel's mixer strip.
class ChannelStrip : public juce::Component, public juce::DragAndDropTarget
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
    std::function<void (int)> onEditSample;    // "Edit sample..." chosen (sampler channels with a sample)
    std::function<void (int)> onOpenPianoRoll; // context menu "Piano roll", or double click on the name / note preview
    std::function<void (int)> onOpenDrumMachine; // drum channels: "Drum machine..." in the menu, or a double click (instead of the piano roll)
    std::function<void (int, Instrument)> onInstrumentChosen; // context menu "Instrument"
    std::function<void (int)> onRename;        // context menu "Rename..."
    std::function<void (int)> onDelete;        // context menu "Delete channel..."
    std::function<void (int)> onClone;         // context menu "Clone channel"
    std::function<void (int, int)> onMove;     // context menu "Move up" (-1) / "Move down" (+1)
    std::function<void (int)> onSavePreset;    // context menu "Save as preset..."
    std::function<void (int)> onAssignTrack;   // context menu "Assign free mixer track"
    std::function<void (int, const drag::Item&)> onItemDropped; // something was dropped from the browser
    std::function<void (int, AutomationKind)> onAutomate;       // right click on volume / pan: create an automation clip

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void paintOverChildren (juce::Graphics&) override;

    // juce::DragAndDropTarget
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { setDropHighlight (true); }
    void itemDragExit (const SourceDetails&) override { setDropHighlight (false); }
    void itemDropped (const SourceDetails&) override;

private:
    void showContextMenu();
    void setDropHighlight (bool on);

    Channel& channel;
    int index;
    bool selected = false;

    juce::Label nameLabel;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    AutomatableSlider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    AutomatableSlider panSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    TrackSelector trackSelector { kMaxTracks };
    Knob pitchKnob;
    Knob swingKnob;
    bool dropHighlight = false;
    juce::TextButton loadButton { "Load" };
    AudioMeter meter;
    StepGrid grid;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};
} // namespace sls
