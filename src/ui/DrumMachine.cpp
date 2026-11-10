#include "ui/DrumMachine.h"

#include <algorithm>

#include "audio/BuiltinSamples.h"
#include "ui/TextPrompt.h"

namespace sls
{
namespace
{
const juce::Colour kBg { 0xff1b1e24 };
const juce::Colour kPanel { 0xff22262e };
const juce::Colour kPadOff { 0xff2b2f38 };
const juce::Colour kPadOn { 0xff3c4f6a };
const juce::Colour kAccent { 0xffe8943a };
const juce::Colour kStepOff { 0xff262a32 };
const juce::Colour kStepOffAlt { 0xff2d323b };
const juce::Colour kStepOn { 0xffe8795a };
const juce::Colour kPlayhead { 0xffffb347 };
const juce::Colour kText { 0xffd5dae3 };
const juce::Colour kDim { 0xff8a93a3 };
constexpr int kTopBar = 34;
constexpr int kHeaderRow = 16;
constexpr int kMargin = 8;
constexpr int kInspectorRowTop = 64;
constexpr int kLayerRowHeight = 30;
constexpr int kLayerRowStep = 32;
constexpr std::array<int, kPadLayers> kDefaultLayerVelocity { 1, 64, 96, 112 };

float velocityForModifiers (const juce::ModifierKeys& m)
{
    if (m.isShiftDown())
        return 0.45f;
    if (m.isCommandDown() || m.isCtrlDown())
        return 1.0f;
    return 0.8f;
}

juce::String sampleLabel (const std::shared_ptr<const SampleData>& s)
{
    if (s == nullptr)
        return "(empty)";
    if (s->numFrames() < 2)
        return juce::File (juce::String (s->source)).getFileName() + " (file not found)";
    return s->name.empty() ? juce::File (juce::String (s->source)).getFileName() : juce::String (s->name);
}
} // namespace

DrumMachineView::DrumMachineView (AudioEngine& engineToUse) : engine (engineToUse)
{
    setWantsKeyboardFocus (false);

    kitLabel.setJustificationType (juce::Justification::centredLeft);
    kitLabel.setColour (juce::Label::textColourId, kText);
    kitLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (kitLabel);

    for (auto* b : { &loadKitButton, &saveKitButton, &clearKitButton })
        addAndMakeVisible (*b);
    loadKitButton.setTooltip ("Replace all the pads with a kit: one built into the program, a folder of sounds, or one of your kits");
    saveKitButton.setTooltip ("Save this kit as a folder (kit.json and copies of its sounds) in your Drum Kits folder");
    clearKitButton.setTooltip ("Remove every sound from the pads");
    loadKitButton.onClick = [this] { showKitMenu(); };
    saveKitButton.onClick = [this] { saveKit(); };
    clearKitButton.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Empty kit",
                                            "Remove every sound and setting from all 16 pads? (The steps in the pattern stay.)", "Empty it", "Cancel", this,
                                            juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<DrumMachineView> (this)] (int result)
                                                                                 {
                                                                                     if (safe == nullptr || result != 1)
                                                                                         return;
                                                                                     safe->editKit ([] (DrumKit& k) { k = DrumKit::empty(); k.name = "Empty kit"; });
                                                                                 }));
    };

    nameEditor.setMultiLine (false);
    nameEditor.setReturnKeyStartsNewLine (false);
    nameEditor.setInputRestrictions (24);
    nameEditor.setTooltip ("The name of the pad");
    nameEditor.onReturnKey = [this]
    {
        if (updating)
            return;
        const auto text = nameEditor.getText().trim().toStdString();
        if (! text.empty())
            editPad ([&text] (DrumPad& p) { p.name = text; });
        nameEditor.giveAwayKeyboardFocus();
    };
    nameEditor.onFocusLost = [this] { nameEditor.onReturnKey(); };
    addAndMakeVisible (nameEditor);

    for (auto* b : { &muteButton, &soloButton })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    addAndMakeVisible (previewButton);
    muteButton.onClick = [this] { if (! updating) editPad ([this] (DrumPad& p) { p.mute = muteButton.getToggleState(); }); };
    soloButton.onClick = [this] { if (! updating) editPad ([this] (DrumPad& p) { p.solo = soloButton.getToggleState(); }); };
    muteButton.setTooltip ("Silence this pad");
    soloButton.setTooltip ("Play only the soloed pads");
    previewButton.setTooltip ("Hear the pad (click a pad: Shift = soft, Ctrl = hard)");
    previewButton.onClick = [this] { preview (selected, 0.8f); };

    chokeBox.addItem ("None", 1);
    for (int g = 1; g <= kChokeGroups; ++g)
        chokeBox.addItem ("Group " + juce::String (g), g + 1);
    chokeBox.setTooltip ("Pads of the same group cut each other off when they start (a closed hi-hat stops an open one)");
    chokeBox.onChange = [this] { if (! updating) editPad ([this] (DrumPad& p) { p.chokeGroup = chokeBox.getSelectedId() - 1; }); };
    addAndMakeVisible (chokeBox);

    for (size_t l = 0; l < kPadLayers; ++l)
    {
        auto& name = layerNames[l];
        name.setColour (juce::Label::textColourId, kText);
        name.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (name);

        layerLoad[l].setButtonText ("Load");
        layerLoad[l].setTooltip ("Choose a sound for layer " + juce::String (static_cast<int> (l) + 1) + " (you can also drop one from the browser)");
        layerLoad[l].onClick = [this, l] { chooseLayerSample (static_cast<int> (l)); };
        addAndMakeVisible (layerLoad[l]);

        layerClear[l].setButtonText ("x");
        layerClear[l].setTooltip ("Remove this layer's sound");
        layerClear[l].onClick = [this, l] { setLayerSample (selected, static_cast<int> (l), nullptr); };
        addAndMakeVisible (layerClear[l]);

        auto& v = layerVelocity[l];
        v.setSliderStyle (juce::Slider::LinearHorizontal);
        v.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
        v.setRange (1.0, 127.0, 1.0);
        v.setTextValueSuffix ("+");
        v.setTooltip ("This layer plays from this velocity up (1 to 127) until a layer with a higher number takes over");
        v.onValueChange = [this, l]
        {
            if (! updating)
                editPad ([this, l] (DrumPad& p) { p.layers[l].minVelocity = static_cast<int> (layerVelocity[l].getValue()); });
        };
        addAndMakeVisible (v);
    }

    const auto setup = [this] (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix, const juce::String& tip)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.setTooltip (tip);
        addAndMakeVisible (s);
    };
    setup (volumeSlider, 0.0, 150.0, 1.0, " %", "Level of the pad (100 % = as recorded)");
    setup (panSlider, -100.0, 100.0, 1.0, "", "Pan of the pad: -100 left, 0 centre, 100 right");
    setup (pitchSlider, -24.0, 24.0, 0.01, " st", "Tuning of the pad in semitones (it also changes the length of the sound)");
    setup (swingSlider, 0.0, 100.0, 1.0, " %", "Delays this pad's off-beat 16th steps (the swing of the channel is not used for drums)");
    volumeSlider.onValueChange = [this] { if (! updating) editPad ([this] (DrumPad& p) { p.volume = static_cast<float> (volumeSlider.getValue() / 100.0); }); };
    panSlider.onValueChange = [this] { if (! updating) editPad ([this] (DrumPad& p) { p.pan = static_cast<float> (panSlider.getValue() / 100.0); }); };
    pitchSlider.onValueChange = [this] { if (! updating) editPad ([this] (DrumPad& p) { p.pitch = static_cast<float> (pitchSlider.getValue()); }); };
    swingSlider.onValueChange = [this] { if (! updating) editPad ([this] (DrumPad& p) { p.swing = static_cast<float> (swingSlider.getValue() / 100.0); }); };
    volumeSlider.setDoubleClickReturnValue (true, 100.0);
    panSlider.setDoubleClickReturnValue (true, 0.0);
    pitchSlider.setDoubleClickReturnValue (true, 0.0);
    swingSlider.setDoubleClickReturnValue (true, 0.0);

    for (auto* l : { &volumeLabel, &panLabel, &pitchLabel, &swingLabel, &chokeLabel })
    {
        l->setColour (juce::Label::textColourId, kDim);
        addAndMakeVisible (*l);
    }
    updateInspector();
}

