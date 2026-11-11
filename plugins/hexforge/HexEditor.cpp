#include "hexforge/HexEditor.h"

#include "hexforge/HexPresets.h"

namespace hexforge
{
namespace
{
hwui::Theme hexTheme()
{
    hwui::Theme t;
    t.background = juce::Colour (0xff0c0d10);
    t.panel = juce::Colour (0xff17191e);
    t.edge = juce::Colour (0xff2d3139);
    t.accent = juce::Colour (0xffff2e5b);  // red
    t.accent2 = juce::Colour (0xff12d8e0); // cyan
    t.text = juce::Colour (0xffe4e7ee);
    t.dim = juce::Colour (0xff848c9a);
    t.knobBody = juce::Colour (0xff262a31);
    return t;
}

const hwui::Theme& th() { return hwui::theme(); }

float paramValue (juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    auto* v = s.getRawParameterValue (id);
    return v != nullptr ? v->load() : 0.0f;
}

void darkBox (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colour (0xff08090b));
    g.fillRoundedRectangle (r, 4.0f);
}

// One operator's wave (its table at full detail).
class OpDisplay : public juce::Component
{
public:
    OpDisplay (HexProcessor& p, int opIndex) : proc (p), op (opIndex) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        darkBox (g, r);
        const int wave = static_cast<int> (paramValue (proc.state, opId (op, "wave")) + 0.5f);
        const bool heard = paramValue (proc.state, opId (op, "out")) > 0.0f;
        std::array<float, kTableSize + 1> spectralTable {};
        const float* table = nullptr;
        if (wave < kNumClassicWaves)
            table = HexWaves::get().classic (wave);
        else
        {
            const auto& s = proc.getSpectrum (wave - kNumClassicWaves);
            float peak = 1.0e-6f;
            for (int i = 0; i <= kTableSize; i += 8)
            {
                double v = 0.0;
                for (size_t k = 0; k < s.amplitude.size(); ++k)
                    if (s.amplitude[k] > 1.0e-4f)
                        v += s.amplitude[k] * std::sin (6.283185307179586 * (static_cast<double> (k + 1) * i / kTableSize + s.phase[k]));
                spectralTable[static_cast<size_t> (i)] = static_cast<float> (v);
                peak = std::max (peak, std::abs (static_cast<float> (v)));
            }
            for (int i = 0; i <= kTableSize; i += 8)
                spectralTable[static_cast<size_t> (i)] /= peak;
            table = spectralTable.data();
        }
        juce::Path path;
        for (int i = 0; i <= kTableSize; i += 8)
        {
            const float x = r.getX() + r.getWidth() * static_cast<float> (i) / kTableSize;
            const float y = r.getCentreY() - table[i] * r.getHeight() * 0.4f;
            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
        const auto colour = heard ? th().accent : th().accent2;
        g.setColour (colour.withAlpha (0.25f));
        g.strokePath (path, juce::PathStrokeType (4.0f));
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (1.4f));
        g.setColour (th().dim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (heard ? "OUT" : "MOD", r.reduced (4.0f, 2.0f), juce::Justification::topRight);
    }

private:
    HexProcessor& proc;
    int op;
};

// The 6 x 6 FM matrix in one panel: row = the operator modulated, column = the operator modulating it; the diagonal is feedback.
class MatrixGrid : public juce::Component
{
public:
    explicit MatrixGrid (juce::AudioProcessorValueTreeState& s)
    {
        for (int t = 0; t < kNumOps; ++t)
            for (int src = 0; src < kNumOps; ++src)
            {
                auto knob = std::make_unique<hwui::Knob> (s, fmId (t, src), "");
                knob->slider.setTooltip ("Operator " + juce::String (src + 1) + (t == src ? " feedback" : " modulates operator " + juce::String (t + 1)));
                addAndMakeVisible (*knob);
                knobs.push_back (std::move (knob));
            }
    }
    void resized() override
    {
        const int cell = std::min ((getWidth() - 30) / kNumOps, (getHeight() - 26) / kNumOps);
        for (int t = 0; t < kNumOps; ++t)
            for (int src = 0; src < kNumOps; ++src)
                knobs[static_cast<size_t> (t * kNumOps + src)]->setBounds (30 + src * cell, 26 + t * cell, cell, cell + 6);
    }
    void paint (juce::Graphics& g) override
    {
        const int cell = std::min ((getWidth() - 30) / kNumOps, (getHeight() - 26) / kNumOps);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        for (int i = 0; i < kNumOps; ++i)
        {
            g.setColour (th().accent2);
            g.drawText (juce::String (i + 1), 30 + i * cell, 2, cell, 20, juce::Justification::centred);
            g.setColour (th().accent);
            g.drawText (juce::String (i + 1), 0, 26 + i * cell, 26, cell, juce::Justification::centred);
            g.setColour (th().accent2.withAlpha (0.08f));
            g.fillRoundedRectangle (static_cast<float> (30 + i * cell + 2), static_cast<float> (26 + i * cell + 2), static_cast<float> (cell - 4),
                                    static_cast<float> (cell - 4), 6.0f); // the diagonal: feedback
        }
        g.setColour (th().dim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("FROM", 0, 2, 30, 20, juce::Justification::centred);
    }

private:
    std::vector<std::unique_ptr<hwui::Knob>> knobs;
};

// The spectral wave editor: 64 harmonics drawn with the mouse, generated or read from a WAV.
class SpectralEditor : public juce::Component
{
public:
    explicit SpectralEditor (HexProcessor& p) : proc (p)
    {
        for (int s = 0; s < 2; ++s)
        {
            auto* b = slotButtons.add (new juce::TextButton (s == 0 ? "SPECTRAL A" : "SPECTRAL B"));
            b->onClick = [this, s] { slot = s; refreshButtons(); repaint(); };
            addAndMakeVisible (b);
        }
        for (const char* name : { "Saw", "Square", "Odd", "Bright", "Formant", "Metal", "Random", "Clear" })
        {
            auto* b = generators.add (new juce::TextButton (name));
            b->onClick = [this, n = juce::String (name)] { proc.setSpectrum (slot, Spectrum::preset (n)); repaint(); };
            addAndMakeVisible (b);
        }
        loadButton.onClick = [this] { loadWav(); };
        addAndMakeVisible (loadButton);
        refreshButtons();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (28);
        for (auto* b : slotButtons)
            b->setBounds (top.removeFromLeft (110).reduced (2));
        loadButton.setBounds (top.removeFromRight (130).reduced (2));
        auto bottom = r.removeFromBottom (30);
        const int w = bottom.getWidth() / generators.size();
        for (auto* b : generators)
            b->setBounds (bottom.removeFromLeft (w).reduced (2));
        bars = r.reduced (2, 8);
    }
    void paint (juce::Graphics& g) override
    {
        darkBox (g, bars.toFloat().expanded (2.0f));
        const auto& s = pending ? editing : proc.getSpectrum (slot);
        const float w = static_cast<float> (bars.getWidth()) / kSpectralHarmonics;
        for (int k = 0; k < kSpectralHarmonics; ++k)
        {
            const float a = s.amplitude[static_cast<size_t> (k)];
            const float h = a * static_cast<float> (bars.getHeight());
            const auto bar = juce::Rectangle<float> (static_cast<float> (bars.getX()) + k * w + 1.0f, static_cast<float> (bars.getBottom()) - h, w - 2.0f, h);
            g.setGradientFill (juce::ColourGradient (th().accent, 0, bar.getY(), th().accent.darker (0.6f), 0, static_cast<float> (bars.getBottom()), false));
            g.fillRect (bar);
            if (k % 8 == 0)
            {
                g.setColour (th().dim);
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (juce::String (k + 1), juce::Rectangle<float> (static_cast<float> (bars.getX()) + k * w, static_cast<float> (bars.getY()), 24.0f, 12.0f),
                            juce::Justification::topLeft);
            }
        }
        g.setColour (th().dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Draw the harmonics with the mouse (1 = the fundamental). Operators play it as \"Spectral " + juce::String (slot == 0 ? "A" : "B") + "\".",
                    bars.reduced (6, 4), juce::Justification::topRight);
    }
    void mouseDown (const juce::MouseEvent& e) override { drawAt (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { drawAt (e.position); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (pending)
            proc.setSpectrum (slot, editing);
        pending = false;
    }

private:
    void drawAt (juce::Point<float> pos)
    {
        if (! bars.toFloat().expanded (0.0f, 8.0f).contains (pos))
            return;
        if (! pending)
            editing = proc.getSpectrum (slot);
        const int k = std::clamp (static_cast<int> ((pos.x - static_cast<float> (bars.getX())) / static_cast<float> (bars.getWidth()) * kSpectralHarmonics), 0,
                                  kSpectralHarmonics - 1);
        editing.amplitude[static_cast<size_t> (k)] = std::clamp ((static_cast<float> (bars.getBottom()) - pos.y) / static_cast<float> (bars.getHeight()), 0.0f, 1.0f);
        pending = true;
        // Heard while drawing, a few times a second (each new wave is built off the audio thread and swapped in).
        if (const auto now = juce::Time::getMillisecondCounter(); now - lastSent > 80)
        {
            lastSent = now;
            proc.setSpectrum (slot, editing);
        }
        repaintBars();
    }
    void refreshButtons()
    {
        for (int i = 0; i < slotButtons.size(); ++i)
            slotButtons[i]->setToggleState (i == slot, juce::dontSendNotification);
    }
    void loadWav()
    {
        chooser = std::make_unique<juce::FileChooser> ("A single-cycle wave (WAV, AIFF, FLAC)", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<SpectralEditor> (this)] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr || fc.getResult() == juce::File())
                                      return;
                                  juce::String error;
                                  if (auto s = Spectrum::fromAudioFile (fc.getResult(), error))
                                      safe->proc.setSpectrum (safe->slot, *s);
                                  else
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load a wave", error);
                                  safe->repaint();
                              });
    }

