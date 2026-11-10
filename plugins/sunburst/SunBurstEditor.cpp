#include "sunburst/SunBurstEditor.h"

#include "sunburst/Presets.h"
#include "sunburst/Wavetables.h"

namespace sunburst
{
namespace
{
const juce::Colour kBackground (0xff121418), kPanel (0xff1d2128), kPanelEdge (0xff313844), kAccent (0xfff59a2f), kAccent2 (0xff41c3cc),
    kText (0xffdfe4ec), kDim (0xff8b94a3), kKnobBody (0xff2b3039);

void drawScrew (juce::Graphics& g, float cx, float cy)
{
    g.setColour (juce::Colour (0xff0b0c0f));
    g.fillEllipse (cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
    g.setColour (juce::Colour (0xff4a515d));
    g.fillEllipse (cx - 3.0f, cy - 3.0f, 6.0f, 6.0f);
    g.setColour (juce::Colour (0xff1a1d22));
    g.drawLine (cx - 2.2f, cy - 2.2f, cx + 2.2f, cy + 2.2f, 1.0f);
}

float paramValue (juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    auto* v = s.getRawParameterValue (id);
    return v != nullptr ? v->load() : 0.0f;
}

// The oscillator's wave as it is set: the wavetable frame at its position, or the basic shape.
class WaveDisplay : public juce::Component
{
public:
    WaveDisplay (juce::AudioProcessorValueTreeState& s, int oscIndex) : state (s), osc (oscIndex) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (kPanelEdge.withAlpha (0.6f));
        g.drawHorizontalLine (static_cast<int> (r.getCentreY()), r.getX(), r.getRight());
        const bool on = paramValue (state, oscId (osc, "on")) > 0.5f;
        const auto wave = static_cast<Wave> (static_cast<int> (paramValue (state, oscId (osc, "wave")) + 0.5f));
        const int table = static_cast<int> (paramValue (state, oscId (osc, "table")) + 0.5f);
        const float position = paramValue (state, oscId (osc, "pos")), width = paramValue (state, oscId (osc, "pw"));
        juce::Path p;
        const int points = 240;
        juce::Random rnd (7);
        for (int i = 0; i <= points; ++i)
        {
            const double ph = static_cast<double> (i) / points;
            float v = 0.0f;
            switch (wave)
            {
                case Wave::Sine: v = std::sin (static_cast<float> (kTwoPi * ph)); break;
                case Wave::Triangle: v = ph < 0.25 ? static_cast<float> (4 * ph) : ph < 0.75 ? static_cast<float> (2 - 4 * ph) : static_cast<float> (4 * ph - 4); break;
                case Wave::Saw: v = static_cast<float> (2 * ph - 1); break;
                case Wave::Square: v = ph < width ? 1.0f : -1.0f; break;
                case Wave::Noise: v = rnd.nextFloat() * 2.0f - 1.0f; break;
                case Wave::Wavetable: v = Wavetables::get().sample (table, position, std::fmod (ph, 1.0), 0); break;
            }
            const float x = r.getX() + r.getWidth() * static_cast<float> (ph);
            const float y = r.getCentreY() - v * r.getHeight() * 0.42f;
            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        g.setColour ((on ? kAccent : kDim).withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (5.0f));
        g.setColour (on ? kAccent : kDim);
        g.strokePath (p, juce::PathStrokeType (1.6f));
        g.setColour (kDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (wave == Wave::Wavetable ? wavetableNames()[table] : waveNames()[static_cast<int> (wave)], r.reduced (6.0f, 3.0f), juce::Justification::topRight);
    }

private:
    juce::AudioProcessorValueTreeState& state;
    int osc;
};

// The envelope shown on the envelope panel's tab.
class EnvDisplay : public juce::Component
{
public:
    EnvDisplay (juce::AudioProcessorValueTreeState& s, const Panel& p) : state (s), panel (p) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRoundedRectangle (r, 4.0f);
        const int e = panel.currentTab();
        const float a = paramValue (state, envId (e, "a")), d = paramValue (state, envId (e, "d")), s = paramValue (state, envId (e, "s")),
                    rel = paramValue (state, envId (e, "r"));
        const float total = std::max (0.5f, a + d + 0.6f + rel);
        const auto px = [&] (float t) { return r.getX() + 6.0f + (r.getWidth() - 12.0f) * t / total; };
        const auto py = [&] (float v) { return r.getBottom() - 5.0f - (r.getHeight() - 10.0f) * v; };
        juce::Path p;
        p.startNewSubPath (px (0), py (0));
        p.lineTo (px (a), py (1));
        p.quadraticTo (px (a + d * 0.3f), py (s), px (a + d), py (s));
        p.lineTo (px (a + d + 0.6f), py (s));
        p.quadraticTo (px (a + d + 0.6f + rel * 0.3f), py (0), px (total), py (0));
        g.setColour (kAccent2.withAlpha (0.18f));
        juce::Path fill (p);
        fill.lineTo (px (0), py (0));
        g.fillPath (fill);
        g.setColour (kAccent2);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }

private:
    juce::AudioProcessorValueTreeState& state;
    const Panel& panel;
};

class LfoDisplay : public juce::Component
{
public:
    LfoDisplay (juce::AudioProcessorValueTreeState& s, const Panel& p) : state (s), panel (p) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRoundedRectangle (r, 4.0f);
        const auto shape = static_cast<LfoShape> (static_cast<int> (paramValue (state, lfoId (panel.currentTab(), "shape")) + 0.5f));
        Lfo lfo;
        lfo.reset (0.0f, 99u);
        juce::Path p;
        const int points = 200;
        for (int i = 0; i <= points; ++i)
        {
            const float v = i == 0 ? lfo.value (shape) : lfo.advance (2.0, 1.0 / points, shape);
            const float x = r.getX() + r.getWidth() * static_cast<float> (i) / points;
            const float y = r.getCentreY() - v * r.getHeight() * 0.4f;
            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        g.setColour (kAccent2);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }

private:
    juce::AudioProcessorValueTreeState& state;
    const Panel& panel;
};

class Meter : public juce::Component
{
public:
    void setLevel (float peak)
    {
        level = std::max (peak, level * 0.85f);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRoundedRectangle (r, 3.0f);
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float fill = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        juce::ColourGradient grad (kAccent2, r.getX(), 0, juce::Colour (0xffe8442e), r.getRight(), 0, false);
        grad.addColour (0.75, kAccent);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r.reduced (2.0f).withWidth ((r.getWidth() - 4.0f) * fill), 2.0f);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("OUT", r.reduced (4.0f, 0.0f), juce::Justification::centredRight);
    }

private:
    float level = 0.0f;
};

// One row of the modulation matrix: source -> destination, amount.
class MatrixRow : public juce::Component
{
public:
    MatrixRow (juce::AudioProcessorValueTreeState& s, int slot)
    {
        number.setText (juce::String (slot + 1), juce::dontSendNotification);
        number.setJustificationType (juce::Justification::centred);
        number.setColour (juce::Label::textColourId, kAccent);
        source.addItemList (sourceNames(), 1);
        dest.addItemList (destNames(), 1);
        amount.setSliderStyle (juce::Slider::LinearHorizontal);
        amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        amount.getProperties().set ("bipolar", true);
        amount.setDoubleClickReturnValue (true, 0.0);
        for (auto* c : std::initializer_list<juce::Component*> { &number, &source, &arrow, &dest, &amount })
            addAndMakeVisible (c);
        arrow.setText (juce::String::fromUTF8 ("\xe2\x86\x92"), juce::dontSendNotification);
        arrow.setJustificationType (juce::Justification::centred);
        sa = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, matrixId (slot, "src"), source);
        da = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, matrixId (slot, "dst"), dest);
        aa = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, matrixId (slot, "amt"), amount);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 0);
        const auto centred = [&r] (int w) { return r.removeFromLeft (w).withSizeKeepingCentre (w, 26); };
        number.setBounds (centred (26));
        source.setBounds (centred (150));
        arrow.setBounds (centred (22));
        dest.setBounds (centred (170));
        r.removeFromLeft (8);
        amount.setBounds (r.withSizeKeepingCentre (r.getWidth(), 28));
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff171a20));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
    }