const DrumKit* DrumMachineView::kit() const
{
    const Channel* c = getChannel ? getChannel() : nullptr;
    return c != nullptr ? c->kit.get() : nullptr;
}

void DrumMachineView::setLoopTicks (int ticks)
{
    loopTicks = std::max (kSixteenthTicks, ticks);
    repaint();
}

void DrumMachineView::setPlayheadTick (int tick)
{
    if (tick != playheadTick)
    {
        playheadTick = tick;
        repaint (stepsArea);
    }
}

void DrumMachineView::refresh()
{
    updateInspector();
    repaint();
}

void DrumMachineView::selectPad (int pad)
{
    selected = std::clamp (pad, 0, kDrumPads - 1);
    updateInspector();
    repaint();
}

void DrumMachineView::editKit (const std::function<void (DrumKit&)>& change)
{
    Channel* c = getChannel ? getChannel() : nullptr;
    if (c == nullptr || c->kit == nullptr)
        return;
    DrumKit copy = *c->kit;
    change (copy);
    c->kit = std::make_shared<const DrumKit> (copy.sanitised());
    if (onKitChanged)
        onKitChanged();
    updateInspector();
    repaint();
}

void DrumMachineView::preview (int pad, float velocity)
{
    engine.liveNoteOn (channelIndex, DrumKit::pitchForPad (pad), velocity);
}

