#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Effects.h"
#include "ui/DragItems.h"

namespace sls
{
// The Plugin picker (0.60.0), as FL Studio's (F8 there; a panel of the workspace here, opened from the toolbar's shortcut panel,
// the VIEW menu or the ADD menu): every instrument and effect the program can use, side by side, with a search box above them.
//   Left list  : the instruments - the built-in ones (Synth, Sampler, Drum machine, Slicer, SunBurst and HexForge when they are
//                built) and the plugin instruments found by the last scan.
//   Right list : the effects - the built-in ones and the plugin effects.
// Double-click (or Enter, or the button under a list): an instrument becomes a new channel, an effect goes into the first free
// slot of the selected channel's mixer track. Instruments can also be dragged onto the Channel Rack or the Playlist, like the
// browser's. The program fills the lists (setInstruments / setEffects) and acts on the choices (the callbacks).
class PluginPicker : public juce::Component
{
public:
    struct Entry
    {
        juce::String name;   // what the list shows
        juce::String detail; // the maker and format, or "built-in"
        // What to do with it: a drag::Item for an instrument (the kind and payload the rack understands), an effect type or a
        // plugin identifier for an effect.
        drag::Item item;
        EffectType effect = EffectType::None;
        juce::String pluginId; // effects: the plugin's identifier (empty for a built-in effect)
    };

    PluginPicker();
    ~PluginPicker() override;

    void setInstruments (std::vector<Entry> list);
    void setEffects (std::vector<Entry> list);

    std::function<void (const Entry&)> onInstrumentChosen;
    std::function<void (const Entry&)> onEffectChosen;
    std::function<void()> onRescan; // "Scan again": look for plugins

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Column; // a titled list with its button

    void applyFilter();

    juce::TextEditor search;
    juce::Label hint;
    juce::TextButton rescanButton { "Scan plugins again" };
    std::unique_ptr<Column> instruments, effects;
    std::vector<Entry> allInstruments, allEffects;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginPicker)
};
} // namespace sls