private:
    juce::Label number, arrow;
    juce::ComboBox source, dest;
    juce::Slider amount;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sa, da;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aa;
};

class Logo : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float cx = r.getX() + 26.0f, cy = r.getCentreY(), radius = 11.0f;
        g.setColour (kAccent);
        for (int i = 0; i < 12; ++i)
        {
            const float a = static_cast<float> (i) * juce::MathConstants<float>::twoPi / 12.0f;
            g.drawLine (cx + std::cos (a) * (radius + 3.0f), cy + std::sin (a) * (radius + 3.0f), cx + std::cos (a) * (radius + 9.0f),
                        cy + std::sin (a) * (radius + 9.0f), 2.0f);
        }
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffd36b), cx, cy - radius, kAccent, cx, cy + radius, false));
        g.fillEllipse (cx - radius, cy - radius, 2 * radius, 2 * radius);
        g.setColour (kText);
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText ("SUNBURST", r.withTrimmedLeft (56.0f), juce::Justification::centredLeft);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText ("POLYPHONIC SYNTHESIZER", r.withTrimmedLeft (58.0f).withTrimmedTop (36.0f), juce::Justification::topLeft);
    }
};

void styleLabel (juce::Label& l)
{
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, kDim);
    l.setInterceptsMouseClicks (false, false);
}
} // namespace