void DrumMachineView::setLayerSample (int pad, int layer, const std::shared_ptr<const SampleData>& sample)
{
    editKit ([=] (DrumKit& k)
             {
                 auto& p = k.pads[static_cast<size_t> (pad)];
                 auto& l = p.layers[static_cast<size_t> (layer)];
                 l.sample = sample;
                 if (sample == nullptr)
                     return;
                 if (l.minVelocity <= 1)
                     l.minVelocity = kDefaultLayerVelocity[static_cast<size_t> (layer)];
                 if (layer == 0 && p.name.rfind ("Pad ", 0) == 0 && ! sample->name.empty())
                     p.name = sample->name.substr (0, 24);
             });
}

void DrumMachineView::chooseLayerSample (int layer)
{
    const int pad = selected;
    chooser = std::make_unique<juce::FileChooser> ("Choose a sound for pad " + juce::String (pad + 1) + ", layer " + juce::String (layer + 1),
                                                  juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.flac;*.ogg;*.oga;*.aif;*.aiff;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<DrumMachineView> (this), pad, layer] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (safe == nullptr || file == juce::File())
                                  return;
                              juce::String problem;
                              auto sample = loadKitSample (file.getFullPathName().toStdString(), problem);
                              if (sample == nullptr)
                                  safe->say (problem);
                              else
                                  safe->setLayerSample (pad, layer, sample);
                          });
}

void DrumMachineView::loadKitFromSpec (const KitSpec& spec)
{
    juce::StringArray problems;
    juce::MouseCursor::showWaitCursor();
    const auto loaded = loadKit (spec, problems);
    juce::MouseCursor::hideWaitCursor();
    editKit ([&loaded] (DrumKit& k) { k = loaded; });
    say ("Loaded the kit \"" + juce::String (spec.name) + "\"" + (problems.isEmpty() ? juce::String() : " (" + problems.joinIntoString ("; ") + ")"));
}

void DrumMachineView::showKitMenu()
{
    juce::PopupMenu menu;
    // The built-in kits (sampled kits first, then the kits made by code), items 1000 + their index.
    const auto& builtin = builtinKits();
    menu.addSectionHeader ("Built-in kits");
    for (size_t i = 0; i < builtin.size(); ++i)
        menu.addItem (1000 + static_cast<int> (i), builtin[i].name);
    menu.addSeparator();
    menu.addItem (3, "From a folder...");
    juce::Array<juce::File> folders;
    if (kitsFolder != juce::File())
        folders = kitsFolder.findChildFiles (juce::File::findDirectories, false);
    std::sort (folders.begin(), folders.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    if (folders.size() > 60)
        folders.resize (60);
    if (! folders.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("My kits");
        for (int i = 0; i < folders.size(); ++i)
            menu.addItem (100 + i, folders[i].getFileName());
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadKitButton),
                        [safe = juce::Component::SafePointer<DrumMachineView> (this), folders] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result >= 1000 && result < 1000 + static_cast<int> (builtinKits().size()))
                                safe->loadKitFromSpec (builtinKitSpec (builtinKits()[static_cast<size_t> (result - 1000)].id));
                            else if (result == 3)
                            {
                                safe->chooser = std::make_unique<juce::FileChooser> ("Choose a kit folder (with a kit.json, or a folder of sounds)",
                                                                                      safe->kitsFolder != juce::File() ? safe->kitsFolder : juce::File::getSpecialLocation (juce::File::userMusicDirectory));
                                safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                                            [safe] (const juce::FileChooser& fc)
                                                            {
                                                                const auto folder = fc.getResult();
                                                                if (safe == nullptr || folder == juce::File())
                                                                    return;
                                                                juce::StringArray problems;
                                                                const auto spec = readKitFolder (folder, problems);
                                                                if (! isKitFolder (folder) && ! problems.isEmpty() && spec.pads[0].sources[0].empty())
                                                                    safe->say (problems.joinIntoString ("; "));
                                                                else
                                                                    safe->loadKitFromSpec (spec);
                                                            });
                            }
                            else if (result >= 100 && result - 100 < folders.size())
                            {
                                juce::StringArray problems;
                                const auto spec = readKitFolder (folders[result - 100], problems);
                                if (spec.pads[0].sources[0].empty() && ! problems.isEmpty())
                                    safe->say (problems.joinIntoString ("; "));
                                else
                                    safe->loadKitFromSpec (spec);
                            }
                        });
}

