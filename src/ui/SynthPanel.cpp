#include "ui/SynthPanel.h"

namespace sls
{
namespace
{
const juce::Colour kPanelBackground { 0xff22252b };
const juce::Colour kPanelText { 0xffd5d9e0 };
const juce::Colour kPanelDim { 0xff8a919d };

constexpr int kColumns = 4;
constexpr int kRowHeight = 24;
constexpr int kRowGap = 5;
constexpr int kLabelWidth = 64;

juce::String secondsText (double s)
{
    return s < 1.0 ? juce::String (juce::roundToInt (s * 1000.0)) + " ms" : juce::String (s, 2) + " s";
}

const juce::StringArray kWaves { "Sine", "Triangle", "Saw", "Square" };
} // namespace

SynthPanel::Section& SynthPanel::addSection (const juce::String& title, int column)
{
    sections.push_back (std::make_unique<Section>());
    sections.back()->title = title;
    sections.back()->column = column;
    return *sections.back();
}

SynthPanel::Param& SynthPanel::addParam (Section& section, const juce::String& name, double lo, double hi, double step, const juce::String& suffix,
                                         std::function<double (const SynthPatch&)> read, std::function<void (SynthPatch&, double)> write)
{
    params.push_back (std::make_unique<Param>());
    auto& p = *params.back();
    p.label.setText (name, juce::dontSendNotification);
    p.label.setColour (juce::Label::textColourId, kPanelText);
    p.label.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (p.label);
    p.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    p.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 20);
    p.slider.setRange (lo, hi, step);
    if (suffix.isNotEmpty())
        p.slider.setTextValueSuffix (suffix);
    p.read = std::move (read);
    p.write = std::move (write);
    p.slider.onValueChange = [this, &p] { edit ([&p] (SynthPatch& patch) { p.write (patch, p.slider.getValue()); }); };
    addAndMakeVisible (p.slider);
    section.rows.push_back ({ &p.label, &p.slider });
    return p;
}

SynthPanel::Choice& SynthPanel::addChoice (Section& section, const juce::String& name, const juce::StringArray& items,
                                           std::function<int (const SynthPatch&)> read, std::function<void (SynthPatch&, int)> write)
{
    choices.push_back (std::make_unique<Choice>());
    auto& c = *choices.back();
    c.label.setText (name, juce::dontSendNotification);
    c.label.setColour (juce::Label::textColourId, kPanelText);
    c.label.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (c.label);
    c.box.addItemList (items, 1);
    c.read = std::move (read);
    c.write = std::move (write);
    c.box.onChange = [this, &c] { edit ([&c] (SynthPatch& patch) { c.write (patch, c.box.getSelectedItemIndex()); }); };
    addAndMakeVisible (c.box);
    section.rows.push_back ({ &c.label, &c.box });
    return c;
}