// --- Look and feel ------------------------------------------------------------------------------------------------------

SunBurstLook::SunBurstLook()
{
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0d0f13));
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::outlineColourId, kPanelEdge);
    setColour (juce::ComboBox::arrowColourId, kAccent);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1a1d23));
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::PopupMenu::headerTextColourId, kAccent);
    setColour (juce::Label::textColourId, kText);
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff23272f));
    setColour (juce::TextButton::buttonOnColourId, kAccent);
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff0d0f13));
    setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xff0d0f13));
    setColour (juce::BubbleComponent::outlineColourId, kAccent);
    setColour (juce::TooltipWindow::textColourId, kText);
    setColour (juce::AlertWindow::backgroundColourId, kPanel);
    setColour (juce::AlertWindow::textColourId, kText);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d0f13));
    setColour (juce::TextEditor::textColourId, kText);
    setColour (juce::TextEditor::outlineColourId, kPanelEdge);
    setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, kAccent);
    setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, kAccent.withAlpha (0.4f));
    setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffe9e6df));
    setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff15171b));
    setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff6b6f77));
    setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
}

void SunBurstLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
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
    g.setColour (s.isEnabled() ? kAccent : kDim);
    g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knob body: a dark skirt and a brushed metal cap.
    const float body = radius - 7.0f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (cx - body, cy - body + 2.0f, body * 2.0f, body * 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a404b), cx, cy - body, juce::Colour (0xff1a1d22), cx, cy + body, false));
    g.fillEllipse (cx - body, cy - body, body * 2.0f, body * 2.0f);
    const float cap = body * 0.72f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6f7784), cx - cap * 0.5f, cy - cap, kKnobBody, cx + cap * 0.4f, cy + cap, true));
    g.fillEllipse (cx - cap, cy - cap, cap * 2.0f, cap * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (cx - cap, cy - cap, cap * 2.0f, cap * 2.0f, 1.0f);

    // Pointer.
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -body + 1.0f, 3.0f, body * 0.55f, 1.0f);
    g.setColour (juce::Colour (0xfff4f1ea));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (cx, cy));
}

