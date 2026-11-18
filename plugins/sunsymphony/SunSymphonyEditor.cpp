#include "sunsymphony/SunSymphonyEditor.h"

namespace sunsymphony
{
namespace
{
const juce::Colour kBackground (0xff15171c), kPanel (0xff1e2128), kGold (0xffe8b04a), kText (0xffe6e6e6), kDim (0xff8a8f99);
}

struct SunSymphonyEditor::PartsModel : public juce::ListBoxModel
{
    explicit PartsModel (SunSymphonyEditor& e) : editor (e) {}
    int getNumRows() override { return orchestra::kParts; }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        g.fillAll (selected ? kGold.withAlpha (0.18f) : (row % 2 == 0 ? kPanel : kPanel.brighter (0.03f)));
        g.setColour (kDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Part " + juce::String (row + 1) + "  ch " + juce::String (row + 1), 8, 0, 110, height, juce::Justification::centredLeft);
        g.setColour (kText);
        g.drawText (editor.processor.partName (row), 120, 0, width - 128, height, juce::Justification::centredLeft, true);
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { editor.choose (row); }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            editor.choose (row);
    }
    SunSymphonyEditor& editor;
};

SunSymphonyEditor::SunSymphonyEditor (SunSymphonyProcessor& p)
    : AudioProcessorEditor (p), processor (p), keyboard (p.keyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    model = std::make_unique<PartsModel> (*this);
    parts.setModel (model.get());
    parts.setRowHeight (22);
    parts.setColour (juce::ListBox::backgroundColourId, kPanel);
    parts.setTooltip ("Double-click (or right-click) a part to choose its instrument");
    addAndMakeVisible (parts);

    master.setSliderStyle (juce::Slider::LinearHorizontal);
    master.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    master.setNumDecimalPlacesToDisplay (2);
    master.setColour (juce::Slider::thumbColourId, kGold);
    master.setColour (juce::Slider::trackColourId, kGold.withAlpha (0.5f));
    if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.getParameters()[0]))
        masterAttachment = std::make_unique<juce::SliderParameterAttachment> (*param, master);
    addAndMakeVisible (master);

    status.setColour (juce::Label::textColourId, kDim);
    addAndMakeVisible (status);
    libraryNote.setColour (juce::Label::textColourId, kDim);
    libraryNote.setFont (juce::FontOptions (12.0f));
    libraryNote.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (libraryNote);

    libraryButton.setColour (juce::TextButton::buttonColourId, kGold);
    libraryButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    libraryButton.setTooltip ("Download the orchestral instruments (free, CC0) and manage the ones installed");
    libraryButton.onClick = [this] { showLibrary (libraryPanel == nullptr); };
    addAndMakeVisible (libraryButton);
    updateLibraryNote();

    keyboard.setAvailableRange (21, 108);
    keyboard.setLowestVisibleKey (36);
    keyboard.setMidiChannel (1);
    addAndMakeVisible (keyboard);

    setSize (760, 580);
    startTimerHz (10);
    // Nothing installed yet: the Library page opens by itself, with the essentials ticked, one click from an orchestra.
    bool any = false;
    for (const auto& i : processor.library().items())
        any = any || processor.library().isInstalled (i);
    if (! any)
        showLibrary (true);
}

SunSymphonyEditor::~SunSymphonyEditor()
{
    stopTimer();
    parts.setModel (nullptr);
}

void SunSymphonyEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    g.setColour (kGold);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("SunSymphony", 16, 10, 300, 32, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("Orchestral sampler  \xc2\xb7  preview ")) + JucePlugin_VersionString, 16, 40, 400, 18, juce::Justification::centredLeft);
    g.drawText ("Master", getWidth() - 300, 14, 60, 22, juce::Justification::centredLeft);
}

void SunSymphonyEditor::resized()
{
    master.setBounds (getWidth() - 240, 14, 224, 22);
    status.setBounds (getWidth() - 300, 40, 284, 18);
    keyboard.setBounds (0, getHeight() - 90, getWidth(), 90);
    libraryButton.setBounds (getWidth() - 16 - 210, getHeight() - 128, 210, 28);
    libraryNote.setBounds (16, getHeight() - 132, getWidth() - 32 - 220, 38);
    parts.setBounds (16, 68, getWidth() - 32, getHeight() - 68 - 140);
    if (libraryPanel != nullptr)
        libraryPanel->setBounds (0, 0, getWidth(), getHeight() - 90);
}