SynthPanel::SynthPanel()
{
    titleLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, kPanelText);
    addAndMakeVisible (titleLabel);
    noteLabel.setFont (juce::FontOptions (12.0f));
    noteLabel.setColour (juce::Label::textColourId, kPanelDim);
    addAndMakeVisible (noteLabel);

    patchBox.setTextWhenNothingSelected ("Load a sound...");
    for (size_t i = 0; i < synthFactoryPatches().size(); ++i)
        patchBox.addItem (synthFactoryPatches()[i].name, static_cast<int> (i) + 1);
    patchBox.onChange = [this]
    {
        const int i = patchBox.getSelectedItemIndex();
        if (i < 0 || updating)
            return;
        const auto patch = synthFactoryPatches()[static_cast<size_t> (i)].patch;
        edit ([patch] (SynthPatch& p) { p = patch; });
        refresh();
    };
    addAndMakeVisible (patchBox);

    previewButton.onStateChange = [this]
    {
        const bool down = previewButton.isDown();
        if (down != previewButton.getToggleState())
        {
            previewButton.setToggleState (down, juce::dontSendNotification);
            if (onPreview)
                onPreview (down);
        }
    };
    addAndMakeVisible (previewButton);

    auto& top = addSection ({}, -1);
    engineChoice = &addChoice (top, "Engine", { "Classic (the original sine sound)", "Subtractive" },
                               [] (const SynthPatch& p) { return static_cast<int> (p.engine); },
                               [] (SynthPatch& p, int i) { p.engine = static_cast<SynthEngine> (std::clamp (i, 0, 1)); });

    for (int o = 0; o < 2; ++o)
    {
        auto& s = addSection ("OSCILLATOR " + juce::String (o + 1), 0);
        const auto osc = static_cast<size_t> (o);
        addChoice (s, "Wave", kWaves, [osc] (const SynthPatch& p) { return static_cast<int> (p.osc[osc].wave); },
                   [osc] (SynthPatch& p, int i) { p.osc[osc].wave = static_cast<Waveform> (std::clamp (i, 0, 3)); });
        addParam (s, "Level", 0.0, 100.0, 1.0, " %", [osc] (const SynthPatch& p) { return p.osc[osc].level * 100.0; },
                  [osc] (SynthPatch& p, double v) { p.osc[osc].level = static_cast<float> (v / 100.0); });
        addParam (s, "Octave", -3.0, 3.0, 1.0, {}, [osc] (const SynthPatch& p) { return p.osc[osc].octave; },
                  [osc] (SynthPatch& p, double v) { p.osc[osc].octave = juce::roundToInt (v); });
        addParam (s, "Semitone", -12.0, 12.0, 1.0, {}, [osc] (const SynthPatch& p) { return p.osc[osc].semitones; },
                  [osc] (SynthPatch& p, double v) { p.osc[osc].semitones = juce::roundToInt (v); });
        addParam (s, "Fine", -100.0, 100.0, 1.0, " ct", [osc] (const SynthPatch& p) { return p.osc[osc].fine; },
                  [osc] (SynthPatch& p, double v) { p.osc[osc].fine = static_cast<float> (v); });
        addParam (s, "Pulse w.", 5.0, 95.0, 1.0, " %", [osc] (const SynthPatch& p) { return p.osc[osc].pulseWidth * 100.0; },
                  [osc] (SynthPatch& p, double v) { p.osc[osc].pulseWidth = static_cast<float> (v / 100.0); });
    }

    {
        auto& s = addSection ("NOISE", 1);
        addParam (s, "Level", 0.0, 100.0, 1.0, " %", [] (const SynthPatch& p) { return p.noise * 100.0; },
                  [] (SynthPatch& p, double v) { p.noise = static_cast<float> (v / 100.0); });
    }
    {
        auto& s = addSection ("FILTER", 1);
        addChoice (s, "Type", { "Off", "Low-pass", "High-pass", "Band-pass" }, [] (const SynthPatch& p) { return static_cast<int> (p.filter); },
                   [] (SynthPatch& p, int i) { p.filter = static_cast<SamplerFilter> (std::clamp (i, 0, 3)); });
        steepToggle.setColour (juce::ToggleButton::textColourId, kPanelText);
        steepToggle.onClick = [this] { edit ([this] (SynthPatch& p) { p.steep = steepToggle.getToggleState(); }); };
        addAndMakeVisible (steepToggle);
        s.rows.push_back ({ nullptr, &steepToggle });
        auto& cutoff = addParam (s, "Cutoff", 0.0, 1.0, 0.001, {}, [] (const SynthPatch& p) { return p.cutoff; },
                                 [] (SynthPatch& p, double v) { p.cutoff = static_cast<float> (v); });
        cutoff.slider.textFromValueFunction = [] (double v)
        {
            const double hz = SamplerSettings::cutoffHz (static_cast<float> (v));
            return hz >= 1000.0 ? juce::String (hz / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
        };
        addParam (s, "Reso", 0.0, 100.0, 1.0, " %", [] (const SynthPatch& p) { return p.resonance * 100.0; },
                  [] (SynthPatch& p, double v) { p.resonance = static_cast<float> (v / 100.0); });
        addParam (s, "Env amt", -100.0, 100.0, 1.0, " %", [] (const SynthPatch& p) { return p.envAmount * 100.0; },
                  [] (SynthPatch& p, double v) { p.envAmount = static_cast<float> (v / 100.0); });
        addParam (s, "Key track", 0.0, 100.0, 1.0, " %", [] (const SynthPatch& p) { return p.keyTrack * 100.0; },
                  [] (SynthPatch& p, double v) { p.keyTrack = static_cast<float> (v / 100.0); });
    }

    const auto envelope = [this] (const juce::String& title, Adsr SynthPatch::* member)
    {
        auto& s = addSection (title, 2);
        auto& a = addParam (s, "Attack", 0.0, 10.0, 0.001, {}, [member] (const SynthPatch& p) { return (p.*member).attack; },
                            [member] (SynthPatch& p, double v) { (p.*member).attack = static_cast<float> (v); });
        auto& d = addParam (s, "Decay", 0.0, 10.0, 0.001, {}, [member] (const SynthPatch& p) { return (p.*member).decay; },
                            [member] (SynthPatch& p, double v) { (p.*member).decay = static_cast<float> (v); });
        addParam (s, "Sustain", 0.0, 100.0, 1.0, " %", [member] (const SynthPatch& p) { return (p.*member).sustain * 100.0; },
                  [member] (SynthPatch& p, double v) { (p.*member).sustain = static_cast<float> (v / 100.0); });
        auto& r = addParam (s, "Release", 0.0, 10.0, 0.001, {}, [member] (const SynthPatch& p) { return (p.*member).release; },
                            [member] (SynthPatch& p, double v) { (p.*member).release = static_cast<float> (v); });
        for (auto* x : { &a.slider, &d.slider, &r.slider })
        {
            x->setSkewFactorFromMidPoint (0.5);
            x->textFromValueFunction = secondsText;
        }
    };
    envelope ("AMPLIFIER ENVELOPE", &SynthPatch::amp);
    envelope ("FILTER ENVELOPE", &SynthPatch::filterEnv);

    {
        auto& s = addSection ("LFO", 3);
        addChoice (s, "Target", { "Off", "Pitch (vibrato)", "Cutoff", "Level (tremolo)", "Pulse width" },
                   [] (const SynthPatch& p) { return static_cast<int> (p.lfoTarget); },
                   [] (SynthPatch& p, int i) { p.lfoTarget = static_cast<LfoTarget> (std::clamp (i, 0, 4)); });
        addChoice (s, "Wave", kWaves, [] (const SynthPatch& p) { return static_cast<int> (p.lfoWave); },
                   [] (SynthPatch& p, int i) { p.lfoWave = static_cast<Waveform> (std::clamp (i, 0, 3)); });
        auto& rate = addParam (s, "Rate", 0.05, 20.0, 0.01, " Hz", [] (const SynthPatch& p) { return p.lfoRate; },
                               [] (SynthPatch& p, double v) { p.lfoRate = static_cast<float> (v); });
        rate.slider.setSkewFactorFromMidPoint (2.0);
        addParam (s, "Depth", 0.0, 100.0, 1.0, " %", [] (const SynthPatch& p) { return p.lfoDepth * 100.0; },
                  [] (SynthPatch& p, double v) { p.lfoDepth = static_cast<float> (v / 100.0); });
    }
    {
        auto& s = addSection ("OUTPUT", 3);
        addParam (s, "Level", 0.0, 150.0, 1.0, " %", [] (const SynthPatch& p) { return p.gain * 100.0; },
                  [] (SynthPatch& p, double v) { p.gain = static_cast<float> (v / 100.0); });
    }

    setSize (1080, 500);
}

