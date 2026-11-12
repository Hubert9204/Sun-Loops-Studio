#pragma once

#include <functional>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Automation.h"
#include "audio/Project.h"
#include "audio/StepSequence.h"
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

    // The row shows squares (a step sequence: no notes, or only steps) or a preview of the piano roll (0.31.0, see StepSequence.h).
    bool showsSteps() const noexcept { return steps::isStepSequence (channel.notes, steps::stepPitch (channel)); }
    int activeSteps() const noexcept { return steps::activeSteps (channel.notes, channel.steps, loopLengthTicks); }

    std::function<void()> onStepsEdited;      // a step was written or erased, or the active part was resized
    std::function<void()> onOpenEditor;       // the preview was clicked: open the piano roll
    std::function<void()> onContextMenu;      // right click on the preview

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // 0.61.0, as FL Studio's step menu: a step every `every` steps (1 = all of them), or none; true when anything changed.
    bool fillEvery (int every);
    bool clearSteps();

private:
    static constexpr int kStepTicks = steps::kStepTicks;

    int stepCount() const noexcept { return steps::patternSteps (loopLengthTicks); }
    float stepWidth() const noexcept { return static_cast<float> (getWidth()) / static_cast<float> (stepCount()); }
    int stepAt (int x) const noexcept;
    void paintStep (int step, bool on);   // left button writes, right button erases

    enum class Drag
    {
        None,
        Paint,
        Erase
    };

    Channel& channel;
    int loopLengthTicks = 8 * kTicksPerBeat;
    int playheadTick = -1;
    Drag drag = Drag::None;
    int lastStep = -1;
};

// One row of the channel rack, doubling as the channel's mixer strip.
class ChannelStrip : public juce::Component, public juce::DragAndDropTarget
{
public:
    ChannelStrip (Channel& channelToControl, int channelIndex);

    void setSelected (bool shouldBeSelected);
    void setLoopLengthTicks (int ticks) { grid.setLoopLengthTicks (ticks); }
    void setPlayheadTick (int tick) { grid.setPlayheadTick (tick); }
    void repaintGrid() { grid.repaint(); }
    void setMeterLevel (float level) { meter.setLevel (level); }
    void refreshFromChannel();
    void showAutomated (AutomationKind kind, float value); // volume or pan as automation has it while the song plays (0.47.0)
    void setTrackCount (int mixerTracks) { trackSelector.setNumTracks (mixerTracks); } // the project's (0.29.0)

    int getIndex() const noexcept { return index; }

    std::function<void (int)> onSelect;        // the strip was clicked
    std::function<void (int)> onChanged;       // anything that changes the sound
    std::function<void (int)> onLoadSample;    // "Load" pressed (sampler channels)
    std::function<void (int)> onEditSample;    // "Edit sample..." chosen (sampler channels with a sample)
    std::function<void (int)> onOpenPianoRoll; // context menu "Piano roll", or double click on the name / note preview
    std::function<void (int)> onOpenDrumMachine; // drum channels: "Drum machine..." in the menu, or a double click (instead of the piano roll)
    std::function<void (int, Instrument)> onInstrumentChosen; // context menu "Instrument"
    std::function<void (int)> onOpenPlugin;      // plugin channels: "Plugin window..." in the menu, or a double click (0.17.0)
    std::function<std::vector<std::pair<juce::String, juce::String>>()> listInstrumentPlugins; // name, identifier (from the last scan)
    std::function<void (int, const juce::String&)> onPluginChosen; // context menu "Instrument > Plugin instruments > ..."
    std::function<void (int)> onRename;        // context menu "Rename..."
    std::function<void (int)> onDelete;        // context menu "Delete channel..."
    std::function<void (int)> onSamplerSettings; // context menu "Sampler settings..." (sampler channels)
    std::function<void (int)> onOpenSlicer;      // context menu "Slicer..." (a sampler channel with a sample or slices, 0.37.0)
    std::function<void (int)> onSynthSettings;   // context menu "Synth settings..." (synth channels, 0.18.0)
    std::function<void (int)> onLoadSoundFont;   // context menu "Load SoundFont (.sf2)..."
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
    TrackSelector trackSelector { kDefaultTracks };
    Knob pitchKnob;
    Knob swingKnob;
    bool dropHighlight = false;
    juce::TextButton loadButton { "Load" };
    AudioMeter meter;
    StepGrid grid;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};
} // namespace sls