void SunBurstLook::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style,
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
    g.setColour (pos >= centre ? kAccent : kAccent2);
    g.fillRoundedRectangle (juce::Rectangle<float> (std::min (centre, pos), r.getY(), std::abs (pos - centre), r.getHeight()), 3.0f);
    g.setColour (juce::Colour (0xffe8e4dc));
    g.fillRoundedRectangle (pos - 4.0f, r.getCentreY() - 8.0f, 8.0f, 16.0f, 2.0f);
}

void SunBurstLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const float d = 12.0f;
    const float ledX = b.getButtonText().isEmpty() ? r.getCentreX() - d * 0.5f : r.getX() + 4.0f;
    const auto led = juce::Rectangle<float> (ledX, r.getCentreY() - d * 0.5f, d, d);
    const bool on = b.getToggleState();
    if (on)
    {
        g.setColour (kAccent.withAlpha (0.35f));
        g.fillEllipse (led.expanded (3.0f));
    }
    g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xffffd08a) : juce::Colour (0xff3a3f48), led.getCentreX(), led.getY(),
                                             on ? kAccent : juce::Colour (0xff15171b), led.getCentreX(), led.getBottom(), false));
    g.fillEllipse (led);
    g.setColour (juce::Colours::black);
    g.drawEllipse (led, 1.0f);
    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (highlighted ? kText : kDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (d + 8.0f), juce::Justification::centredLeft);
    }
}

void SunBurstLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setGradientFill (juce::ColourGradient (on ? kAccent : juce::Colour (0xff2c313a), 0, r.getY(), on ? kAccent.darker (0.3f) : juce::Colour (0xff1b1e24),
                                             0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (highlighted || down ? kAccent : kPanelEdge);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void SunBurstLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, static_cast<float> (w), static_cast<float> (h)).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (false) ? kAccent : kPanelEdge);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = static_cast<float> (w) - 12.0f, ay = static_cast<float> (h) * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (kAccent);
    g.fillPath (arrow);
}