void DrumMachineView::saveKit()
{
    const auto* k = kit();
    if (k == nullptr)
        return;
    if (kitsFolder == juce::File())
    {
        say ("There is no Drum Kits folder to save in.");
        return;
    }
    promptForText (*this, "Save kit", "Kit name (a folder of this name is made in your Drum Kits folder):", juce::String (k->name), "Save",
                   [safe = juce::Component::SafePointer<DrumMachineView> (this)] (const juce::String& typed)
                   {
                       const auto name = typed.trim().substring (0, 32);
                       const auto fileName = juce::File::createLegalFileName (name);
                       if (safe == nullptr || name.isEmpty() || fileName.isEmpty())
                           return;
                       const auto folder = safe->kitsFolder.getChildFile (fileName);
                       const auto doSave = [safe, folder, name]
                       {
                           if (safe == nullptr)
                               return;
                           safe->editKit ([&name] (DrumKit& kitToName) { kitToName.name = name.toStdString(); });
                           const auto* current = safe->kit();
                           if (current == nullptr)
                               return;
                           const auto error = saveKitFolder (*current, folder);
                           if (error.isNotEmpty())
                           {
                               safe->say ("Could not save the kit: " + error);
                               juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not save the kit", error);
                           }
                           else
                               safe->say ("Saved the kit in " + folder.getFullPathName());
                       };
                       if (isKitFolder (folder))
                           juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Replace the kit?",
                                                               "A kit called \"" + name + "\" already exists. Replace it?", "Replace", "Cancel", safe.getComponent(),
                                                               juce::ModalCallbackFunction::create ([doSave] (int result) { if (result == 1) doSave(); }));
                       else
                           doSave();
                   });
}

void DrumMachineView::updateInspector()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto* k = kit();
    const bool have = k != nullptr;
    for (auto* c : std::initializer_list<juce::Component*> { &nameEditor, &muteButton, &soloButton, &previewButton, &chokeBox, &volumeSlider, &panSlider,
                                                              &pitchSlider, &swingSlider, &saveKitButton, &clearKitButton, &loadKitButton })
        c->setEnabled (have);
    kitLabel.setText (have ? "Kit: " + juce::String (k->name) : juce::String ("No drum kit"), juce::dontSendNotification);
    if (! have)
        return;

    const auto& p = k->pads[static_cast<size_t> (selected)];
    if (! nameEditor.hasKeyboardFocus (true))
        nameEditor.setText (juce::String (p.name), false);
    muteButton.setToggleState (p.mute, juce::dontSendNotification);
    soloButton.setToggleState (p.solo, juce::dontSendNotification);
    chokeBox.setSelectedId (p.chokeGroup + 1, juce::dontSendNotification);
    volumeSlider.setValue (p.volume * 100.0, juce::dontSendNotification);
    panSlider.setValue (p.pan * 100.0, juce::dontSendNotification);
    pitchSlider.setValue (p.pitch, juce::dontSendNotification);
    swingSlider.setValue (p.swing * 100.0, juce::dontSendNotification);
    for (size_t l = 0; l < kPadLayers; ++l)
    {
        const bool filled = p.layers[l].sample != nullptr;
        layerNames[l].setText (juce::String (static_cast<int> (l) + 1) + "   " + sampleLabel (p.layers[l].sample), juce::dontSendNotification);
        layerNames[l].setColour (juce::Label::textColourId, filled ? kText : kDim);
        layerClear[l].setEnabled (filled);
        layerVelocity[l].setEnabled (filled);
        layerVelocity[l].setValue (p.layers[l].minVelocity, juce::dontSendNotification);
    }
}

