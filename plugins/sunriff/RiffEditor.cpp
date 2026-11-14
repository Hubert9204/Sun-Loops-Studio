#include "sunriff/RiffEditor.h"

#include "sunriff/RiffParams.h"
#include "sunriff/RiffPresets.h"

namespace sunriff
{
namespace
{
hwui::Theme riffTheme()
{
    hwui::Theme t;
    t.background = juce::Colour (0xff15110f);
    t.panel = juce::Colour (0xff221b17);
    t.edge = juce::Colour (0xff3d3029);
    t.accent = juce::Colour (0xffe2553b);  // amp red
    t.accent2 = juce::Colour (0xfff0c15a); // brass
    t.text = juce::Colour (0xffeee4dc);
    t.dim = juce::Colour (0xff9c8f86);
    t.knobBody = juce::Colour (0xff2e2621);
    return t;
}
const hwui::Theme& th() { return hwui::theme(); }

// Radio buttons bound to a choice parameter (FL Slayer's string, coil, amp and cabinet selectors).
class Radio : public juce::Component
{
public:
    Radio (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& title, int columns)
        : cols (columns), heading (title)
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id));
        jassert (param != nullptr);
        for (int i = 0; i < param->choices.size(); ++i)
        {
            auto* b = buttons.add (new juce::TextButton (param->choices[i]));
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, th().accent);
            b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            b->onClick = [this, i] { if (attachment != nullptr) attachment->setValueAsCompleteGesture (static_cast<float> (i)); };
            addAndMakeVisible (b);
        }
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
        {
            for (int i = 0; i < buttons.size(); ++i)
                buttons[i]->setToggleState (i == juce::roundToInt (v), juce::dontSendNotification);
        });
        attachment->sendInitialUpdate();
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (th().dim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (heading, 0, 0, getWidth(), 14, juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (heading.isEmpty() ? 0 : 16);
        const int rows = (buttons.size() + cols - 1) / cols;
        const int h = std::min (24, r.getHeight() / std::max (1, rows));
        for (int i = 0; i < buttons.size(); ++i)
        {
            const int c = i % cols, row = i / cols;
            const int w = r.getWidth() / cols;
            buttons[i]->setBounds (r.getX() + c * w + 1, r.getY() + row * (h + 3), w - 2, h);
        }
    }

private:
    int cols;
    juce::String heading;
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// The guitar: a body with six strings and the pickup(s). Dragging up and down moves the pickup between bridge and neck.
class Guitar : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Guitar (juce::AudioProcessorValueTreeState& s) : state (s)
    {
        pickup = std::make_unique<juce::ParameterAttachment> (*s.getParameter (id::pickup), [this] (float) { repaint(); });
        coil = std::make_unique<juce::ParameterAttachment> (*s.getParameter (id::coil), [this] (float) { repaint(); });
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setTooltip ("Drag the pickup up (towards the neck) or down (towards the bridge)");
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (6.0f);
        // the body: two rounded lobes and a waist
        juce::Path body;
        const float cx = r.getCentreX();
        body.addEllipse (cx - r.getWidth() * 0.36f, r.getY() + r.getHeight() * 0.34f, r.getWidth() * 0.72f, r.getHeight() * 0.62f);
        body.addEllipse (cx - r.getWidth() * 0.29f, r.getY() + r.getHeight() * 0.12f, r.getWidth() * 0.58f, r.getHeight() * 0.42f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffb5441f), cx, r.getY() + r.getHeight() * 0.5f, juce::Colour (0xff3a1408),
                                                 cx + r.getWidth() * 0.45f, r.getBottom(), true));
        g.fillPath (body);
        g.setColour (juce::Colour (0xfff2e6d0).withAlpha (0.8f));
        g.strokePath (body, juce::PathStrokeType (2.0f));
        // the neck
        const float neckW = r.getWidth() * 0.16f;
        g.setColour (juce::Colour (0xff4a2e1c));
        g.fillRect (cx - neckW / 2.0f, r.getY() - 6.0f, neckW, r.getHeight() * 0.32f);
        g.setColour (juce::Colour (0xffbfa98a));
        for (int f = 1; f < 5; ++f)
            g.fillRect (cx - neckW / 2.0f, r.getY() - 6.0f + f * r.getHeight() * 0.07f, neckW, 1.5f);
        // the bridge
        const float bridgeY = r.getBottom() - r.getHeight() * 0.14f;
        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillRoundedRectangle (cx - neckW * 0.75f, bridgeY - 4.0f, neckW * 1.5f, 8.0f, 2.0f);
        // the pickups
        const int c = juce::roundToInt (state.getRawParameterValue (id::coil)->load());
        const float pos = state.getRawParameterValue (id::pickup)->load();
        const float y = pickupY (pos);
        if (c > 0)
        {
            const float w = neckW * 1.25f, h = c == 2 ? 18.0f : 10.0f;
            g.setColour (juce::Colour (0xff111111));
            g.fillRoundedRectangle (cx - w / 2.0f, y - h / 2.0f, w, h, 3.0f);
            g.setColour (th().accent2);
            for (int k = 0; k < 6; ++k)
            {
                const float px = cx - neckW * 0.4f + k * neckW * 0.16f;
                g.fillEllipse (px - 2.0f, y - (c == 2 ? 6.0f : 2.0f), 4.0f, 4.0f);
                if (c == 2)
                    g.fillEllipse (px - 2.0f, y + 2.0f, 4.0f, 4.0f);
            }
        }
        // the strings
        g.setColour (juce::Colour (0xffe8e8e8).withAlpha (0.9f));
        for (int k = 0; k < 6; ++k)
        {
            const float sx = cx - neckW * 0.4f + k * neckW * 0.16f;
            g.drawLine (sx, r.getY() - 6.0f, sx, bridgeY, 0.6f + (5 - k) * 0.25f);
        }
        g.setColour (th().text);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (c == 0 ? "NO PICKUP" : juce::String (c == 1 ? "SINGLE COIL" : "DOUBLE COIL") + juce::String (pos < 0.33f ? " - BRIDGE" : pos > 0.66f ? " - NECK" : " - MIDDLE"),
                    getLocalBounds().removeFromBottom (16), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        pickup->beginGesture();
        mouseDrag (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto r = getLocalBounds().toFloat().reduced (6.0f);
        const float bottom = r.getBottom() - r.getHeight() * 0.2f, top = r.getY() + r.getHeight() * 0.36f;
        pickup->setValueAsPartOfGesture (std::clamp ((bottom - static_cast<float> (e.y)) / (bottom - top), 0.0f, 1.0f));
    }
    void mouseUp (const juce::MouseEvent&) override { pickup->endGesture(); }

private:
    float pickupY (float pos) const
    {
        const auto r = getLocalBounds().toFloat().reduced (6.0f);
        const float bottom = r.getBottom() - r.getHeight() * 0.2f, top = r.getY() + r.getHeight() * 0.36f;
        return bottom - pos * (bottom - top);
    }
    juce::AudioProcessorValueTreeState& state;
    std::unique_ptr<juce::ParameterAttachment> pickup, coil;
};