    HexProcessor& proc;
    int slot = 0;
    juce::OwnedArray<juce::TextButton> slotButtons, generators;
    juce::TextButton loadButton { "Load WAV..." };
    juce::Rectangle<int> bars;
    void repaintBars() { repaint (bars.expanded (2)); }

    Spectrum editing;
    bool pending = false;
    juce::uint32 lastSent = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};

class Logo : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float cx = r.getX() + 26.0f, cy = r.getCentreY(), radius = 17.0f;
        juce::Path hex;
        for (int i = 0; i < 6; ++i)
        {
            const float a = juce::MathConstants<float>::pi / 6.0f + static_cast<float> (i) * juce::MathConstants<float>::twoPi / 6.0f;
            const auto pt = juce::Point<float> (cx + std::cos (a) * radius, cy + std::sin (a) * radius);
            if (i == 0)
                hex.startNewSubPath (pt);
            else
                hex.lineTo (pt);
        }
        hex.closeSubPath();
        g.setColour (th().accent.withAlpha (0.25f));
        g.fillPath (hex);
        g.setColour (th().accent);
        g.strokePath (hex, juce::PathStrokeType (2.2f));
        g.setColour (th().accent2);
        g.fillEllipse (cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
        g.setColour (th().text);
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText ("HEXFORGE", r.withTrimmedLeft (56.0f), juce::Justification::centredLeft);
        g.setColour (th().dim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText ("HYBRID FM SYNTHESIZER", r.withTrimmedLeft (58.0f).withTrimmedTop (36.0f), juce::Justification::topLeft);
    }
};
} // namespace