// ---------------------------------------------------------------------------------------------------------- geometry

void DrumMachineView::resized()
{
    auto area = getLocalBounds().reduced (kMargin);
    auto top = area.removeFromTop (kTopBar - kMargin);
    clearKitButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (6);
    saveKitButton.setBounds (top.removeFromRight (100));
    top.removeFromRight (6);
    loadKitButton.setBounds (top.removeFromRight (100));
    kitLabel.setBounds (top);

    const int padsH = 288;
    padsArea = { kMargin, kTopBar + 6, 344, padsH };
    inspectorArea = { padsArea.getRight() + 12, padsArea.getY(), std::max (300, getWidth() - padsArea.getRight() - 12 - kMargin), padsH };
    stepsArea = { kMargin, padsArea.getBottom() + 10, getWidth() - 2 * kMargin, std::max (60, getHeight() - padsArea.getBottom() - 10 - kMargin) };

    const int x0 = inspectorArea.getX() + 10;
    const int w = inspectorArea.getWidth() - 20;
    int y = inspectorArea.getY() + 8;
    nameEditor.setBounds (x0, y, 190, 24);
    muteButton.setBounds (x0 + 198, y, 56, 24);
    soloButton.setBounds (x0 + 260, y, 56, 24);
    previewButton.setBounds (x0 + 322, y, 84, 24);
    y += 30;
    chokeLabel.setBounds (x0, y, 90, 24);
    chokeBox.setBounds (x0 + 94, y, 130, 24);

    for (size_t l = 0; l < kPadLayers; ++l)
    {
        const auto row = layerRowRect (static_cast<int> (l)).reduced (6, 2);
        const int velW = std::min (200, row.getWidth() / 2 - 40);
        layerNames[l].setBounds (row.getX(), row.getY(), row.getWidth() - velW - 96, row.getHeight());
        layerLoad[l].setBounds (row.getRight() - velW - 92, row.getY() + 1, 52, row.getHeight() - 2);
        layerClear[l].setBounds (row.getRight() - velW - 36, row.getY() + 1, 28, row.getHeight() - 2);
        layerVelocity[l].setBounds (row.getRight() - velW, row.getY(), velW, row.getHeight());
    }

    y = inspectorArea.getY() + kInspectorRowTop + 4 * kLayerRowStep + 6;
    for (auto [label, slider] : { std::pair<juce::Label*, juce::Slider*> { &volumeLabel, &volumeSlider }, { &panLabel, &panSlider },
                                  { &pitchLabel, &pitchSlider }, { &swingLabel, &swingSlider } })
    {
        label->setBounds (x0, y, 60, 20);
        slider->setBounds (x0 + 62, y, std::min (w - 62, 360), 20);
        y += 21;
    }
}

juce::Rectangle<int> DrumMachineView::padRect (int pad) const
{
    const int gap = 4;
    const int cw = (padsArea.getWidth() - 3 * gap) / 4;
    const int ch = (padsArea.getHeight() - 3 * gap) / 4;
    const int col = pad % 4;
    const int row = 3 - pad / 4; // pad 1 at the bottom left, like a pad controller
    return { padsArea.getX() + col * (cw + gap), padsArea.getY() + row * (ch + gap), cw, ch };
}

int DrumMachineView::padAt (juce::Point<int> p) const
{
    for (int i = 0; i < kDrumPads; ++i)
        if (padRect (i).contains (p))
            return i;
    return -1;
}

juce::Rectangle<int> DrumMachineView::layerRowRect (int layer) const
{
    return { inspectorArea.getX() + 4, inspectorArea.getY() + kInspectorRowTop + layer * kLayerRowStep, inspectorArea.getWidth() - 8, kLayerRowHeight };
}

int DrumMachineView::layerRowAt (juce::Point<int> p) const
{
    for (int l = 0; l < kPadLayers; ++l)
        if (layerRowRect (l).contains (p))
            return l;
    return -1;
}

