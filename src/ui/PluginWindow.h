#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugins/PluginHost.h"

namespace sls
{
// The window of one plugin: its own editor, or JUCE's generic editor (a slider per parameter) when it has none. The window keeps the plugin
// alive while it is open; the owner closes it when the plugin leaves the project.
//
// 0.85.0, as FL Studio's plugin wrapper: a bar above the editor with
//   - Presets: the plugin's own programs and the user's presets (files in a folder per plugin), the arrows step through them, Save...;
//   - Undo / Redo of what was changed in the plugin (its whole state is kept after each change, up to kMaxUndo steps; Ctrl+Z / Ctrl+Y);
//   - Routing (when the plugin has a sidechain, more outputs or MIDI out: the owner's menu);
//   - Process: whether the plugin runs in this program or in a process of its own (a sandbox; the owner swaps it), and its state there.
class PluginFrame;

class PluginWindow : public juce::DocumentWindow, private juce::Timer
{
public:
    explicit PluginWindow (std::shared_ptr<JucePlugin> pluginToShow, const juce::File& presetsRoot);
    ~PluginWindow() override;

    const HostedPlugin* shows() const noexcept { return plugin.get(); }
    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }

    // The owner's parts (all optional): the routing menu shown at `anchor`; the switch between this process and a sandbox; a state the window
    // put back (undo, redo, a preset) to be counted as an edit of the project.
    std::function<void (juce::Component& anchor)> onRoutingMenu;
    std::function<void()> onToggleSandbox;
    std::function<void()> onRestart;
    std::function<void()> onStateRestored;

    bool keyPressed (const juce::KeyPress&) override;

    void undo();
    void redo();
    bool canUndo() const noexcept { return ! undoStates.empty(); }
    bool canRedo() const noexcept { return ! redoStates.empty(); }
    static constexpr int kMaxUndo = 50;

    // The folder of a plugin's presets, and the file format (".slsplugin": the plugin's name, format and state).
    static juce::File presetsFolder (const juce::File& root, const juce::PluginDescription& d);
    static juce::Result savePreset (const JucePlugin& p, const juce::File& file);
    static juce::Result loadPreset (JucePlugin& p, const juce::File& file);

    void refreshBar(); // after a change from outside (the plugin was swapped, crashed, came back)

private:
    friend class PluginFrame;
    void timerCallback() override;
    void restore (const juce::MemoryBlock& state);
    void showPresetMenu (juce::Component& anchor);
    void stepPreset (int delta);

    std::shared_ptr<JucePlugin> plugin;
    juce::File presetsRoot;
    PluginFrame* frame = nullptr;

    // undo: the state now, and those before / after it
    juce::MemoryBlock currentState;
    std::vector<juce::MemoryBlock> undoStates, redoStates;
    std::uint32_t seenChanges = 0;
    int quietTicks = -1;    // ticks since the last change seen (-1: nothing pending)
    bool settling = false;  // a state was just put back: what it reports is not a new edit
    juce::File lastPreset;  // the user preset loaded or saved last (the arrows step from it)
};
} // namespace sls
