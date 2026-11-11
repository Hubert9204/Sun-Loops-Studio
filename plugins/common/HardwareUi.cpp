#include "common/HardwareUi.h"

namespace hwui
{
Theme& theme()
{
    static Theme current;
    return current;
}

void drawScrew (juce::Graphics& g, float cx, float cy)
{
    g.setColour (juce::Colour (0xff0b0c0f));
    g.fillEllipse (cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
    g.setColour (juce::Colour (0xff4a515d));
    g.fillEllipse (cx - 3.0f, cy - 3.0f, 6.0f, 6.0f);
    g.setColour (juce::Colour (0xff1a1d22));
    g.drawLine (cx - 2.2f, cy - 2.2f, cx + 2.2f, cy + 2.2f, 1.0f);
}

void styleLabel (juce::Label& l)
{
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, theme().dim);
    l.setInterceptsMouseClicks (false, false);
}

// --- Look and feel ------------------------------------------------------------------------------------------------------

Look::Look()
{
    applyColours();
}

Look::Look (const Theme& t)
{
    theme() = t;
    applyColours();
}

void Look::applyColours()
{
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0d0f13));
    setColour (juce::ComboBox::textColourId, theme().text);
    setColour (juce::ComboBox::outlineColourId, theme().edge);
    setColour (juce::ComboBox::arrowColourId, theme().accent);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1a1d23));
    setColour (juce::PopupMenu::textColourId, theme().text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme().accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::PopupMenu::headerTextColourId, theme().accent);
    setColour (juce::Label::textColourId, theme().text);
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff23272f));
    setColour (juce::TextButton::buttonOnColourId, theme().accent);
    setColour (juce::TextButton::textColourOffId, theme().text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::Slider::textBoxTextColourId, theme().text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff0d0f13));
    setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xff0d0f13));
    setColour (juce::BubbleComponent::outlineColourId, theme().accent);
    setColour (juce::TooltipWindow::textColourId, theme().text);
    setColour (juce::AlertWindow::backgroundColourId, theme().panel);
    setColour (juce::AlertWindow::textColourId, theme().text);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d0f13));
    setColour (juce::TextEditor::textColourId, theme().text);
    setColour (juce::TextEditor::outlineColourId, theme().edge);
    setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme().accent);
    setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, theme().accent.withAlpha (0.4f));
    setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffe9e6df));
    setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff15171b));
    setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff6b6f77));
    setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
    const float angle = start + pos * (end - start);
    const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);

    // Value ring.
    juce::Path track;
    track.addCentredArc (cx, cy, radius - 2.0f, radius - 2.0f, 0.0f, start, end, true);
    g.setColour (juce::Colour (0xff0a0b0e));
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path value;
    const float from = bipolar ? (start + end) * 0.5f : start;
    value.addCentredArc (cx, cy, radius - 2.0f, radius - 2.0f, 0.0f, std::min (from, angle), std::max (from, angle), true);
    g.setColour (s.isEnabled() ? theme().accent : theme().dim);
    g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knob body: a dark skirt and a brushed metal cap.
    const float body = radius - 7.0f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (cx - body, cy - body + 2.0f, body * 2.0f, body * 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a404b), cx, cy - body, juce::Colour (0xff1a1d22), cx, cy + body, false));
    g.fillEllipse (cx - body, cy - body, body * 2.0f, body * 2.0f);
    const float cap = body * 0.72f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6f7784), cx - cap * 0.5f, cy - cap, theme().knobBody, cx + cap * 0.4f, cy + cap, true));
    g.fillEllipse (cx - cap, cy - cap, cap * 2.0f, cap * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (cx - cap, cy - cap, cap * 2.0f, cap * 2.0f, 1.0f);

    // Pointer.
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -body + 1.0f, 3.0f, body * 0.55f, 1.0f);
    g.setColour (juce::Colour (0xfff4f1ea));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (cx, cy));
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style,
                                     juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0.0f, 0.0f, style, s);
        return;
    }
    const auto r = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y) + h * 0.5f - 3.0f, static_cast<float> (w), 6.0f);
    g.setColour (juce::Colour (0xff0a0b0e));
    g.fillRoundedRectangle (r, 3.0f);
    const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);
    const float centre = bipolar ? r.getCentreX() : r.getX();
    g.setColour (pos >= centre ? theme().accent : theme().accent2);
    g.fillRoundedRectangle (juce::Rectangle<float> (std::min (centre, pos), r.getY(), std::abs (pos - centre), r.getHeight()), 3.0f);
    g.setColour (juce::Colour (0xffe8e4dc));
    g.fillRoundedRectangle (pos - 4.0f, r.getCentreY() - 8.0f, 8.0f, 16.0f, 2.0f);
}

void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const float d = 12.0f;
    const float ledX = b.getButtonText().isEmpty() ? r.getCentreX() - d * 0.5f : r.getX() + 4.0f;
    const auto led = juce::Rectangle<float> (ledX, r.getCentreY() - d * 0.5f, d, d);
    const bool on = b.getToggleState();
    if (on)
    {
        g.setColour (theme().accent.withAlpha (0.35f));
        g.fillEllipse (led.expanded (3.0f));
    }
    g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xffffd08a) : juce::Colour (0xff3a3f48), led.getCentreX(), led.getY(),
                                             on ? theme().accent : juce::Colour (0xff15171b), led.getCentreX(), led.getBottom(), false));
    g.fillEllipse (led);
    g.setColour (juce::Colours::black);
    g.drawEllipse (led, 1.0f);
    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (highlighted ? theme().text : theme().dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (d + 8.0f), juce::Justification::centredLeft);
    }
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setGradientFill (juce::ColourGradient (on ? theme().accent : juce::Colour (0xff2c313a), 0, r.getY(), on ? theme().accent.darker (0.3f) : juce::Colour (0xff1b1e24),
                                             0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (highlighted || down ? theme().accent : theme().edge);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, static_cast<float> (w), static_cast<float> (h)).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (false) ? theme().accent : theme().edge);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = static_cast<float> (w) - 12.0f, ay = static_cast<float> (h) * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (theme().accent);
    g.fillPath (arrow);
}

