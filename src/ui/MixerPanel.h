#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Project.h"
#include "ui/VuMeter.h"

namespace sls
{
// One effect slot as a button: shows the effect's name, dimmed when empty or bypassed.
class SlotButton : public juce::Component
{
public:
    void set (const EffectSlot& slot, bool isSelected, bool showFullName);

    std::function<void()> onClick; // left click: select / open the menu
    std::function<void()> onMenu;  // right click

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    EffectSlot slotCopy;
    bool selected = false;
    bool fullName = false;
};

// One vertical mixer track: name and the channels routed to it, four effect slots, balance, mute/solo,
// a fader with a Peak/RMS meter and a dB read-out.
class MixerStrip : public juce::Component
{
public:
    MixerStrip (MixerTrack& trackToControl, int trackIndex);

    void refreshFromTrack();
    void setSelected (bool shouldBeSelected);
    void setSelectedSlot (int slot) { selectedSlot = slot; refreshSlots(); }
    void setRoutedChannels (const juce::String& text);
    void setLevels (float peak, float rms) { meter.setLevels (peak, rms); }
    int getIndex() const noexcept { return index; }

    std::function<void (int)> onSelect;                // the strip was clicked
    std::function<void (int)> onChanged;               // fader, balance, mute or solo changed
    std::function<void (int, int)> onSlotClicked;      // (track, slot): select the slot in the effects panel
    std::function<void (int, int)> onSlotMenu;        // (track, slot): right click

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static juce::String volumeToText (float volume);
    static juce::String panToText (float pan);

    static constexpr int kWidth = 80;

private:
    void refreshSlots();

    MixerTrack& track;
    int index;
    bool selected = false;
    int selectedSlot = -1;

    juce::Label nameLabel;
    juce::Label routedLabel;
    std::array<SlotButton, kSlotsPerTrack> slotButtons;
    juce::Slider panSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label panLabel;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label dbLabel;
    VuMeter meter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerStrip)
};

// Index that stands for the master bus where a mixer track index is expected (the tracks are 0 .. kMaxTracks - 1).
constexpr int kMasterIndex = kMaxTracks;

// The master strip: the master fader (linked to the toolbar's master volume) and four insert-effect slots that
// process the sum of all tracks, before the master fader and the soft limiter.
class MasterStrip : public juce::Component
{
public:
    MasterStrip();

    // The master bus's effect slots live in `masterTrack.slots`; the object is not owned.
    void setMasterTrack (MixerTrack& masterTrack);
    void refreshSlots();
    void setSelected (bool shouldBeSelected);
    void setSelectedSlot (int slot) { selectedSlot = slot; refreshSlots(); }
    void setVolume (double volume);
    void setLevels (float peak, float rms) { meter.setLevels (peak, rms); }

    std::function<void (double)> onVolumeChanged;
    std::function<void()> onSelect;               // the strip was clicked
    std::function<void (int)> onSlotClicked;      // slot index
    std::function<void (int)> onSlotMenu;         // right click on a slot

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    MixerTrack* master = nullptr;
    bool selected = false;
    int selectedSlot = -1;
    std::array<SlotButton, kSlotsPerTrack> slotButtons;
    juce::Label nameLabel;
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label dbLabel;
    VuMeter meter;
};

// The effects of the selected track: four slots (pick an effect, switch it on/off) and the parameters of the
// selected slot. These are Sun Loops Studio's built-in effects; hosting VST3 plugins is not implemented yet.
class FxPanel : public juce::Component
{
public:
    FxPanel();

    // `trackIndex` == kMasterIndex shows the master bus (its title and help text change).
    void setTrack (MixerTrack* track, int trackIndex);
    void setSelectedSlot (int slot);
    int getSelectedSlot() const noexcept { return selectedSlot; }
    void refresh();

    std::function<void (int)> onChanged;        // an effect or parameter changed (track index)
    std::function<void (int)> onSlotsChanged;   // slots were added/removed/bypassed (track index): strips redraw
    std::function<void (int)> onSelectedSlotChanged; // the user picked another slot in this panel

    void showSlotMenu (int slot);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void applySlot (int slot, const EffectSlot& newSlot);
    void rebuildParameters();

    MixerTrack* track = nullptr;
    int trackIndex = 0;
    int selectedSlot = 0;

    juce::Label titleLabel;
    std::array<SlotButton, kSlotsPerTrack> slotButtons;
    std::array<juce::TextButton, kSlotsPerTrack> powerButtons;
    juce::Label effectLabel;
    juce::Label notePlaceholder;
    struct ParamRow
    {
        juce::Label name;
        juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    };
    std::array<ParamRow, kMaxEffectParams> params;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxPanel)
};

// The Mixer panel body (F9): all mixer tracks side by side, the master strip and the effects panel.
class MixerPanel : public juce::Component
{
public:
    MixerPanel();

    // The tracks are not owned and must not be reallocated while the panel exists.
    void setTracks (std::vector<MixerTrack>& tracks);
    // The master bus's effect chain (its `slots`); not owned. Call once, after setTracks().
    void setMaster (MixerTrack& masterTrack);

    // `index` is a track (0 .. kMaxTracks - 1) or kMasterIndex for the master bus, here and in the callbacks.
    void refreshTrack (int index);
    void setSelectedTrack (int index);
    void setTrackChannels (int index, const juce::String& channelNames);
    void setTrackLevels (int index, float peak, float rms);
    void setMasterLevels (float peak, float rms) { masterStrip.setLevels (peak, rms); }
    void setMasterVolume (double volume) { masterStrip.setVolume (volume); }

    std::function<void (int)> onTrackChanged;       // anything that changes the sound (track index, or kMasterIndex)
    std::function<void (double)> onMasterVolumeChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kFxPanelWidth = 240;

private:
    struct Inner : juce::Component {};

    void layoutInner();
    void selectMaster();

    int selectedTrack = 0;
    std::vector<MixerTrack>* trackData = nullptr;
    MixerTrack* masterData = nullptr;
    juce::Viewport viewport;
    Inner inner;
    std::vector<std::unique_ptr<MixerStrip>> strips;
    MasterStrip masterStrip;
    FxPanel fxPanel;
};
} // namespace sls