juce::Font SunBurstLook::getComboBoxFont (juce::ComboBox&) { return juce::FontOptions (12.5f); }
juce::Font SunBurstLook::getLabelFont (juce::Label&) { return juce::FontOptions (11.5f); }
juce::Font SunBurstLook::getTextButtonFont (juce::TextButton&, int) { return juce::FontOptions (13.0f, juce::Font::bold); }

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
    g.setGradientFill (juce::ColourGradient (kPanel.brighter (0.05f), 0, r.getY(), kPanel.darker (0.25f), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (kPanelEdge);
    g.drawRoundedRectangle (r, 7.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawHorizontalLine (26, r.getX() + 8.0f, r.getRight() - 8.0f);
    g.setColour (kAccent);
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

// --- Editor ------------------------------------------------------------------------------------------------------------------

SunBurstEditor::SunBurstEditor (SunBurstProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p), keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&look);
    auto& s = processor.state;
    addAndMakeVisible (content);

    const auto knob = [&s] (const juce::String& id, const juce::String& text, bool bipolar = false) { return std::make_unique<Knob> (s, id, text, bipolar); };
    const auto choice = [&s] (const juce::String& id, const juce::String& text) { return std::make_unique<Choice> (s, id, text); };
    const auto toggle = [&s] (const juce::String& id, const juce::String& text) { return std::make_unique<Switch> (s, id, text); };
    const auto panel = [this] (juce::Component& parent, const juce::String& title, int columns, int displayHeight = 0)
    {
        auto pnl = std::make_unique<Panel> (title, columns, displayHeight);
        auto* raw = pnl.get();
        parent.addAndMakeVisible (raw);
        owned.push_back (std::move (pnl));
        return raw;
    };
    const auto own = [this] (std::unique_ptr<juce::Component> c) { auto* raw = c.get(); owned.push_back (std::move (c)); return raw; };

    // Header.
    auto* logo = own (std::make_unique<Logo>());
    content.addAndMakeVisible (logo);
    logo->setBounds (12, 6, 250, 52);
    for (auto* b : { &prevButton, &presetButton, &nextButton, &saveButton })
        content.addAndMakeVisible (b);
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    presetButton.onClick = [this] { showPresetMenu(); };
    saveButton.onClick = [this] { savePresetDialog(); };
    prevButton.setBounds (280, 16, 30, 32);
    presetButton.setBounds (314, 16, 330, 32);
    nextButton.setBounds (648, 16, 30, 32);
    saveButton.setBounds (684, 16, 60, 32);
    for (int m = 0; m < kNumMacros; ++m)
    {
        auto* k = own (knob (macroId (m), "MACRO " + juce::String (m + 1)));
        content.addAndMakeVisible (k);
        k->setBounds (770 + m * 64, 2, 60, 60);
    }
    auto* masterKnob = own (knob ("master", "MASTER"));
    content.addAndMakeVisible (masterKnob);
    masterKnob->setBounds (1040, 2, 60, 60);
    meter = std::make_unique<Meter>();
    content.addAndMakeVisible (*meter);
    meter->setBounds (1110, 24, 118, 18);

    const juce::StringArray pageNames { "SYNTH", "MOD MATRIX", "EFFECTS & ARP" };
    for (int i = 0; i < pageNames.size(); ++i)
    {
        auto* b = pageButtons.add (new juce::TextButton (pageNames[i]));
        b->onClick = [this, i] { showPage (i); };
        content.addAndMakeVisible (b);
        b->setBounds (12 + i * 150, 68, 144, 26);
    }
    for (auto& pg : pages)
    {
        content.addChildComponent (pg);
        pg.setBounds (10, 100, kWidth - 20, 612);
    }

    // --- Synth page ---
    auto& synth = pages[0];
    for (int o = 0; o < kNumOscs; ++o)
    {
        auto* pnl = panel (synth, "Oscillator " + juce::String (o + 1), 6, 64);
        pnl->setPower (std::make_unique<Switch> (s, oscId (o, "on"), ""));
        pnl->setDisplay (own (std::make_unique<WaveDisplay> (s, o)));
        pnl->add (choice (oscId (o, "wave"), "WAVE"), 0, 2);
        pnl->add (choice (oscId (o, "table"), "WAVETABLE"), 0, 2);
        pnl->add (knob (oscId (o, "oct"), "OCTAVE", true));
        pnl->add (knob (oscId (o, "semi"), "SEMI", true));
        pnl->add (knob (oscId (o, "fine"), "FINE", true));
        pnl->add (knob (oscId (o, "level"), "LEVEL"));
        pnl->add (knob (oscId (o, "pan"), "PAN", true));
        pnl->add (knob (oscId (o, "pw"), "WIDTH"));
        pnl->add (knob (oscId (o, "pos"), "WT POS"));
        pnl->add (knob (oscId (o, "voices"), "UNISON"));
        pnl->add (knob (oscId (o, "detune"), "DETUNE"));
        pnl->add (knob (oscId (o, "blend"), "BLEND"));
        pnl->add (knob (oscId (o, "spread"), "SPREAD"));
        pnl->add (toggle (oscId (o, "rphase"), "Random phase"), 0, 2);
        pnl->setBounds (o * 408, 0, 400, 298);
    }
    {
        auto* pnl = panel (synth, "Sub & noise", 2);
        pnl->add (knob ("sub_level", "SUB"));
        pnl->add (choice ("sub_wave", "SUB WAVE"));
        pnl->add (choice ("sub_oct", "SUB OCT"));
        pnl->add (knob ("noise_level", "NOISE"));
        pnl->add (knob ("noise_colour", "COLOUR"));
        pnl->setBounds (0, 306, 150, 306);
    }
    {
        auto* pnl = panel (synth, "Filter", 3);
        pnl->setPower (std::make_unique<Switch> (s, "flt_on", ""));
        pnl->add (choice ("flt_type", "TYPE"), 0, 2);
        pnl->add (knob ("flt_cut", "CUTOFF"));
        pnl->add (knob ("flt_res", "RESO"));
        pnl->add (knob ("flt_drive", "DRIVE"));
        pnl->add (knob ("flt_env", "ENV AMT", true));
        pnl->add (knob ("flt_key", "KEY"));
        pnl->add (knob ("flt_vel", "VELOCITY"));
        pnl->setBounds (158, 306, 270, 306);
    }
    {
        auto* pnl = panel (synth, "Envelopes", 4, 70);
        auto* display = own (std::make_unique<EnvDisplay> (s, *pnl));
        pnl->setDisplay (display);
        for (int e = 0; e < kNumEnvs; ++e)
        {
            pnl->add (knob (envId (e, "a"), "ATTACK"), e);
            pnl->add (knob (envId (e, "d"), "DECAY"), e);
            pnl->add (knob (envId (e, "s"), "SUSTAIN"), e);
            pnl->add (knob (envId (e, "r"), "RELEASE"), e);
        }
        pnl->add (knob ("amp_vel", "VELOCITY"), 0);
        pnl->add (knob ("pitch_env", "TO PITCH", true), 2);
        pnl->setTabs ({ "AMP", "FILTER", "MOD" });
        pnl->onTab = [display] (int) { display->repaint(); };
        pnl->setBounds (436, 306, 300, 306);
    }
    {
        auto* pnl = panel (synth, "LFO", 3, 56);
        auto* display = own (std::make_unique<LfoDisplay> (s, *pnl));
        pnl->setDisplay (display);
        for (int l = 0; l < kNumLfos; ++l)
        {
            pnl->add (choice (lfoId (l, "shape"), "SHAPE"), l);
            pnl->add (knob (lfoId (l, "rate"), "RATE"), l);
            pnl->add (choice (lfoId (l, "div"), "DIVISION"), l);
            pnl->add (toggle (lfoId (l, "sync"), "Sync"), l);
            pnl->add (knob (lfoId (l, "fade"), "FADE IN"), l);
            pnl->add (toggle (lfoId (l, "retrig"), "Retrigger"), l);
            pnl->add (toggle (lfoId (l, "uni"), "Unipolar"), l);
        }
        pnl->setTabs ({ "1", "2", "3" });
        pnl->onTab = [display] (int) { display->repaint(); };
        pnl->setBounds (744, 306, 290, 306);
    }
    {
        auto* pnl = panel (synth, "Voice", 2);
        pnl->add (choice ("mode", "MODE"));
        pnl->add (knob ("voices", "POLYPHONY"));
        pnl->add (knob ("glide", "GLIDE"));
        pnl->add (knob ("bend", "BEND"));
        pnl->setBounds (1042, 306, 178, 306);
    }

    // --- Modulation matrix page ---
    auto& matrix = pages[1];
    {
        auto* pnl = panel (matrix, "Modulation matrix  -  source, destination, amount", 1);
        pnl->setBounds (0, 0, kWidth - 20, 548);
        for (int slot = 0; slot < kMatrixSlots; ++slot)
        {
            auto* row = own (std::make_unique<MatrixRow> (s, slot));
            pnl->addAndMakeVisible (row);
            row->setBounds (12 + (slot / 8) * 604, 36 + (slot % 8) * 62, 596, 56);
        }
        auto* hint = own (std::make_unique<juce::Label>());
        auto* label = static_cast<juce::Label*> (hint);
        label->setText ("LFOs, envelopes 2 and 3, velocity, key, the mod wheel, aftertouch, pitch bend, a random value per note and the four macros can move "
                        "any destination. Effects follow the free-running LFOs, the wheel, aftertouch, bend and the macros. Factory presets: wheel = brightness, "
                        "Macro 1 = cutoff, 2 = reverb, 3 = unison width, 4 = filter drive.",
                        juce::dontSendNotification);
        label->setColour (juce::Label::textColourId, kDim);
        label->setJustificationType (juce::Justification::topLeft);
        matrix.addAndMakeVisible (label);
        label->setBounds (14, 556, kWidth - 50, 52);
    }

    // --- Effects and arpeggiator page ---
    auto& effects = pages[2];
    {
        auto* pnl = panel (effects, "Distortion", 3);
        pnl->setPower (std::make_unique<Switch> (s, "dist_on", ""));
        pnl->add (choice ("dist_type", "TYPE"));
        pnl->add (knob ("dist_drive", "DRIVE"));
        pnl->add (knob ("dist_mix", "MIX"));
        pnl->setBounds (0, 0, 330, 196);
    }
    {
        auto* pnl = panel (effects, "Equalizer", 4);
        pnl->setPower (std::make_unique<Switch> (s, "eq_on", ""));
        pnl->add (knob ("eq_low", "LOW", true));
        pnl->add (knob ("eq_mid", "MID", true));
        pnl->add (knob ("eq_midf", "MID FREQ"));
        pnl->add (knob ("eq_high", "HIGH", true));
        pnl->setBounds (338, 0, 440, 196);
    }
    {
        auto* pnl = panel (effects, "Filter", 3);
        pnl->setPower (std::make_unique<Switch> (s, "ff_on", ""));
        pnl->add (choice ("ff_type", "TYPE"));
        pnl->add (knob ("ff_cut", "CUTOFF"));
        pnl->add (knob ("ff_res", "RESO"));
        pnl->setBounds (786, 0, 434, 196);
    }
    {
        auto* pnl = panel (effects, "Chorus", 3);
        pnl->setPower (std::make_unique<Switch> (s, "cho_on", ""));
        pnl->add (knob ("cho_rate", "RATE"));
        pnl->add (knob ("cho_depth", "DEPTH"));
        pnl->add (knob ("cho_mix", "MIX"));
        pnl->setBounds (0, 204, 330, 220);
    }
    {
        auto* pnl = panel (effects, "Delay", 4);
        pnl->setPower (std::make_unique<Switch> (s, "dly_on", ""));
        pnl->add (toggle ("dly_sync", "Tempo sync"));
        pnl->add (choice ("dly_div", "DIVISION"));
        pnl->add (knob ("dly_time", "TIME"));
        pnl->add (knob ("dly_fb", "FEEDBACK"));
        pnl->add (toggle ("dly_pp", "Ping-pong"));
        pnl->add (knob ("dly_tone", "TONE"));
        pnl->add (knob ("dly_mix", "MIX"));
        pnl->setBounds (338, 204, 440, 220);
    }
    {
        auto* pnl = panel (effects, "Reverb", 3);
        pnl->setPower (std::make_unique<Switch> (s, "rev_on", ""));
        pnl->add (knob ("rev_size", "SIZE"));
        pnl->add (knob ("rev_damp", "DAMPING"));
        pnl->add (knob ("rev_width", "WIDTH"));
        pnl->add (knob ("rev_pre", "PRE-DELAY"));
        pnl->add (knob ("rev_mix", "MIX"));
        pnl->setBounds (786, 204, 434, 220);
    }
    {
        auto* pnl = panel (effects, "Arpeggiator", 4);
        pnl->setPower (std::make_unique<Switch> (s, "arp_on", ""));
        pnl->add (choice ("arp_mode", "MODE"));
        pnl->add (choice ("arp_rate", "RATE"));
        pnl->add (knob ("arp_oct", "OCTAVES"));
        pnl->add (knob ("arp_gate", "GATE"));
        pnl->setBounds (0, 432, 560, 180);
    }

    keyboard.setAvailableRange (24, 108);
    keyboard.setKeyWidth (static_cast<float> (kWidth - 20) / 50.0f); // the 50 white keys from C1 to C8 fill the width
    keyboard.setLowestVisibleKey (24);
    content.addAndMakeVisible (keyboard);
    keyboard.setBounds (10, 718, kWidth - 20, 74);

    showPage (0);
    refreshPresetLabel();
    content.setSize (kWidth, kHeight);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (kWidth) / kHeight);
    setResizeLimits (kWidth * 6 / 10, kHeight * 6 / 10, kWidth * 2, kHeight * 2);
    setSize (kWidth, kHeight);
    startTimerHz (30);
}