HexEditor::HexEditor (HexProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p), look (hexTheme()), keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard),
      presetBar (p, presetCategories(), look)
{
    setLookAndFeel (&look);
    auto& s = processor.state;
    addAndMakeVisible (content);

    const auto knob = [&s] (const juce::String& id, const juce::String& text, bool bipolar = false) { return std::make_unique<hwui::Knob> (s, id, text, bipolar); };
    const auto choice = [&s] (const juce::String& id, const juce::String& text) { return std::make_unique<hwui::Choice> (s, id, text); };
    const auto toggle = [&s] (const juce::String& id, const juce::String& text) { return std::make_unique<hwui::Switch> (s, id, text); };
    const auto panel = [this] (juce::Component& parent, const juce::String& title, int columns, juce::Rectangle<int> bounds, int displayHeight = 0)
    {
        auto pnl = std::make_unique<hwui::Panel> (title, columns, displayHeight);
        auto* raw = pnl.get();
        parent.addAndMakeVisible (raw);
        raw->setBounds (bounds);
        owned.push_back (std::move (pnl));
        return raw;
    };
    const auto own = [this] (std::unique_ptr<juce::Component> c) { auto* raw = c.get(); owned.push_back (std::move (c)); return raw; };
    const auto env = [&] (hwui::Panel* pnl, const juce::String& prefix)
    {
        pnl->add (knob (prefix + "_a", "ATTACK"));
        pnl->add (knob (prefix + "_d", "DECAY"));
        pnl->add (knob (prefix + "_s", "SUSTAIN"));
        pnl->add (knob (prefix + "_r", "RELEASE"));
    };

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

    const juce::StringArray pageNames { "OPERATORS", "FILTER & AMP", "SPECTRAL & MOD", "EFFECTS" };
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

    // --- Operators and the FM matrix ---
    auto& ops = pages[0];
    for (int o = 0; o < kNumOps; ++o)
    {
        auto* pnl = panel (ops, "Operator " + juce::String (o + 1), 4, { (o % 3) * 242, (o / 3) * 308, 236, 302 }, 34);
        pnl->setDisplay (own (std::make_unique<OpDisplay> (processor, o)));
        pnl->add (choice (opId (o, "wave"), "WAVE"), 0, 4);
        pnl->add (choice (opId (o, "ratio"), "RATIO"), 0, 2);
        pnl->add (knob (opId (o, "fine"), "FINE", true));
        pnl->add (knob (opId (o, "out"), "OUT"));
        pnl->add (knob (opId (o, "level"), "LEVEL"));
        pnl->add (knob (opId (o, "pan"), "PAN", true));
        pnl->add (knob (opId (o, "vel"), "VELOCITY"));
        pnl->add (std::make_unique<juce::Component>());
        env (pnl, opId (o, "env"));
    }
    {
        auto* pnl = panel (ops, "FM matrix  -  row: modulated, column: modulator, diagonal: feedback", 1, { 732, 0, 488, 420 });
        auto* grid = own (std::make_unique<MatrixGrid> (s));
        pnl->addAndMakeVisible (grid);
        grid->setBounds (14, 32, 460, 380);
    }
    {
        auto* pnl = panel (ops, "Engine", 4, { 732, 426, 488, 186 });
        pnl->add (knob ("fm_depth", "FM DEPTH"));
        pnl->add (knob ("uni_voices", "UNISON"));
        pnl->add (knob ("uni_detune", "DETUNE"));
        pnl->add (knob ("uni_spread", "SPREAD"));
        pnl->add (knob ("octave", "OCTAVE", true));
        pnl->add (knob ("drop_amt", "PITCH DROP", true));
        pnl->add (knob ("drop_time", "DROP TIME"));
    }

    // --- Filter and amplifier ---
    auto& filt = pages[1];
    {
        auto* pnl = panel (filt, "Ladder filter", 4, { 0, 0, 420, 300 });
        pnl->setPower (std::make_unique<hwui::Switch> (s, "flt_on", ""));
        pnl->add (choice ("flt_mode", "MODE"), 0, 2);
        pnl->add (knob ("flt_cut", "CUTOFF"));
        pnl->add (knob ("flt_res", "RESONANCE"));
        pnl->add (knob ("flt_drive", "DRIVE"));
        pnl->add (knob ("flt_env", "ENV AMT", true));
        pnl->add (knob ("flt_key", "KEY TRACK"));
        pnl->add (knob ("flt_vel", "VELOCITY"));
    }
    {
        auto* pnl = panel (filt, "Filter envelope", 4, { 428, 0, 340, 300 });
        env (pnl, "fenv");
    }
    {
        auto* pnl = panel (filt, "Filter LFO", 3, { 776, 0, 444, 300 });
        pnl->add (knob ("flfo_amt", "AMOUNT", true));
        pnl->add (choice ("flfo_shape", "SHAPE"));
        pnl->add (knob ("flfo_rate", "RATE"));
        pnl->add (toggle ("flfo_sync", "Tempo sync"));
        pnl->add (choice ("flfo_div", "DIVISION"));
    }
    {
        auto* pnl = panel (filt, "Amplifier", 5, { 0, 308, 420, 304 });
        env (pnl, "aenv");
        pnl->add (knob ("amp_vel", "VELOCITY"));
    }
    {
        auto* pnl = panel (filt, "LFO 2 (modulation)", 2, { 428, 308, 340, 304 });
        pnl->add (choice ("lfo2_shape", "SHAPE"));
        pnl->add (knob ("lfo2_rate", "RATE"));
        pnl->add (toggle ("lfo2_sync", "Tempo sync"));
        pnl->add (choice ("lfo2_div", "DIVISION"));
    }
    {
        auto* pnl = panel (filt, "Voice", 2, { 776, 308, 444, 304 });
        pnl->add (choice ("mode", "MODE"));
        pnl->add (knob ("voices", "POLYPHONY"));
        pnl->add (knob ("glide", "GLIDE"));
        pnl->add (knob ("bend", "BEND RANGE"));
    }

    // --- Spectral waves and modulation ---
    auto& spec = pages[2];
    {
        auto* pnl = panel (spec, "Spectral waves", 1, { 0, 0, 700, 612 });
        auto* editor = own (std::make_unique<SpectralEditor> (processor));
        pnl->addAndMakeVisible (editor);
        editor->setBounds (12, 32, 676, 568);
        spectralEditor = editor;
    }
    {
        auto* pnl = panel (spec, "Modulation", 1, { 708, 0, 512, 612 });
        for (int slot = 0; slot < kMatrixSlots; ++slot)
        {
            auto* row = own (std::make_unique<hwui::MatrixRow> (s, slot, slotId (slot, "src"), slotId (slot, "dst"), slotId (slot, "amt"), sourceNames(), destNames(),
                                                         hwui::MatrixRow::Style { 22, 120, 130, 6, 48, false, false }));
            pnl->addAndMakeVisible (row);
            row->setBounds (8, 36 + slot * 52, 496, 46);
        }
        auto* hint = static_cast<juce::Label*> (own (std::make_unique<juce::Label>()));
        hint->setText ("Factory presets: mod wheel = more FM, Macro 1 = cutoff, Macro 2 = FM depth, Macro 3 = distortion, Macro 4 = unison width.",
                       juce::dontSendNotification);
        hint->setColour (juce::Label::textColourId, th().dim);
        hint->setJustificationType (juce::Justification::topLeft);
        pnl->addAndMakeVisible (hint);
        hint->setBounds (14, 470, 484, 60);
    }

    // --- Effects ---
    auto& fxPage = pages[3];
    {
        auto* pnl = panel (fxPage, "Distortion", 3, { 0, 0, 400, 300 });
        pnl->setPower (std::make_unique<hwui::Switch> (s, "dist_on", ""));
        pnl->add (choice ("dist_type", "TYPE"));
        pnl->add (knob ("dist_drive", "DRIVE"));
        pnl->add (knob ("dist_mix", "MIX"));
    }
    {
        auto* pnl = panel (fxPage, "Equalizer", 4, { 408, 0, 404, 300 });
        pnl->setPower (std::make_unique<hwui::Switch> (s, "eq_on", ""));
        pnl->add (knob ("eq_low", "LOW", true));
        pnl->add (knob ("eq_mid", "MID", true));
        pnl->add (knob ("eq_midf", "MID FREQ"));
        pnl->add (knob ("eq_high", "HIGH", true));
    }
    {
        auto* pnl = panel (fxPage, "Chorus", 3, { 820, 0, 400, 300 });
        pnl->setPower (std::make_unique<hwui::Switch> (s, "cho_on", ""));
        pnl->add (knob ("cho_rate", "RATE"));
        pnl->add (knob ("cho_depth", "DEPTH"));
        pnl->add (knob ("cho_mix", "MIX"));
    }
    {
        auto* pnl = panel (fxPage, "Delay", 3, { 0, 308, 606, 304 });
        pnl->setPower (std::make_unique<hwui::Switch> (s, "dly_on", ""));
        pnl->add (choice ("dly_div", "DIVISION"));
        pnl->add (knob ("dly_fb", "FEEDBACK"));
        pnl->add (knob ("dly_tone", "TONE"));
        pnl->add (toggle ("dly_pp", "Ping-pong"));
        pnl->add (knob ("dly_mix", "MIX"));
    }
    {
        auto* pnl = panel (fxPage, "Reverb", 3, { 614, 308, 606, 304 });
        pnl->setPower (std::make_unique<hwui::Switch> (s, "rev_on", ""));
        pnl->add (knob ("rev_size", "SIZE"));
        pnl->add (knob ("rev_damp", "DAMPING"));
        pnl->add (knob ("rev_mix", "MIX"));
    }

    keyboard.setAvailableRange (24, 108);
    keyboard.setKeyWidth (static_cast<float> (kWidth - 20) / 50.0f);
    keyboard.setLowestVisibleKey (24);
    content.addAndMakeVisible (keyboard);
    keyboard.setBounds (10, 718, kWidth - 20, 74);

    // The panels were given their places before their controls: lay them out now.
    for (auto& c : owned)
        if (auto* pnl = dynamic_cast<hwui::Panel*> (c.get()))
            pnl->resized();

    showPage (0);
    content.setSize (kWidth, kHeight);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (kWidth) / kHeight);
    setResizeLimits (kWidth * 6 / 10, kHeight * 6 / 10, kWidth * 2, kHeight * 2);
    setSize (kWidth, kHeight);
    startTimerHz (30);
}