class Logo : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        // a pick
        juce::Path pick;
        const float x = 6.0f, y = 6.0f, w = 36.0f, h = 40.0f;
        pick.startNewSubPath (x + w / 2.0f, y + h);
        pick.cubicTo (x - 4.0f, y + h * 0.45f, x, y, x + w / 2.0f, y);
        pick.cubicTo (x + w, y, x + w + 4.0f, y + h * 0.45f, x + w / 2.0f, y + h);
        pick.closeSubPath();
        g.setGradientFill (juce::ColourGradient (th().accent2, x, y, th().accent, x + w, y + h, false));
        g.fillPath (pick);
        g.setColour (th().text);
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText ("SUNRIFF", r.withTrimmedLeft (54.0f), juce::Justification::centredLeft);
        g.setColour (th().dim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText ("ELECTRIC GUITAR SIMULATION", r.withTrimmedLeft (56.0f).withTrimmedTop (38.0f), juce::Justification::topLeft);
    }
};
} // namespace

RiffEditor::RiffEditor (RiffProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p), look (riffTheme()), keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard),
      presetBar (p, presetCategories(), look)
{
    setLookAndFeel (&look);
    auto& s = processor.state;
    addAndMakeVisible (content);

    const auto knob = [&s] (const juce::String& pid, const juce::String& text, bool bipolar = false) { return std::make_unique<hwui::Knob> (s, pid, text, bipolar); };
    const auto panel = [this] (const juce::String& title, int columns, juce::Rectangle<int> bounds)
    {
        auto pnl = std::make_unique<hwui::Panel> (title, columns);
        auto* raw = pnl.get();
        content.addAndMakeVisible (raw);
        raw->setBounds (bounds);
        owned.push_back (std::move (pnl));
        return raw;
    };
    const auto place = [this] (std::unique_ptr<juce::Component> c, juce::Rectangle<int> bounds)
    {
        auto* raw = c.get();
        content.addAndMakeVisible (raw);
        raw->setBounds (bounds);
        owned.push_back (std::move (c));
        return raw;
    };

    // Header
    place (std::make_unique<Logo>(), { 12, 6, 280, 52 });
    content.addAndMakeVisible (presetBar);
    presetBar.setBounds (310, 16, 470, 32);
    auto* masterKnob = place (knob (id::master, "MASTER"), { 960, 2, 60, 60 });
    juce::ignoreUnused (masterKnob);
    content.addAndMakeVisible (meter);
    meter.setBounds (1036, 24, 130, 18);

    // The guitar (pickup) and its coil
    auto* guitarFrame = panel ("PICKUP", 1, { 10, 70, 250, 380 });
    guitar = place (std::make_unique<Guitar> (s), { 20, 98, 230, 290 });
    place (std::make_unique<Radio> (s, id::coil, "COIL TYPE", 3), { 22, 396, 226, 44 });
    juce::ignoreUnused (guitarFrame);

    // Playing mode
    {
        auto* pnl = panel ("PLAY", 3, { 268, 70, 300, 184 });
        pnl->add (std::make_unique<hwui::Choice> (s, id::mode, "PLAYING MODE"), 0, 3);
        pnl->add (knob (id::speed, "SPEED"));
        pnl->add (std::make_unique<hwui::Switch> (s, id::hold, "HOLD"));
        pnl->add (knob (id::bend2, "PITCH BEND 2", true));
    }
    place (std::make_unique<Radio> (s, id::gliss, "GLISSANDO (COARSE BEND)", 2), { 280, 260, 276, 42 });

    // Strings
    {
        panel ("STRING", 1, { 576, 70, 200, 232 });
        place (std::make_unique<Radio> (s, id::string, "", 1), { 588, 100, 176, 196 });
    }

    // Guitar properties
    {
        auto* pnl = panel ("GUITAR", 7, { 268, 310, 508, 140 });
        pnl->add (knob (id::tone, "TONE"));
        pnl->add (knob (id::slap, "SLAP"));
        pnl->add (knob (id::fret, "FRET"));
        pnl->add (knob (id::harmonic, "HARMONIC"));
        pnl->add (knob (id::vel, "VEL"));
        pnl->add (knob (id::damping, "DAMPING"));
        pnl->add (knob (id::dampVel, "DAMP VEL"));
    }

    // Amp and cabinet
    {
        auto* pnl = panel ("AMP", 6, { 784, 70, 386, 232 });
        place (std::make_unique<Radio> (s, id::amp, "AMP TYPE", 4), { 796, 98, 362, 42 });
        place (std::make_unique<Radio> (s, id::cabinet, "CABINET", 4), { 796, 144, 362, 42 });
        auto* knobs = place (std::make_unique<juce::Component>(), { 792, 190, 370, 104 });
        const char* ids[] { id::drive, id::presence, id::feedback, id::low, id::mid, id::high };
        const char* names[] { "DRIVE", "PRESENCE", "FEEDBACK", "LOW", "MID", "HIGH" };
        for (int i = 0; i < 6; ++i)
        {
            auto k = knob (ids[i], names[i], i == 1 || i >= 3);
            k->setBounds (i * 61, 10, 62, 78);
            knobs->addAndMakeVisible (k.get());
            owned.push_back (std::move (k));
        }
        juce::ignoreUnused (pnl);
    }

    // Effects unit
    {
        auto* pnl = panel ("FX UNIT", 4, { 784, 310, 386, 140 });
        pnl->add (std::make_unique<hwui::Choice> (s, id::fx, "EFFECT"), 0, 2);
        auto k1 = knob (id::fx1, "PARAM 1");
        auto k2 = knob (id::fx2, "PARAM 2");
        fxKnob1 = k1.get();
        fxKnob2 = k2.get();
        pnl->add (std::move (k1));
        pnl->add (std::move (k2));
    }

    keyboard.setAvailableRange (28, 96);
    keyboard.setKeyWidth (static_cast<float> (kWidth - 20) / 40.0f);
    keyboard.setLowestVisibleKey (28);
    content.addAndMakeVisible (keyboard);
    keyboard.setBounds (10, 462, kWidth - 20, 120);

    for (auto& c : owned)
        if (auto* pnl = dynamic_cast<hwui::Panel*> (c.get()))
            pnl->resized();

    content.setSize (kWidth, kHeight);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (kWidth) / static_cast<double> (kHeight));
    setResizeLimits (kWidth * 6 / 10, kHeight * 6 / 10, kWidth * 2, kHeight * 2);
    setSize (kWidth, kHeight);
    startTimerHz (30);
}

