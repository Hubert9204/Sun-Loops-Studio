#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Automation.h"
#include "audio/Project.h"
#include "ui/AutomatableSlider.h"
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
    // `isAuxBus`: the strip of an aux bus (its index is kAuxIndexBase + n): no sends, no VCA, no solo.
    MixerStrip (MixerTrack& trackToControl, int trackIndex, bool isAuxBus = false);

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
    std::function<void (const AutomationTarget&)> onAutomate; // right click on the fader / balance

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static juce::String volumeToText (float volume);
    static juce::String panToText (float pan);

    static constexpr int kWidth = 72;
    juce::String routedChannels() const { return routed; } // the channels playing through this track ("" when none)

private:
    void refreshSlots();

    MixerTrack& track;
    int index;
    bool selected = false;
    bool isAux = false;
    int selectedSlot = -1;

    juce::Label nameLabel;
    juce::Label routedLabel;
    juce::String routed;
    std::array<SlotButton, kSlotsPerTrack> slotButtons;
    std::array<juce::Slider, kAuxBuses> sendSliders;
    std::array<juce::Label, kAuxBuses> sendLabels;
    juce::TextButton preButton { "Pre" };
    std::array<juce::TextButton, kVcaGroups> vcaButtons;
    AutomatableSlider panSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label panLabel;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    AutomatableSlider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label dbLabel;
    VuMeter meter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerStrip)
};

// Index that stands for the master bus where a mixer track index is expected (the tracks are 0 .. kMaxTracks - 1).
constexpr int kMasterIndex = kMaxTracks;
// ... and the aux buses: kAuxIndexBase + n.
constexpr int kAuxIndexBase = kMasterIndex + 1;

// One VCA group: a fader that scales the faders of its member tracks (the tracks choose their groups on their own strips).
class VcaStrip : public juce::Component
{
public:
    VcaStrip (Console& consoleToControl, int groupIndex);

    void refresh();
    void setMemberCount (int count);

    std::function<void()> onChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Console& console;
    int group;
    juce::Label nameLabel;
    juce::Label membersLabel;
    juce::TextButton muteButton { "M" };
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label dbLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VcaStrip)
};

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
// selected slot: the built-in effects, or a third-party plugin effect (VST3, LV2, LADSPA; 0.17.0).
class FxPanel : public juce::Component
{
public:
    FxPanel();

    // `trackIndex` == kMasterIndex shows the master bus (its title and help text change).
    void setTrack (MixerTrack* track, int trackIndex);
    void setSelectedSlot (int slot);
    // Time Shapers left without a recording in the whole project (0.41.0): told when one is selected.
    void setTimeShaperShortfall (int count);
    int getSelectedSlot() const noexcept { return selectedSlot; }
    void refresh();

    std::function<void (int)> onChanged;        // an effect or parameter changed (track index)
    std::function<void (int)> onSlotsChanged;   // slots were added/removed/bypassed (track index): strips redraw
    std::function<void (int)> onSelectedSlotChanged; // the user picked another slot in this panel
    std::function<void (const AutomationTarget&)> onAutomate; // right click on a parameter (index = track or kMasterIndex)

    // Plugin effects (0.17.0): what the slot menu lists, how one is made, and how its window is opened.
    struct PluginChoice
    {
        juce::String name, identifier;
    };
    std::function<std::vector<PluginChoice>()> listEffectPlugins;
    std::function<std::shared_ptr<HostedPlugin> (const juce::String& identifier)> createEffectPlugin;
    std::function<void (const std::shared_ptr<HostedPlugin>&)> openPluginWindow;
    // The name of mixer track `index` (0-based), for the compressor's sidechain choice (0.45.0).
    std::function<juce::String (int)> trackNameOf;
    std::function<int()> trackCount; // how many mixer tracks there are, for that choice
    // Effect-chain presets (0.45.0): the Chain button's menu (save the slots, load a chain, clear them); MainComponent does the files.
    std::function<void (juce::Component& button, MixerTrack& track, int trackIndex)> onChainMenu;

    void showSlotMenu (int slot);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void applySlot (int slot, const EffectSlot& newSlot);
    void rebuildParameters();
    void showPresetMenu();

    MixerTrack* track = nullptr;
    int trackIndex = 0;
    int selectedSlot = 0;