HexEditor::~HexEditor()
{
    setLookAndFeel (nullptr);
}

void HexEditor::paint (juce::Graphics& g)
{
    g.fillAll (th().background);
    g.addTransform (juce::AffineTransform::scale (static_cast<float> (getWidth()) / kWidth));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1e2127), 0, 0, juce::Colour (0xff111317), 0, 62, false));
    g.fillRect (0, 0, kWidth, 62);
    g.setGradientFill (juce::ColourGradient (th().accent, 0, 0, th().accent2, static_cast<float> (kWidth), 0, false));
    g.fillRect (0, 62, kWidth, 2);
}

void HexEditor::resized()
{
    content.setBounds (0, 0, kWidth, kHeight);
    content.setTransform (juce::AffineTransform::scale (static_cast<float> (getWidth()) / kWidth));
}

void HexEditor::showPage (int index)
{
    page = index;
    for (size_t i = 0; i < pages.size(); ++i)
        pages[i].setVisible (static_cast<int> (i) == page);
    for (int i = 0; i < pageButtons.size(); ++i)
        pageButtons[i]->setToggleState (i == page, juce::dontSendNotification);
}

void HexEditor::timerCallback()
{
    meter.setLevel (processor.consumePeak());
    if (presetBar.poll() && spectralEditor != nullptr)
        spectralEditor->repaint();
    if (page == 0)
        pages[0].repaint();
}

} // namespace hexforge
