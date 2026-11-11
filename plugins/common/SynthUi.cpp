#include "common/SynthUi.h"

#include "common/HardwareUi.h"

namespace hwui
{
PresetBar::PresetBar (synthkit::SynthProcessor& p, juce::StringArray factoryCategories, juce::LookAndFeel& lf)
    : processor (p), categories (std::move (factoryCategories)), look (lf)
{
    for (auto* b : { &prevButton, &presetButton, &nextButton, &saveButton })
        addAndMakeVisible (b);
    prevButton.onClick = [this] { step (-1); };
    nextButton.onClick = [this] { step (1); };
    presetButton.onClick = [this] { showMenu(); };
    saveButton.onClick = [this] { saveDialog(); };
    poll();
}

PresetBar::~PresetBar()
{
    if (saveWindow != nullptr)
        saveWindow->setLookAndFeel (nullptr);
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    prevButton.setBounds (r.removeFromLeft (30));
    saveButton.setBounds (r.removeFromRight (60));
    r.removeFromRight (6);
    nextButton.setBounds (r.removeFromRight (30));
    r.removeFromRight (4);
    r.removeFromLeft (4);
    presetButton.setBounds (r);
}

bool PresetBar::poll()
{
    if (processor.presetGeneration() == seenGeneration)
        return false;
    seenGeneration = processor.presetGeneration();
    presetButton.setButtonText (processor.getPresetCategory().toUpperCase() + "   " + processor.getPresetName());
    return true;
}

void PresetBar::showMenu()
{
    const auto list = processor.presets();
    juce::PopupMenu menu;
    auto shown = categories;
    for (const auto& p : list)
        shown.addIfNotAlreadyThere (p.category);
    for (const auto& category : shown)
    {
        juce::PopupMenu sub;
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].category == category)
                sub.addItem (static_cast<int> (i) + 1, list[i].name + (list[i].file != juce::File() ? "  (user)" : ""), true,
                             list[i].name == processor.getPresetName() && list[i].category == processor.getPresetCategory());
        if (sub.getNumItems() > 0)
            menu.addSubMenu (category, sub);
    }
    menu.addSeparator();
    menu.addItem (-1, "Save preset...");
    menu.addItem (-2, "Init patch");
    menu.addItem (-3, "Open the presets folder");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [safe = juce::Component::SafePointer<PresetBar> (this), list] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result > 0 && result <= static_cast<int> (list.size()))
                                safe->processor.loadPreset (list[static_cast<size_t> (result - 1)]);
                            else if (result == -1)
                                safe->saveDialog();
                            else if (result == -2)
                                safe->processor.initPatch();
                            else if (result == -3)
                            {
                                const auto folder = safe->processor.userPresetFolder();
                                folder.createDirectory();
                                folder.startAsProcess();
                            }
                        });
}

void PresetBar::step (int delta)
{
    const auto list = processor.presets();
    if (list.empty())
        return;
    int current = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == processor.getPresetName() && list[i].category == processor.getPresetCategory())
            current = static_cast<int> (i);
    const int count = static_cast<int> (list.size());
    const int next = ((current < 0 ? (delta > 0 ? -1 : 0) : current) + delta + count) % count;
    processor.loadPreset (list[static_cast<size_t> (next)]);
}

void PresetBar::saveDialog()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "A name and a category for this sound.", juce::MessageBoxIconType::NoIcon, this);
    saveWindow->setLookAndFeel (&look);
    saveWindow->addTextEditor ("name", processor.getPresetName() == "Init" ? juce::String ("My sound") : processor.getPresetName(), "Name");
    auto choices = categories;
    choices.add ("User");
    saveWindow->addComboBox ("category", choices, "Category");
    if (auto* box = saveWindow->getComboBoxComponent ("category"))
        box->setSelectedItemIndex (std::max (0, choices.indexOf (processor.getPresetCategory())), juce::dontSendNotification);
    saveWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<PresetBar> (this)] (int result)
    {
        if (safe == nullptr || safe->saveWindow == nullptr)
            return;
        if (result == 1)
        {
            const auto name = safe->saveWindow->getTextEditorContents ("name");
            const auto category = safe->saveWindow->getComboBoxComponent ("category")->getText();
            if (safe->processor.savePreset (name, category) == juce::File())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save preset", "The preset could not be saved.");
        }
        safe->saveWindow->setLookAndFeel (nullptr);
        safe->saveWindow.reset();
    }), false);
}

void Meter::setLevel (float peak)
{
    level = std::max (peak, level * 0.85f);
    repaint();
}

void Meter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0b0d10));
    g.fillRoundedRectangle (r, 3.0f);
    const float fill = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (level, -60.0f) + 60.0f) / 60.0f);
    const auto& t = theme();
    juce::ColourGradient grad (t.accent2, r.getX(), 0, hotColour.isTransparent() ? t.accent : hotColour, r.getRight(), 0, false);
    if (! hotColour.isTransparent())
        grad.addColour (0.75, t.accent);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r.reduced (2.0f).withWidth ((r.getWidth() - 4.0f) * fill), 2.0f);
    g.setColour (t.dim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("OUT", r.reduced (4.0f, 0.0f), juce::Justification::centredRight);
}

MatrixRow::MatrixRow (juce::AudioProcessorValueTreeState& s, int slot, const juce::String& srcId, const juce::String& dstId, const juce::String& amtId,
                      const juce::StringArray& sources, const juce::StringArray& destinations, const Style& st)
    : style (st)
{
    number.setText (juce::String (slot + 1), juce::dontSendNotification);
    number.setJustificationType (juce::Justification::centred);
    number.setColour (juce::Label::textColourId, theme().accent);
    source.addItemList (sources, 1);
    dest.addItemList (destinations, 1);
    amount.setSliderStyle (juce::Slider::LinearHorizontal);
    amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, style.valueBoxWidth, 20);
    amount.getProperties().set ("bipolar", true);
    amount.setDoubleClickReturnValue (true, 0.0);
    for (auto* c : std::initializer_list<juce::Component*> { &number, &source, &dest, &amount })
        addAndMakeVisible (c);
    if (style.arrow)
    {
        arrow.setText (juce::String::fromUTF8 ("\xe2\x86\x92"), juce::dontSendNotification);
        arrow.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (arrow);
    }
    sa = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, srcId, source);
    da = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, dstId, dest);
    aa = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, amtId, amount);
}

void MatrixRow::resized()
{
    auto r = getLocalBounds().reduced (4, 0);
    const auto centred = [&r] (int w) { return r.removeFromLeft (w).withSizeKeepingCentre (w, 26); };
    number.setBounds (centred (style.numberWidth));
    source.setBounds (centred (style.sourceWidth));
    r.removeFromLeft (style.gap);
    if (style.arrow)
        arrow.setBounds (centred (22));
    dest.setBounds (centred (style.destWidth));
    r.removeFromLeft (style.arrow ? 8 : style.gap);
    amount.setBounds (r.withSizeKeepingCentre (r.getWidth(), 28));
}

void MatrixRow::paint (juce::Graphics& g)
{
    if (! style.filled)
        return;
    g.setColour (juce::Colour (0xff171a20));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
}
} // namespace hwui