juce::Rectangle<int> DrumMachineView::stepCell (int pad, int step) const
{
    const int gridX = stepsArea.getX() + stepLabelWidth;
    const int gridW = stepsArea.getWidth() - stepLabelWidth;
    const int rowH = std::clamp ((stepsArea.getHeight() - kHeaderRow) / kDrumPads, 10, 22);
    const int x0 = gridX + step * gridW / stepCount();
    const int x1 = gridX + (step + 1) * gridW / stepCount();
    return { x0, stepsArea.getY() + kHeaderRow + pad * rowH, x1 - x0, rowH };
}

bool DrumMachineView::stepAt (juce::Point<int> p, int& pad, int& step) const
{
    const int gridX = stepsArea.getX() + stepLabelWidth;
    const int gridW = stepsArea.getWidth() - stepLabelWidth;
    const int rowH = std::clamp ((stepsArea.getHeight() - kHeaderRow) / kDrumPads, 10, 22);
    if (p.x < gridX || p.x >= gridX + gridW || p.y < stepsArea.getY() + kHeaderRow || gridW <= 0)
        return false;
    pad = (p.y - stepsArea.getY() - kHeaderRow) / rowH;
    step = (p.x - gridX) * stepCount() / gridW;
    return pad >= 0 && pad < kDrumPads && step >= 0 && step < stepCount();
}

bool DrumMachineView::stepIsOn (const Channel& c, int pad, int step) const
{
    const int pitch = DrumKit::pitchForPad (pad);
    const int from = step * kSixteenthTicks;
    for (const auto& n : c.notes)
        if (n.pitch == pitch && n.startTick >= from && n.startTick < from + kSixteenthTicks)
            return true;
    return false;
}

void DrumMachineView::setStep (int pad, int step, bool on, float velocity)
{
    Channel* c = getChannel ? getChannel() : nullptr;
    if (c == nullptr)
        return;
    const int pitch = DrumKit::pitchForPad (pad);
    const int from = step * kSixteenthTicks;
    const auto before = c->notes.size();
    c->notes.erase (std::remove_if (c->notes.begin(), c->notes.end(),
                                    [=] (const Note& n) { return n.pitch == pitch && n.startTick >= from && n.startTick < from + kSixteenthTicks; }),
                    c->notes.end());
    bool changed = c->notes.size() != before;
    if (on)
    {
        Note n;
        n.pitch = pitch;
        n.startTick = from;
        n.lengthTicks = kSixteenthTicks;
        n.velocity = velocity;
        c->notes.push_back (n);
        changed = true;
    }
    changedBySteps = changedBySteps || changed;
}

// ------------------------------------------------------------------------------------------------------------ mouse

void DrumMachineView::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const int pad = padAt (p);
    if (pad >= 0)
    {
        selectPad (pad);
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            const auto* k = kit();
            menu.addItem (1, "Load a sound into layer 1...");
            menu.addItem (2, "Clear this pad", k != nullptr && k->pads[static_cast<size_t> (pad)].hasSample());
            menu.addItem (3, "Mute", true, k != nullptr && k->pads[static_cast<size_t> (pad)].mute);
            menu.addItem (4, "Solo", true, k != nullptr && k->pads[static_cast<size_t> (pad)].solo);
            menu.showMenuAsync (juce::PopupMenu::Options(),
                                [safe = juce::Component::SafePointer<DrumMachineView> (this), pad] (int result)
                                {
                                    if (safe == nullptr || result == 0)
                                        return;
                                    if (result == 1)
                                        safe->chooseLayerSample (0);
                                    else if (result == 2)
                                        safe->editKit ([pad] (DrumKit& whole) { auto& q = whole.pads[static_cast<size_t> (pad)]; q.layers = {}; q.name = "Pad " + std::to_string (pad + 1); });
                                    else if (result == 3)
                                        safe->editPad ([] (DrumPad& q) { q.mute = ! q.mute; });
                                    else if (result == 4)
                                        safe->editPad ([] (DrumPad& q) { q.solo = ! q.solo; });
                                });
        }
        else
            preview (pad, velocityForModifiers (e.mods));
        return;
    }

    int stepPad = 0, step = 0;
    if (stepAt (p, stepPad, step))
    {
        const Channel* c = getChannel ? getChannel() : nullptr;
        if (c == nullptr)
            return;
        selectPad (stepPad);
        painting = true;
        changedBySteps = false;
        paintVelocity = velocityForModifiers (e.mods);
        paintValue = ! e.mods.isPopupMenu() && ! stepIsOn (*c, stepPad, step);
        lastPaintPad = stepPad;
        lastPaintStep = step;
        setStep (stepPad, step, paintValue, paintVelocity);
        if (paintValue && kit() != nullptr)
            preview (stepPad, paintVelocity); // hear what was put there
        repaint (stepsArea);
        return;
    }

    if (e.x < stepsArea.getX() + stepLabelWidth && e.y >= stepsArea.getY() + kHeaderRow && e.x >= stepsArea.getX())
    {
        const int rowH = std::clamp ((stepsArea.getHeight() - kHeaderRow) / kDrumPads, 10, 22);
        const int row = (e.y - stepsArea.getY() - kHeaderRow) / rowH;
        if (row >= 0 && row < kDrumPads)
        {
            selectPad (row);
            if (! e.mods.isPopupMenu())
                preview (row, 0.8f);
        }
    }
}