void SynthPanel::edit (const std::function<void (SynthPatch&)>& change)
{
    if (updating || ! getChannel)
        return;
    if (auto* c = getChannel())
    {
        change (c->synth);
        c->synth = c->synth.sanitised();
        if (onChanged)
            onChanged();
        refresh(); // the engine switch enables the other controls, a loaded sound moves them all
    }
}

void SynthPanel::refresh()
{
    const juce::ScopedValueSetter<bool> busy (updating, true);
    auto* c = getChannel ? getChannel() : nullptr;
    setEnabled (c != nullptr);
    if (c == nullptr)
    {
        titleLabel.setText ("(this channel is no longer a synth)", juce::dontSendNotification);
        return;
    }
    const auto& p = c->synth;
    const bool sub = p.engine == SynthEngine::Subtractive;
    titleLabel.setText (juce::String (c->name), juce::dontSendNotification);
    noteLabel.setText (sub ? "Two oscillators and noise, a filter with its own envelope, an amplitude envelope and an LFO."
                           : "The original sound of Sun Loops Studio (a sine with two harmonics). Choose Subtractive, or load a sound, to shape it.",
                       juce::dontSendNotification);

    int match = -1;
    for (size_t i = 0; i < synthFactoryPatches().size() && match < 0; ++i)
        if (synthFactoryPatches()[i].patch.sanitised() == p)
            match = static_cast<int> (i);
    if (match >= 0)
        patchBox.setSelectedItemIndex (match, juce::dontSendNotification);
    else
        patchBox.setText ("(edited)", juce::dontSendNotification);

    for (auto& x : params)
    {
        x->slider.setValue (x->read (p), juce::dontSendNotification);
        x->slider.setEnabled (sub);
    }
    for (auto& x : choices)
    {
        x->box.setSelectedItemIndex (x->read (p), juce::dontSendNotification);
        x->box.setEnabled (sub || x.get() == engineChoice);
    }
    steepToggle.setToggleState (p.steep, juce::dontSendNotification);
    steepToggle.setEnabled (sub);
}