juce::Font Look::getComboBoxFont (juce::ComboBox&) { return juce::FontOptions (12.5f); }
juce::Font Look::getLabelFont (juce::Label&) { return juce::FontOptions (11.5f); }
juce::Font Look::getTextButtonFont (juce::TextButton&, int) { return juce::FontOptions (13.0f, juce::Font::bold); }

// --- Controls -------------------------------------------------------------------------------------------------------------

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& text, bool bipolar)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.getProperties().set ("bipolar", bipolar);
    if (auto* p = state.getParameter (id))
    {
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.setTooltip (p->getName (64));
    }
    label.setText (text, juce::dontSendNotification);
    styleLabel (label);
    addAndMakeVisible (slider);
    addAndMakeVisible (label);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromBottom (15));
    const int side = std::min (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (side, side));
}

Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& text)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
        box.addItemList (p->choices, 1);
    label.setText (text, juce::dontSendNotification);
    styleLabel (label);
    addAndMakeVisible (box);
    addAndMakeVisible (label);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id, box);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromBottom (15));
    box.setBounds (r.withSizeKeepingCentre (std::min (r.getWidth() - 4, 150), 24));
}

Switch::Switch (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& text)
{
    button.setButtonText (text);
    if (auto* p = state.getParameter (id))
        button.setTooltip (p->getName (64));
    addAndMakeVisible (button);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id, button);
}

// --- Panel -----------------------------------------------------------------------------------------------------------------

Panel::Panel (const juce::String& t, int cols, int displayH) : title (t), columns (cols), displayHeight (displayH) {}

juce::Component* Panel::add (std::unique_ptr<juce::Component> c, int forTab, int span)
{
    itemSpan.push_back (std::clamp (span, 1, columns));
    auto* raw = c.get();
    addChildComponent (raw);
    raw->setVisible (forTab == tab);
    owned.push_back (std::move (c));
    itemTab.push_back (forTab);
    return raw;
}

void Panel::setPower (std::unique_ptr<Switch> s)
{
    power = std::move (s);
    addAndMakeVisible (*power);
}

void Panel::setTabs (const juce::StringArray& names)
{
    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = tabButtons.add (new juce::TextButton (names[i]));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { showTab (i); };
        addAndMakeVisible (b);
    }
    showTab (0);
}

void Panel::showTab (int t)
{
    tab = t;
    for (size_t i = 0; i < owned.size(); ++i)
        owned[i]->setVisible (itemTab[i] == tab);
    for (int i = 0; i < tabButtons.size(); ++i)
        tabButtons[i]->setToggleState (i == tab, juce::dontSendNotification);
    if (onTab)
        onTab (tab);
    resized();
    repaint();
}

void Panel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setGradientFill (juce::ColourGradient (theme().panel.brighter (0.05f), 0, r.getY(), theme().panel.darker (0.25f), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (theme().edge);
    g.drawRoundedRectangle (r, 7.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawHorizontalLine (26, r.getX() + 8.0f, r.getRight() - 8.0f);
    g.setColour (theme().accent);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText (title.toUpperCase(), r.withHeight (26.0f).withTrimmedLeft (power != nullptr ? 34.0f : 14.0f), juce::Justification::centredLeft);
    for (const auto& p : { juce::Point<float> (r.getX() + 8.0f, r.getBottom() - 8.0f), juce::Point<float> (r.getRight() - 8.0f, r.getBottom() - 8.0f) })
        drawScrew (g, p.x, p.y);
}

void Panel::resized()
{
    auto r = getLocalBounds().reduced (8, 4);
    auto header = r.removeFromTop (22);
    if (power != nullptr)
        power->setBounds (header.removeFromLeft (22).withX (6));
    for (int i = tabButtons.size(); --i >= 0;)
    {
        tabButtons[i]->setBounds (header.removeFromRight (std::max (30, juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (13.0f, juce::Font::bold),
                                                                                                                    tabButtons[i]->getButtonText()) + 16)).reduced (1, 1));
        header.removeFromRight (2);
    }
    r.removeFromTop (4);
    if (display != nullptr)
    {
        display->setBounds (r.removeFromTop (displayHeight));
        r.removeFromTop (4);
    }
    r.removeFromBottom (8);
    // Flow the visible items into rows; an item that does not fit in what is left of a row starts the next one.
    std::vector<std::pair<juce::Component*, juce::Point<int>>> placed; // item, (column, row)
    int col = 0, row = 0;
    for (size_t i = 0; i < owned.size(); ++i)
    {
        if (itemTab[i] != tab)
            continue;
        if (col + itemSpan[i] > columns)
        {
            col = 0;
            ++row;
        }
        placed.push_back ({ owned[i].get(), { col, row } });
        col += itemSpan[i];
    }
    if (placed.empty())
        return;
    const int rows = row + 1;
    const int cellW = r.getWidth() / columns, cellH = std::min (78, r.getHeight() / rows);
    for (size_t i = 0, k = 0; i < owned.size(); ++i)
    {
        if (itemTab[i] != tab)
            continue;
        const auto [c, at] = placed[k++];
        c->setBounds (r.getX() + at.x * cellW, r.getY() + at.y * cellH, cellW * itemSpan[i], cellH);
    }
}

} // namespace hwui