    juce::Label titleLabel;
    std::array<SlotButton, kSlotsPerTrack> slotButtons;
    std::array<juce::TextButton, kSlotsPerTrack> powerButtons;
    juce::Label effectLabel;
    juce::Label notePlaceholder;
    juce::TextButton pluginButton { "Open plugin window" };
    juce::TextButton automatePluginButton { "Automate a parameter..." }; // 0.35.0
    juce::Label timeShaperNotice;
    int timeShaperShortfall = 0;
    juce::TextButton presetButton { "Presets..." }; // the built-in effect's ready-made settings (0.21.0)
    juce::TextButton chainButton { "Chain..." };    // the whole chain of slots saved and loaded (0.45.0)
    struct ParamRow
    {
        juce::Label name;
        AutomatableSlider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::ComboBox choice; // a mixer track (0.45.0: the compressor's sidechain), shown instead of the slider
    };
    std::array<ParamRow, kMaxEffectParams> params;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxPanel)
};

// The Mixer panel body (F9): all mixer tracks side by side, the master strip and the effects panel.
class MixerPanel : public juce::Component
{
public:
    MixerPanel();

    // The tracks are not owned and must not be reallocated while the panel exists. Called again with the same vector after it
    // grew or shrank (0.29.0), only the strips at the end are added or removed. `stripCount` below the vector's size removes
    // the strips of the last tracks first (before those tracks are destroyed).
    void setTracks (std::vector<MixerTrack>& tracks, size_t stripCount = static_cast<size_t> (-1));
    // The master bus's effect chain (its `slots`); not owned. Call once, after setTracks().
    void setMaster (MixerTrack& masterTrack);
    // The aux buses and VCA groups; not owned. Call once, after setTracks().
    void setConsole (Console& consoleToControl);
    void refreshConsole(); // after the console was replaced (a project was opened)
    void setAuxLevels (int aux, float peak, float rms);

    // `index` is a track (0 .. kMaxTracks - 1) or kMasterIndex for the master bus, here and in the callbacks.
    void refreshTrack (int index);
    void setSelectedTrack (int index);
    void setTrackChannels (int index, const juce::String& channelNames);
    void setTrackLevels (int index, float peak, float rms);
    void setMasterLevels (float peak, float rms) { masterStrip.setLevels (peak, rms); }
    void setMasterVolume (double volume) { masterStrip.setVolume (volume); }
    void setTimeShaperShortfall (int count) { fxPanel.setTimeShaperShortfall (count); }

    std::function<void (int)> onTrackChanged;       // anything that changes the sound (track index, or kMasterIndex)
    std::function<void (int)> onAddTracks;          // the "+" button: add this many tracks (0.29.0)
    std::function<void()> onRemoveLastTrack;        // its menu: remove the last track
    std::function<void (double)> onMasterVolumeChanged;
    // Right click on a track fader / balance or on an effect parameter. For the master bus: kind MasterEffect.
    std::function<void (const AutomationTarget&)> onAutomate;
    std::function<std::vector<FxPanel::PluginChoice>()> listEffectPlugins;
    std::function<std::shared_ptr<HostedPlugin> (const juce::String& identifier)> createEffectPlugin;
    std::function<void (const std::shared_ptr<HostedPlugin>&)> openPluginWindow;
    std::function<void (juce::Component& button, MixerTrack& track, int trackIndex)> onChainMenu; // 0.45.0, see FxPanel
    void refreshEffects() { fxPanel.refresh(); }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kFxPanelWidth = 240;

private:
    struct Inner : juce::Component {};

    void layoutInner();
    void addStrip (size_t index);
    void showAddTrackMenu();
    void selectMaster();
    void selectAux (int aux);
    void updateVcaMembers();
    MixerTrack* trackFor (int index) noexcept; // a track, an aux bus or the master (nullptr if there is none)

    int selectedTrack = 0;
    std::vector<MixerTrack>* trackData = nullptr;
    MixerTrack* masterData = nullptr;
    Console* consoleData = nullptr;
    std::vector<std::unique_ptr<MixerStrip>> auxStrips;
    std::vector<std::unique_ptr<VcaStrip>> vcaStrips;
    juce::Viewport viewport;
    Inner inner;
    std::vector<std::unique_ptr<MixerStrip>> strips;
    // After the last track (0.29.0): a click adds a track, a right-click offers more.
    struct AddTrackButton : juce::TextButton
    {
        AddTrackButton() : juce::TextButton ("+") {}
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && onRightClick)
                onRightClick();
            else
                juce::TextButton::mouseDown (e);
        }
    };
    AddTrackButton addTrackButton;
    static constexpr int kAddButtonWidth = 28;
    MasterStrip masterStrip;
    FxPanel fxPanel;
};
} // namespace sls
