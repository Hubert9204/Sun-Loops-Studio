#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/AudioEngine.h"
#include "audio/DrumKit.h"
#include "audio/Project.h"
#include "library/DrumKitFile.h"
#include "ui/DragItems.h"

namespace sls
{
// The drum machine of one drum channel: sixteen pads (click to hear and select), what the selected pad is made of (up to four
// sound layers picked by velocity, level, pan, tuning, swing, choke group, mute and solo), a kit menu, and a step sequencer with one
// row per pad that edits the channel's notes of the current pattern (pad n = note 36 + n, so the piano roll shows the same notes).
//
// The view never keeps a copy of the channel: `getChannel` returns the live channel (nullptr when it is gone or no longer a drum channel),
// edits to a kit are made on a copy that replaces `Channel::kit`, and the host is told through the callbacks.
class DrumMachineView : public juce::Component, public juce::DragAndDropTarget
{
public:
    explicit DrumMachineView (AudioEngine& engineToUse);

    std::function<Channel*()> getChannel;
    std::function<void()> onKitChanged;  // pads or the kit changed: publish the project
    std::function<void()> onNotesChanged; // steps changed: publish the project, update the piano roll, undo
    std::function<void (const juce::String&)> onStatus;

    void setChannelIndex (int index) noexcept { channelIndex = index; }
    void setLoopTicks (int ticks);
    void setPlayheadTick (int tick);
    void setKitsFolder (const juce::File& folder) { kitsFolder = folder; }
    void refresh(); // the channel changed from outside (another kit, another pattern)

    void selectPad (int pad);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    const DrumKit* kit() const;
    void editKit (const std::function<void (DrumKit&)>& change);
    void editPad (const std::function<void (DrumPad&)>& change) { editKit ([this, &change] (DrumKit& k) { change (k.pads[static_cast<size_t> (selected)]); }); }
    void updateInspector();
    void preview (int pad, float velocity);
    void setLayerSample (int pad, int layer, const std::shared_ptr<const SampleData>& sample);
    void chooseLayerSample (int layer);
    void loadKitFromSpec (const KitSpec& spec);
    void showKitMenu();
    void saveKit();
    void say (const juce::String& text) const { if (onStatus) onStatus (text); }

    // geometry
    juce::Rectangle<int> padRect (int pad) const;
    int padAt (juce::Point<int> p) const;
    juce::Rectangle<int> layerRowRect (int layer) const;
    int layerRowAt (juce::Point<int> p) const;
    juce::Rectangle<int> stepCell (int pad, int step) const;
    bool stepAt (juce::Point<int> p, int& pad, int& step) const;
    int stepCount() const noexcept { return std::max (1, loopTicks / kSixteenthTicks); }
    bool stepIsOn (const Channel& c, int pad, int step) const;
    void setStep (int pad, int step, bool on, float velocity);

    AudioEngine& engine;
    int channelIndex = 0;
    int loopTicks = 8 * kTicksPerBeat;
    int playheadTick = -1;
    int selected = 0;
    juce::File kitsFolder;
    std::unique_ptr<juce::FileChooser> chooser;

    // step painting
    bool painting = false;
    bool paintValue = false;
    float paintVelocity = 0.8f;
    int lastPaintPad = -1, lastPaintStep = -1;
    bool changedBySteps = false;

    juce::Rectangle<int> padsArea, inspectorArea, stepsArea;
    int stepLabelWidth = 96;

    juce::Label kitLabel;
    juce::TextButton loadKitButton { "Load kit..." }, saveKitButton { "Save kit..." }, clearKitButton { "Empty kit" };

    juce::TextEditor nameEditor;
    juce::TextButton muteButton { "Mute" }, soloButton { "Solo" }, previewButton { "Play pad" };
    juce::ComboBox chokeBox;
    std::array<juce::Label, kPadLayers> layerNames;
    std::array<juce::TextButton, kPadLayers> layerLoad, layerClear;
    std::array<juce::Slider, kPadLayers> layerVelocity;
    juce::Slider volumeSlider, panSlider, pitchSlider, swingSlider;
    juce::Label volumeLabel { {}, "Level" }, panLabel { {}, "Pan" }, pitchLabel { {}, "Tuning" }, swingLabel { {}, "Swing" }, chokeLabel { {}, "Choke group" };
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumMachineView)
};

class DrumMachineWindow : public juce::DocumentWindow
{
public:
    explicit DrumMachineWindow (AudioEngine& engine);
    ~DrumMachineWindow() override { clearContentComponent(); }

    DrumMachineView& view() noexcept { return content; }
    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    DrumMachineView content;
};
} // namespace sls
