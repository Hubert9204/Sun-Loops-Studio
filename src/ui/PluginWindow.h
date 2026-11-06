#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugins/PluginHost.h"

namespace sls
{
// The window of one plugin: its own editor, or JUCE's generic editor (a slider per parameter) when it has none. The window keeps the plugin
// alive while it is open; the owner closes it when the plugin leaves the project.
class PluginWindow : public juce::DocumentWindow
{
public:
    explicit PluginWindow (std::shared_ptr<JucePlugin> pluginToShow)
        : juce::DocumentWindow (juce::String (pluginToShow->displayName()), juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton),
          plugin (std::move (pluginToShow))
    {
        setUsingNativeTitleBar (false);
        auto* instance = plugin->getInstance();
        juce::AudioProcessorEditor* editor = nullptr;
        if (instance != nullptr)
        {
            editor = instance->hasEditor() ? instance->createEditorIfNeeded() : nullptr;
            if (editor == nullptr)
                editor = new juce::GenericAudioProcessorEditor (*instance);
        }
        if (editor != nullptr)
        {
            setContentOwned (editor, true);
            setResizable (editor->isResizable(), false);
        }
        else
        {
            auto* note = new juce::Label ({}, "This plugin could not be loaded (it is not installed here). Its settings are kept in the project.");
            note->setSize (460, 80);
            note->setJustificationType (juce::Justification::centred);
            setContentOwned (note, true);
        }
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    ~PluginWindow() override { clearContentComponent(); } // the editor goes before the plugin it shows

    const HostedPlugin* shows() const noexcept { return plugin.get(); }
    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    std::shared_ptr<JucePlugin> plugin;
};
} // namespace sls