RiffEditor::~RiffEditor()
{
    setLookAndFeel (nullptr);
}

void RiffEditor::paint (juce::Graphics& g)
{
    g.fillAll (th().background);
    g.addTransform (juce::AffineTransform::scale (static_cast<float> (getWidth()) / kWidth));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a201b), 0, 0, juce::Colour (0xff15110f), 0, 62, false));
    g.fillRect (0, 0, kWidth, 62);
    g.setGradientFill (juce::ColourGradient (th().accent, 0, 0, th().accent2, static_cast<float> (kWidth), 0, false));
    g.fillRect (0, 62, kWidth, 2);
}

void RiffEditor::resized()
{
    content.setBounds (0, 0, kWidth, kHeight);
    content.setTransform (juce::AffineTransform::scale (static_cast<float> (getWidth()) / kWidth));
}

void RiffEditor::timerCallback()
{
    meter.setLevel (processor.consumePeak());
    presetBar.poll();
    // The effect's two knobs are named after what they do.
    const int type = juce::roundToInt (processor.state.getRawParameterValue (id::fx)->load());
    if (type != shownFx && fxKnob1 != nullptr)
    {
        shownFx = type;
        const auto names = fxParamNames (type);
        fxKnob1->label.setText (names.first.isEmpty() ? "-" : names.first.toUpperCase(), juce::dontSendNotification);
        fxKnob2->label.setText (names.second.isEmpty() ? "-" : names.second.toUpperCase(), juce::dontSendNotification);
        fxKnob1->setEnabled (names.first.isNotEmpty());
        fxKnob2->setEnabled (names.second.isNotEmpty());
    }
}
} // namespace sunriff
