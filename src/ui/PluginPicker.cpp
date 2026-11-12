#include "ui/PluginPicker.h"

namespace sls
{
namespace
{
const juce::Colour kBackground { 0xff22252b };
const juce::Colour kColumn { 0xff2a2e36 };
const juce::Colour kText { 0xffd5d9e0 };
const juce::Colour kDim { 0xff8a919d };
const juce::Colour kAccent { 0xffe8943a };
const juce::Colour kInstrument { 0xff4cc38a };
const juce::Colour kEffect { 0xff5fb0ff };
} // namespace

// One column of the picker: a title, a list of entries and a button that adds the selected one.
class PluginPicker::Column : public juce::Component, private juce::ListBoxModel
{
public:
    Column (const juce::String& titleText, const juce::String& buttonText, juce::Colour colour, bool draggable)
        : title (titleText), accent (colour), canDrag (draggable)
    {
        list.setModel (this);
        list.setRowHeight (26);
        list.setColour (juce::ListBox::backgroundColourId, kColumn);
        list.setWantsKeyboardFocus (true);
        addAndMakeVisible (list);
        addButton.setButtonText (buttonText);
        addButton.setMouseClickGrabsKeyboardFocus (false);
        addButton.onClick = [this] { choose (list.getSelectedRow()); };
        addAndMakeVisible (addButton);
    }

    void setEntries (std::vector<Entry> newEntries)
    {
        entries = std::move (newEntries);
        list.updateContent();
        list.repaint();
        addButton.setEnabled (! entries.empty());
    }

    std::function<void (const Entry&)> onChosen;

    void paint (juce::Graphics& g) override
    {
        g.setColour (kColumn);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.setColour (accent);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (title + "  (" + juce::String (entries.size()) + ")", getLocalBounds().removeFromTop (26).reduced (10, 0),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4);
        area.removeFromTop (24);
        addButton.setBounds (area.removeFromBottom (26).reduced (40, 1));
        area.removeFromBottom (4);
        list.setBounds (area);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey)
        {
            choose (list.getSelectedRow());
            return true;
        }
        return false;
    }

private:
    int getNumRows() override { return static_cast<int> (entries.size()); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= static_cast<int> (entries.size()))
            return;
        const auto& e = entries[static_cast<size_t> (row)];
        if (selected)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillRect (0, 0, width, height);
        }
        g.setColour (accent);
        g.fillEllipse (8.0f, height * 0.5f - 3.0f, 6.0f, 6.0f);
        g.setColour (kText);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (e.name, 22, 0, width - 26, height, juce::Justification::centredLeft, true);
        if (e.detail.isNotEmpty())
        {
            g.setColour (kDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (e.detail, 22, 0, width - 30, height, juce::Justification::centredRight, true);
        }
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { choose (row); }

    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        if (! canDrag || rows.isEmpty())
            return {};
        const int row = rows[0];
        if (row < 0 || row >= static_cast<int> (entries.size()))
            return {};
        return entries[static_cast<size_t> (row)].item.toString();
    }

    void choose (int row)
    {
        if (row >= 0 && row < static_cast<int> (entries.size()) && onChosen)
            onChosen (entries[static_cast<size_t> (row)]);
    }

    juce::String title;
    juce::Colour accent;
    bool canDrag;
    std::vector<Entry> entries;
    juce::ListBox list;
    juce::TextButton addButton;
};

PluginPicker::PluginPicker()
{
    search.setTextToShowWhenEmpty ("Search instruments and effects...", kDim);
    search.setFont (juce::FontOptions (13.0f));
    search.onTextChange = [this] { applyFilter(); };
    search.onEscapeKey = [this] { search.clear(); applyFilter(); };
    addAndMakeVisible (search);

    hint.setFont (juce::FontOptions (11.0f));
    hint.setColour (juce::Label::textColourId, kDim);
    hint.setText ("Double-click an instrument for a new channel (or drag it onto the Channel Rack); double-click an effect to put it "
                  "on the selected channel's mixer track.",
                  juce::dontSendNotification);
    hint.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (hint);

    rescanButton.setTooltip ("Look for VST3, LV2 and LADSPA plugins again");
    rescanButton.setMouseClickGrabsKeyboardFocus (false);
    rescanButton.onClick = [this] { if (onRescan) onRescan(); };
    addAndMakeVisible (rescanButton);

    instruments = std::make_unique<Column> ("Instruments", "Add channel", kInstrument, true);
    effects = std::make_unique<Column> ("Effects", "Add to the selected track", kEffect, false);
    instruments->onChosen = [this] (const Entry& e) { if (onInstrumentChosen) onInstrumentChosen (e); };
    effects->onChosen = [this] (const Entry& e) { if (onEffectChosen) onEffectChosen (e); };
    addAndMakeVisible (*instruments);
    addAndMakeVisible (*effects);
}

PluginPicker::~PluginPicker() = default;

void PluginPicker::setInstruments (std::vector<Entry> list)
{
    allInstruments = std::move (list);
    applyFilter();
}

void PluginPicker::setEffects (std::vector<Entry> list)
{
    allEffects = std::move (list);
    applyFilter();
}

void PluginPicker::applyFilter()
{
    const auto needle = search.getText().trim();
    const auto matches = [&needle] (const Entry& e)
    {
        return needle.isEmpty() || e.name.containsIgnoreCase (needle) || e.detail.containsIgnoreCase (needle);
    };
    std::vector<Entry> shown;
    for (const auto& e : allInstruments)
        if (matches (e))
            shown.push_back (e);
    instruments->setEntries (std::move (shown));
    shown.clear();
    for (const auto& e : allEffects)
        if (matches (e))
            shown.push_back (e);
    effects->setEntries (std::move (shown));
}

void PluginPicker::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
}

void PluginPicker::resized()
{
    auto area = getLocalBounds().reduced (6);
    auto top = area.removeFromTop (28);
    rescanButton.setBounds (top.removeFromRight (150));
    top.removeFromRight (6);
    search.setBounds (top);
    area.removeFromTop (4);
    hint.setBounds (area.removeFromTop (18));
    area.removeFromTop (4);
    const int half = (area.getWidth() - 6) / 2;
    instruments->setBounds (area.removeFromLeft (half));
    area.removeFromLeft (6);
    effects->setBounds (area);
}
} // namespace sls