void SynthPanel::paint (juce::Graphics& g)
{
    g.fillAll (kPanelBackground);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (const auto& s : sections)
    {
        if (s->column < 0)
            continue;
        g.setColour (kPanelDim);
        g.drawText (s->title, s->header, juce::Justification::centredLeft);
        g.setColour (kPanelDim.withAlpha (0.3f));
        g.drawHorizontalLine (s->header.getBottom(), static_cast<float> (s->header.getX()), static_cast<float> (s->header.getRight()));
    }
}

void SynthPanel::resized()
{
    auto area = getLocalBounds().reduced (14);
    auto head = area.removeFromTop (26);
    titleLabel.setBounds (head.removeFromLeft (200));
    patchBox.setBounds (head.removeFromLeft (220));
    head.removeFromLeft (16);
    if (engineChoice != nullptr)
    {
        engineChoice->label.setBounds (head.removeFromLeft (56));
        engineChoice->box.setBounds (head.removeFromLeft (260));
    }
    head.removeFromLeft (16);
    previewButton.setBounds (head.removeFromLeft (90));
    noteLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (8);

    const int gap = 18;
    const int columnWidth = (area.getWidth() - gap * (kColumns - 1)) / kColumns;
    std::array<int, kColumns> y {};
    y.fill (area.getY());
    for (auto& s : sections)
    {
        if (s->column < 0)
            continue;
        const int x = area.getX() + s->column * (columnWidth + gap);
        auto& top = y[static_cast<size_t> (s->column)];
        s->header = { x, top, columnWidth, 18 };
        top += 24;
        for (auto& row : s->rows)
        {
            juce::Rectangle<int> r (x, top, columnWidth, kRowHeight);
            if (row.label != nullptr)
                row.label->setBounds (r.removeFromLeft (kLabelWidth));
            else
                r.removeFromLeft (kLabelWidth);
            row.control->setBounds (r);
            top += kRowHeight + kRowGap;
        }
        top += 10;
    }
}

SynthWindow::SynthWindow()
    : juce::DocumentWindow ("Synth", juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (false);
    setContentNonOwned (&content, true);
    setResizable (false, false);
    centreWithSize (content.getWidth(), content.getHeight());
    setAlwaysOnTop (true);
}
} // namespace sls
