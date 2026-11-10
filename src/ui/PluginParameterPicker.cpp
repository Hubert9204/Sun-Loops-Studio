#include "ui/PluginParameterPicker.h"

namespace sls
{
PluginParameterPicker::PluginParameterPicker (std::shared_ptr<HostedPlugin> p, std::function<void (int)> c)
    : plugin (std::move (p)), chosen (std::move (c))
{
    const int last = plugin != nullptr ? plugin->lastTouchedParameter() : -1;
    hint.setText (last >= 0 ? "Last moved in the plugin's window: " + juce::String (plugin->parameterName (last)) + " (chosen below)"
                            : "Tip: move a control in the plugin's window first, and it is chosen here.",
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, juce::Colour (0xffb8bec9));
    addAndMakeVisible (hint);

    search.setTextToShowWhenEmpty ("Filter by name...", juce::Colours::grey);
    search.onTextChange = [this] { filter(); };
    search.onReturnKey = [this] { choose(); };
    addAndMakeVisible (search);

    list.setModel (this);
    list.setRowHeight (22);
    addAndMakeVisible (list);

    automateButton.onClick = [this] { choose(); };
    cancelButton.onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };
    addAndMakeVisible (automateButton);
    addAndMakeVisible (cancelButton);
    setSize (460, 480); // before choosing the row, so that the list has a size to scroll in

    filter();
    if (last >= 0)
        if (const auto it = std::find (shown.begin(), shown.end(), last); it != shown.end())
        {
            const int row = static_cast<int> (it - shown.begin());
            list.selectRow (row);
            list.scrollToEnsureRowIsOnscreen (row);
        }
}

void PluginParameterPicker::filter()
{
    shown.clear();
    const auto words = juce::StringArray::fromTokens (search.getText().toLowerCase(), " ", "");
    const int count = plugin != nullptr ? plugin->parameterCount() : 0;
    for (int i = 0; i < count; ++i)
    {
        const auto name = juce::String (plugin->parameterName (i)).toLowerCase();
        bool ok = true;
        for (const auto& w : words)
            ok = ok && (w.isEmpty() || name.contains (w));
        if (ok)
            shown.push_back (i);
    }
    list.updateContent();
    list.repaint();
    if (! shown.empty() && list.getSelectedRow() < 0)
        list.selectRow (0);
}

void PluginParameterPicker::choose()
{
    const int row = list.getSelectedRow();
    if (row < 0 || row >= static_cast<int> (shown.size()))
        return;
    const int index = shown[static_cast<size_t> (row)];
    auto callback = chosen;
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState (1);
    if (callback)
        callback (index);
}

void PluginParameterPicker::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff23272e));
}

void PluginParameterPicker::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= static_cast<int> (shown.size()) || plugin == nullptr)
        return;
    const int index = shown[static_cast<size_t> (row)];
    if (selected)
        g.fillAll (juce::Colour (0xffe8943a).withAlpha (0.45f));
    g.setColour (juce::Colours::white);
    g.setFont (13.0f);
    g.drawText (juce::String (index + 1) + "  " + juce::String (plugin->parameterName (index)), 6, 0, width - 140, height,
                juce::Justification::centredLeft, true);
    g.setColour (juce::Colour (0xffb8bec9));
    g.drawText (juce::String (plugin->parameterText (index, plugin->parameterValue (index))), width - 134, 0, 128, height,
                juce::Justification::centredRight, true);
}

void PluginParameterPicker::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    list.selectRow (row);
    choose();
}

void PluginParameterPicker::resized()
{
    auto area = getLocalBounds().reduced (10);
    hint.setBounds (area.removeFromTop (22));
    area.removeFromTop (4);
    search.setBounds (area.removeFromTop (26));
    area.removeFromTop (6);
    auto buttons = area.removeFromBottom (30);
    cancelButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    automateButton.setBounds (buttons.removeFromRight (100));
    area.removeFromBottom (8);
    list.setBounds (area);
}

void PluginParameterPicker::show (const std::shared_ptr<HostedPlugin>& plugin, const juce::String& title, std::function<void (int)> chosen)
{
    juce::DialogWindow::LaunchOptions options;
    auto* picker = new PluginParameterPicker (plugin, std::move (chosen));
    options.content.setOwned (picker);
    options.dialogTitle = title;
    options.dialogBackgroundColour = juce::Colour (0xff23272e);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    if (auto* dw = options.launchAsync())
    {
        dw->setResizeLimits (360, 300, 1200, 1400);
        picker->search.grabKeyboardFocus();
    }
}
} // namespace sls