void DrumMachineView::mouseDrag (const juce::MouseEvent& e)
{
    if (! painting)
        return;
    int pad = 0, step = 0;
    if (stepAt (e.getPosition(), pad, step) && (pad != lastPaintPad || step != lastPaintStep))
    {
        lastPaintPad = pad;
        lastPaintStep = step;
        setStep (pad, step, paintValue, paintVelocity);
        repaint (stepsArea);
    }
}

void DrumMachineView::mouseUp (const juce::MouseEvent&)
{
    if (! painting)
        return;
    painting = false;
    if (changedBySteps && onNotesChanged)
        onNotesChanged();
    changedBySteps = false;
}

// -------------------------------------------------------------------------------------------------------- drag and drop

bool DrumMachineView::isInterestedInDragSource (const SourceDetails& details)
{
    drag::Item item;
    return drag::Item::parse (details.description, item) && (item.kind == "file" || item.kind == "builtin") && kit() != nullptr;
}

void DrumMachineView::itemDropped (const SourceDetails& details)
{
    drag::Item item;
    if (! drag::Item::parse (details.description, item))
        return;

    const auto at = details.localPosition;
    int pad = padAt (at);
    int layer = 0;
    if (pad < 0 && layerRowAt (at) >= 0)
    {
        pad = selected;
        layer = layerRowAt (at);
    }
    if (pad < 0)
    {
        say ("Drop the sound on a pad (or on one of the layers of the selected pad).");
        return;
    }

    std::shared_ptr<const SampleData> sample;
    if (item.kind == "builtin")
        sample = makeBuiltinSample (item.payload.toStdString());
    else
    {
        juce::String problem;
        sample = loadKitSample (item.payload.toStdString(), problem);
        if (sample == nullptr)
            say (problem);
    }
    if (sample == nullptr)
        return;
    selectPad (pad);
    setLayerSample (pad, layer, sample);
    say ("Put the sound on pad " + juce::String (pad + 1) + (layer > 0 ? ", layer " + juce::String (layer + 1) : juce::String()));
}

// ------------------------------------------------------------------------------------------------------------ painting