SunBurstEditor::~SunBurstEditor()
{
    setLookAndFeel (nullptr);
}

void SunBurstEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    const float scale = static_cast<float> (getWidth()) / kWidth;
    g.addTransform (juce::AffineTransform::scale (scale));
    // A brushed strip behind the header.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff23272f), 0, 0, juce::Colour (0xff15181d), 0, 62, false));
    g.fillRect (0, 0, kWidth, 62);
    g.setColour (kAccent.withAlpha (0.6f));
    g.fillRect (0, 62, kWidth, 2);
}

void SunBurstEditor::resized()
{
    content.setBounds (0, 0, kWidth, kHeight);
    content.setTransform (juce::AffineTransform::scale (static_cast<float> (getWidth()) / kWidth));
}

void SunBurstEditor::showPage (int index)
{
    page = index;
    for (size_t i = 0; i < pages.size(); ++i)
        pages[i].setVisible (static_cast<int> (i) == page);
    for (int i = 0; i < pageButtons.size(); ++i)
        pageButtons[i]->setToggleState (i == page, juce::dontSendNotification);
}

void SunBurstEditor::timerCallback()
{
    static_cast<Meter*> (meter.get())->setLevel (processor.consumePeak());
    if (processor.presetGeneration() != seenGeneration)
        refreshPresetLabel();
    if (page == 0)
        pages[0].repaint(); // the wave, envelope and LFO displays follow their knobs
}

