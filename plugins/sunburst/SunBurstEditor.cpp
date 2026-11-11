#include "sunburst/SunBurstEditor.h"

#include "sunburst/Presets.h"
#include "sunburst/Wavetables.h"

namespace sunburst
{
namespace
{
const juce::Colour kBackground (0xff121418), kPanel (0xff1d2128), kPanelEdge (0xff313844), kAccent (0xfff59a2f), kAccent2 (0xff41c3cc),
    kText (0xffdfe4ec), kDim (0xff8b94a3), kKnobBody (0xff2b3039);

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

} // namespace

// --- Editor ------------------------------------------------------------------------------------------------------------------

SunBurstEditor::SunBurstEditor (SunBurstProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p), keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard),
      presetBar (p, presetCategories(), look)
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
    content.addAndMakeVisible (presetBar);
    presetBar.setBounds (280, 16, 464, 32);
    for (int m = 0; m < kNumMacros; ++m)
    {
        auto* k = own (knob (macroId (m), "MACRO " + juce::String (m + 1)));
        content.addAndMakeVisible (k);
        k->setBounds (770 + m * 64, 2, 60, 60);
    }
    auto* masterKnob = own (knob ("master", "MASTER"));
    content.addAndMakeVisible (masterKnob);
    masterKnob->setBounds (1040, 2, 60, 60);
    content.addAndMakeVisible (meter);
    meter.setBounds (1110, 24, 118, 18);

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
            auto* row = own (std::make_unique<hwui::MatrixRow> (s, slot, matrixId (slot, "src"), matrixId (slot, "dst"), matrixId (slot, "amt"), sourceNames(),
                                                         destNames(), hwui::MatrixRow::Style {}));
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
    meter.setLevel (processor.consumePeak());
    presetBar.poll();
    if (page == 0)
        pages[0].repaint(); // the wave, envelope and LFO displays follow their knobs
}

} // namespace sunburst