void DrumMachineView::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    const auto* k = kit();
    const Channel* c = getChannel ? getChannel() : nullptr;

    // pads
    for (int i = 0; i < kDrumPads; ++i)
    {
        const auto r = padRect (i).toFloat();
        const bool has = k != nullptr && k->pads[static_cast<size_t> (i)].hasSample();
        const bool muted = k != nullptr && k->pads[static_cast<size_t> (i)].mute;
        g.setColour (has ? kPadOn : kPadOff);
        g.fillRoundedRectangle (r, 6.0f);
        if (i == selected)
        {
            g.setColour (kAccent);
            g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 2.0f);
        }
        if (k == nullptr)
            continue;
        const auto& p = k->pads[static_cast<size_t> (i)];
        g.setColour (muted ? kDim.withAlpha (0.5f) : kText);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (juce::String (p.name), r.reduced (6.0f, 18.0f).toNearestInt(), juce::Justification::centred, 2);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::String (i + 1) + "  " + juce::MidiMessage::getMidiNoteName (DrumKit::pitchForPad (i), true, true, 5), r.reduced (6.0f, 3.0f).toNearestInt(),
                    juce::Justification::topLeft);
        if (muted)
            g.drawText ("M", r.reduced (6.0f, 3.0f).toNearestInt(), juce::Justification::topRight);
        else if (p.solo)
        {
            g.setColour (kAccent);
            g.drawText ("S", r.reduced (6.0f, 3.0f).toNearestInt(), juce::Justification::topRight);
        }
        if (p.chokeGroup > 0)
        {
            g.setColour (kAccent.withAlpha (0.9f));
            g.drawText ("choke " + juce::String (p.chokeGroup), r.reduced (6.0f, 3.0f).toNearestInt(), juce::Justification::bottomLeft);
        }
        int layers = 0;
        for (const auto& l : p.layers)
            layers += l.sample != nullptr ? 1 : 0;
        for (int l = 0; l < layers; ++l)
        {
            g.setColour (kText.withAlpha (0.8f));
            g.fillRect (r.getRight() - 10.0f - 7.0f * static_cast<float>(layers - 1 - l), r.getBottom() - 9.0f, 5.0f, 5.0f);
        }
    }

    // inspector
    g.setColour (kPanel);
    g.fillRoundedRectangle (inspectorArea.toFloat(), 6.0f);
    for (int l = 0; l < kPadLayers; ++l)
    {
        g.setColour (kBg.withAlpha (0.6f));
        g.fillRoundedRectangle (layerRowRect (l).toFloat(), 4.0f);
    }
    if (k != nullptr)
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Pad " + juce::String (selected + 1) + " (note " + juce::String (DrumKit::pitchForPad (selected)) + ")",
                    inspectorArea.getRight() - 150, inspectorArea.getY() + 10, 140, 18, juce::Justification::centredRight);
    }

    // steps
    g.setColour (kPanel);
    g.fillRoundedRectangle (stepsArea.toFloat(), 6.0f);
    if (c == nullptr || k == nullptr)
        return;
    g.setFont (juce::FontOptions (10.0f));
    const int steps = stepCount();
    for (int s = 0; s < steps; s += 4)
    {
        g.setColour (kDim);
        const auto first = stepCell (0, s);
        g.drawText (juce::String (s / 4 + 1), first.getX(), stepsArea.getY() + 1, std::max (12, first.getWidth() * 4), kHeaderRow - 2, juce::Justification::centredLeft);
    }
    for (int pad = 0; pad < kDrumPads; ++pad)
    {
        const auto label = stepCell (pad, 0).withX (stepsArea.getX() + 4).withWidth (stepLabelWidth - 8);
        g.setColour (pad == selected ? kAccent : kDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (pad + 1) + "  " + juce::String (k->pads[static_cast<size_t> (pad)].name), label, juce::Justification::centredLeft, true);
        for (int s = 0; s < steps; ++s)
        {
            const auto cell = stepCell (pad, s).reduced (1);
            g.setColour ((s / 4) % 2 == 0 ? kStepOff : kStepOffAlt);
            g.fillRoundedRectangle (cell.toFloat(), 2.0f);
        }
    }
    for (const auto& n : c->notes)
    {
        const int pad = DrumKit::padForPitch (n.pitch);
        if (pad < 0 || n.startTick < 0)
            continue;
        const int s = n.startTick / kSixteenthTicks;
        if (s >= steps)
            continue;
        g.setColour (kStepOn.withAlpha (0.35f + 0.65f * std::clamp (n.velocity, 0.0f, 1.0f)));
        g.fillRoundedRectangle (stepCell (pad, s).reduced (1).toFloat(), 2.0f);
    }
    if (playheadTick >= 0)
    {
        const int gridX = stepsArea.getX() + stepLabelWidth;
        const int gridW = stepsArea.getWidth() - stepLabelWidth;
        const int x = gridX + static_cast<int> (static_cast<double> (playheadTick) / loopTicks * gridW);
        g.setColour (kPlayhead);
        g.drawVerticalLine (x, static_cast<float> (stepsArea.getY() + kHeaderRow), static_cast<float> (stepCell (kDrumPads - 1, 0).getBottom()));
    }
}

// -------------------------------------------------------------------------------------------------------------- window

DrumMachineWindow::DrumMachineWindow (AudioEngine& engine)
    : juce::DocumentWindow ("Drum machine", juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton), content (engine)
{
    setUsingNativeTitleBar (false);
    setContentNonOwned (&content, false);
    setResizable (true, true);
    setResizeLimits (860, 640, 2400, 1000);
    centreWithSize (1000, 700);
    setAlwaysOnTop (true);
}
} // namespace sls