void SunBurstEditor::refreshPresetLabel()
{
    seenGeneration = processor.presetGeneration();
    presetButton.setButtonText (processor.getPresetCategory().toUpperCase() + "   " + processor.getPresetName());
}

void SunBurstEditor::showPresetMenu()
{
    const auto list = processor.presets();
    juce::PopupMenu menu;
    juce::StringArray categories = presetCategories();
    for (const auto& p : list)
        categories.addIfNotAlreadyThere (p.category);
    for (const auto& category : categories)
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
                        [safe = juce::Component::SafePointer<SunBurstEditor> (this), list] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result > 0 && result <= static_cast<int> (list.size()))
                                safe->processor.loadPreset (list[static_cast<size_t> (result - 1)]);
                            else if (result == -1)
                                safe->savePresetDialog();
                            else if (result == -2)
                                safe->processor.initPatch();
                            else if (result == -3)
                            {
                                SunBurstProcessor::userPresetFolder().createDirectory();
                                SunBurstProcessor::userPresetFolder().startAsProcess();
                            }
                        });
}

void SunBurstEditor::stepPreset (int delta)
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

void SunBurstEditor::savePresetDialog()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "A name and a category for this sound.", juce::MessageBoxIconType::NoIcon, this);
    saveWindow->setLookAndFeel (&look);
    saveWindow->addTextEditor ("name", processor.getPresetName() == "Init" ? juce::String ("My sound") : processor.getPresetName(), "Name");
    juce::StringArray categories = presetCategories();
    categories.add ("User");
    saveWindow->addComboBox ("category", categories, "Category");
    if (auto* box = saveWindow->getComboBoxComponent ("category"))
        box->setSelectedItemIndex (std::max (0, categories.indexOf (processor.getPresetCategory())), juce::dontSendNotification);
    saveWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<SunBurstEditor> (this)] (int result)
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
        safe->saveWindow.reset();
    }), false);
}
} // namespace sunburst