void SunSymphonyEditor::showLibrary (bool show)
{
    if (show && libraryPanel == nullptr)
    {
        libraryPanel = std::make_unique<LibraryPanel> (processor.library());
        libraryPanel->onClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)] { if (safe != nullptr) safe->showLibrary (false); }); };
        addAndMakeVisible (*libraryPanel);
        resized();
    }
    else if (! show && libraryPanel != nullptr)
    {
        libraryPanel.reset();
        refreshParts();
    }
}

void SunSymphonyEditor::updateLibraryNote()
{
    int installed = 0;
    for (const auto& i : processor.library().items())
        installed += processor.library().isInstalled (i) ? 1 : 0;
    libraryNote.setText (installed == 0 ? juce::String ("No orchestral instrument installed yet: press the gold button to download them (free). "
                                                        "Meanwhile part 1 plays a test tone.")
                                        : juce::String (installed) + " orchestral instruments installed. Double-click a part to choose its "
                                              "instrument; the keyboard plays part 1.",
                         juce::dontSendNotification);
}

void SunSymphonyEditor::timerCallback()
{
    if (const auto generation = processor.library().progress().generation; generation != lastGeneration)
    {
        lastGeneration = generation;
        updateLibraryNote();
        refreshParts();
    }
    status.setText (juce::String (processor.activeVoices()) + " / " + juce::String (orchestra::kMaxVoices) + " voices",
                    juce::dontSendNotification);
    // 0.84.0: a keyswitch changes what a part shows
    std::array<int, orchestra::kParts> now {};
    for (int p = 0; p < orchestra::kParts; ++p)
        now[static_cast<size_t> (p)] = processor.partArticulation (p);
    if (now != shownArticulations)
    {
        shownArticulations = now;
        refreshParts();
    }
}

void SunSymphonyEditor::refreshParts()
{
    parts.updateContent();
    parts.repaint();
}

void SunSymphonyEditor::choose (int part)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Part " + juce::String (part + 1));
    // The installed orchestral instruments, by family, each with its articulations.
    auto& lib = processor.library();
    auto choices = std::make_shared<juce::StringArray>(); // menu id 1000 + n -> SFZ file
    juce::PopupMenu families;
    juce::String family;
    juce::PopupMenu current;
    auto flush = [&]
    {
        if (current.getNumItems() > 0)
            families.addSubMenu (family, current);
        current = juce::PopupMenu();
    };
    for (const auto& item : lib.items())
    {
        if (! lib.isInstalled (item))
            continue;
        if (item.family != family)
        {
            flush();
            family = item.family;
        }
        juce::PopupMenu arts;
        if (item.articulations.size() > 1) // 0.84.0: every articulation in one, chosen by keyswitches
        {
            const auto all = "library:" + item.id;
            choices->add (all);
            arts.addItem (1000 + choices->size() - 1, "All articulations (keyswitches)", true, processor.partSource (part) == all);
            arts.addSeparator();
        }
        for (const auto& a : item.articulations)
        {
            const auto path = lib.sfzFile (item, a).getFullPathName();
            choices->add (path);
            arts.addItem (1000 + choices->size() - 1, a.name, true, processor.partSource (part) == path);
        }
        current.addSubMenu (item.name, arts);
    }
    flush();
    if (families.getNumItems() > 0)
        menu.addSubMenu ("Orchestra", families);
    else
        menu.addItem (4, "Download orchestral instruments...");
    menu.addItem (1, "Test Tone (built in)");
    menu.addItem (2, "Load an SFZ file...");
    menu.addSeparator();
    menu.addItem (3, "Empty", processor.partSource (part).isNotEmpty());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&parts), [this, part, choices] (int result)
    {
        juce::String error;
        if (result >= 1000 && result - 1000 < choices->size())
        {
            juce::MouseCursor::showWaitCursor();
            if (! processor.loadInstrument (part, (*choices)[result - 1000], error) || error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "SunSymphony", error);
            juce::MouseCursor::hideWaitCursor();
        }
        else if (result == 4)
            showLibrary (true);
        else if (result == 1)
            processor.loadInstrument (part, "test-tone", error);
        else if (result == 3)
            processor.loadInstrument (part, {}, error);
        else if (result == 2)
        {
            const auto start = SunSymphonyProcessor::defaultLibraryFolder();
            chooser = std::make_unique<juce::FileChooser> ("Choose an SFZ instrument", start.isDirectory() ? start : juce::File(), "*.sfz");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, part] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file == juce::File())
                    return;
                juce::String err;
                if (! processor.loadInstrument (part, file.getFullPathName(), err) || err.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "SunSymphony", err);
                refreshParts();
            });
            return;
        }
        refreshParts();
    });
}
} // namespace sunsymphony
