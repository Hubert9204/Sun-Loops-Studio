#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/HostedPlugin.h"

namespace sls
{
// Choosing which parameter of a plugin to automate (0.35.0): a list of the plugin's parameters with their current values, a box
// that filters it by name, and the parameter last moved in the plugin's own window chosen at first (as FL Studio's "last tweaked").
class PluginParameterPicker : public juce::Component, private juce::ListBoxModel
{
public:
    PluginParameterPicker (std::shared_ptr<HostedPlugin> plugin, std::function<void (int)> chosen);

    void resized() override;
    void paint (juce::Graphics& g) override;

    // Opens the picker in its own dialog window. `chosen` gets the parameter's index (never called when cancelled).
    static void show (const std::shared_ptr<HostedPlugin>& plugin, const juce::String& title, std::function<void (int)> chosen);

private:
    int getNumRows() override { return static_cast<int> (shown.size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int) override { choose(); }

    void filter();
    void choose();

    std::shared_ptr<HostedPlugin> plugin;
    std::function<void (int)> chosen;
    std::vector<int> shown; // parameter indexes that pass the filter
    juce::Label hint;
    juce::TextEditor search;
    juce::ListBox list;
    juce::TextButton automateButton { "Automate" }, cancelButton { "Cancel" };
};
} // namespace sls
