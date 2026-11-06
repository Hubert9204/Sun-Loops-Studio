#include "ui/SamplerPanel.h"

namespace sls
{
namespace
{
const juce::Colour kPanelBackground { 0xff22252b };
const juce::Colour kPanelText { 0xffd5d9e0 };
const juce::Colour kPanelDim { 0xff8a919d };

juce::String noteName (int key)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((key % 12) + 12) % 12]) + juce::String (key / 12 - 1);
}
} // namespace

SamplerPanel::SamplerPanel()
{
    for (auto* l : { &sourceLabel, &programInfo })
    {
        l->setColour (juce::Label::textColourId, kPanelText);
        addAndMakeVisible (*l);
    }
    programInfo.setColour (juce::Label::textColourId, kPanelDim);
    programInfo.setFont (juce::FontOptions (12.0f));
    sourceLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));

    for (auto* l : { &rootLabel, &attackLabel, &decayLabel, &sustainLabel, &releaseLabel, &loopLabel, &loopStartLabel, &loopEndLabel,
                     &filterLabel, &cutoffLabel, &resonanceLabel })
    {
        l->setColour (juce::Label::textColourId, kPanelText);
        addAndMakeVisible (*l);
    }

    for (auto* b : { &loadSampleButton, &loadFontButton, &clearProgramButton })
        addAndMakeVisible (*b);
    loadSampleButton.onClick = [this] { if (onLoadSample) onLoadSample(); };
    loadFontButton.onClick = [this] { if (onLoadSoundFont) onLoadSoundFont(); };
    clearProgramButton.onClick = [this] { if (onClearProgram) onClearProgram(); };

    slider (rootSlider, 0, 127, 1, {});
    rootSlider.textFromValueFunction = [] (double v) { return noteName (static_cast<int> (v)) + " (" + juce::String (static_cast<int> (v)) + ")"; };
    rootSlider.onValueChange = [this] { write ([this] (Channel& c) { c.rootPitch = static_cast<int> (rootSlider.getValue()); }); };

    envelopeToggle.setColour (juce::ToggleButton::textColourId, kPanelText);
    addAndMakeVisible (envelopeToggle);
    envelopeToggle.onClick = [this] { write ([this] (Channel& c) { c.sampler.envelope = envelopeToggle.getToggleState(); }); };

    slider (attackSlider, 0.0, 10.0, 0.001, " s").setSkewFactorFromMidPoint (0.3);
    slider (decaySlider, 0.0, 10.0, 0.001, " s").setSkewFactorFromMidPoint (0.5);
    slider (sustainSlider, 0.0, 100.0, 1.0, " %");
    slider (releaseSlider, 0.0, 10.0, 0.001, " s").setSkewFactorFromMidPoint (0.5);
    attackSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.attack = static_cast<float> (attackSlider.getValue()); }); };
    decaySlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.decay = static_cast<float> (decaySlider.getValue()); }); };
    sustainSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.sustain = static_cast<float> (sustainSlider.getValue() / 100.0); }); };
    releaseSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.release = static_cast<float> (releaseSlider.getValue()); }); };

    loopBox.addItem ("Off", 1);
    loopBox.addItem ("Forward", 2);
    addAndMakeVisible (loopBox);
    loopBox.onChange = [this] { write ([this] (Channel& c) { c.sampler.loop = loopBox.getSelectedId() == 2 ? LoopMode::Forward : LoopMode::Off; }); };
    slider (loopStartSlider, 0.0, 100.0, 0.1, " %");
    slider (loopEndSlider, 0.0, 100.0, 0.1, " %");
    loopStartSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.loopStart = static_cast<float> (loopStartSlider.getValue() / 100.0); }); };
    loopEndSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.loopEnd = static_cast<float> (loopEndSlider.getValue() / 100.0); }); };

    filterBox.addItem ("Off", 1);
    filterBox.addItem ("Low-pass", 2);
    filterBox.addItem ("High-pass", 3);
    filterBox.addItem ("Band-pass", 4);
    addAndMakeVisible (filterBox);
    filterBox.onChange = [this]
    {
        write ([this] (Channel& c) { c.sampler.filter = static_cast<SamplerFilter> (std::clamp (filterBox.getSelectedId() - 1, 0, 3)); });
    };
    slider (cutoffSlider, 0.0, 1.0, 0.001, {});
    cutoffSlider.textFromValueFunction = [] (double v)
    {
        const double hz = SamplerSettings::cutoffHz (static_cast<float> (v));
        return hz >= 1000.0 ? juce::String (hz / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    };
    slider (resonanceSlider, 0.0, 100.0, 1.0, " %");
    cutoffSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.cutoff = static_cast<float> (cutoffSlider.getValue()); }); };
    resonanceSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.resonance = static_cast<float> (resonanceSlider.getValue() / 100.0); }); };

    setSize (600, 590);
}

juce::Slider& SamplerPanel::slider (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 92, 22);
    s.setRange (lo, hi, step);
    if (suffix.isNotEmpty())
        s.setTextValueSuffix (suffix);
    addAndMakeVisible (s);
    return s;
}

void SamplerPanel::write (const std::function<void (Channel&)>& change)
{
    if (updating || ! getChannel)
        return;
    if (auto* c = getChannel())
    {
        change (*c);
        c->sampler = c->sampler.sanitised();
        if (onChanged)
            onChanged();
    }
}

void SamplerPanel::refresh()
{
    const juce::ScopedValueSetter<bool> busy (updating, true);
    auto* c = getChannel ? getChannel() : nullptr;
    setEnabled (c != nullptr);
    if (c == nullptr)
    {
        sourceLabel.setText ("(this channel is no longer a sampler)", juce::dontSendNotification);
        programInfo.setText ({}, juce::dontSendNotification);
        return;
    }

    const bool program = c->program != nullptr;
    if (program)
    {
        const auto& p = *c->program;
        sourceLabel.setText (juce::String (p.name.empty() ? "Program" : p.name), juce::dontSendNotification);
        juce::String info;
        if (p.zones.empty())
            info = "Missing file: " + juce::String (p.source);
        else if (p.preset >= 0)
            info = "SoundFont preset, " + juce::String (static_cast<int> (p.zones.size())) + " zones - " + juce::File (juce::String (p.source)).getFileName()
                 + ". Root keys and loops come from the file.";
        else
            info = juce::String (static_cast<int> (p.zones.size())) + " slices on the keys " + noteName (p.zones.front().loKey) + " to "
                 + noteName (p.zones.back().hiKey) + ", one per key";
        programInfo.setText (info, juce::dontSendNotification);
    }
    else
    {
        sourceLabel.setText (c->sample != nullptr ? juce::String (c->sample->name) : juce::String ("(no sample)"), juce::dontSendNotification);
        programInfo.setText (c->sample != nullptr ? juce::String (c->sample->numFrames() / c->sample->sampleRate, 2) + " s, " + juce::String (c->sample->sampleRate, 0) + " Hz"
                                                  : juce::String ("Load a sample or a SoundFont"),
                             juce::dontSendNotification);
    }
    clearProgramButton.setEnabled (program);
    rootSlider.setEnabled (! program);
    loopBox.setEnabled (! program);
    loopStartSlider.setEnabled (! program && c->sampler.loop != LoopMode::Off);
    loopEndSlider.setEnabled (! program && c->sampler.loop != LoopMode::Off);

    const auto& s = c->sampler;
    rootSlider.setValue (c->rootPitch, juce::dontSendNotification);
    envelopeToggle.setToggleState (s.envelope, juce::dontSendNotification);
    for (auto* x : { &attackSlider, &decaySlider, &sustainSlider, &releaseSlider })
        x->setEnabled (s.envelope);
    attackSlider.setValue (s.attack, juce::dontSendNotification);
    decaySlider.setValue (s.decay, juce::dontSendNotification);
    sustainSlider.setValue (s.sustain * 100.0, juce::dontSendNotification);
    releaseSlider.setValue (s.release, juce::dontSendNotification);
    loopBox.setSelectedId (s.loop == LoopMode::Forward ? 2 : 1, juce::dontSendNotification);
    loopStartSlider.setValue (s.loopStart * 100.0, juce::dontSendNotification);
    loopEndSlider.setValue (s.loopEnd * 100.0, juce::dontSendNotification);
    filterBox.setSelectedId (static_cast<int> (s.filter) + 1, juce::dontSendNotification);
    cutoffSlider.setEnabled (s.filter != SamplerFilter::Off);
    resonanceSlider.setEnabled (s.filter != SamplerFilter::Off);
    cutoffSlider.setValue (s.cutoff, juce::dontSendNotification);
    resonanceSlider.setValue (s.resonance * 100.0, juce::dontSendNotification);
}

void SamplerPanel::paint (juce::Graphics& g)
{
    g.fillAll (kPanelBackground);
    g.setColour (kPanelDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    const char* names[] = { "SOUND", "ENVELOPE", "LOOP", "FILTER" };
    for (size_t i = 0; i < headers.size(); ++i)
    {
        g.drawText (names[i], headers[i], juce::Justification::centredLeft);
        g.setColour (kPanelDim.withAlpha (0.3f));
        g.drawHorizontalLine (headers[i].getBottom(), static_cast<float> (headers[i].getX() + 70), static_cast<float> (getWidth() - 12));
        g.setColour (kPanelDim);
    }
}

void SamplerPanel::resized()
{
    auto area = getLocalBounds().reduced (12);
    sourceLabel.setBounds (area.removeFromTop (24));
    programInfo.setBounds (area.removeFromTop (20));
    area.removeFromTop (6);
    auto buttons = area.removeFromTop (26);
    loadSampleButton.setBounds (buttons.removeFromLeft (130));
    buttons.removeFromLeft (8);
    loadFontButton.setBounds (buttons.removeFromLeft (190));
    buttons.removeFromLeft (8);
    clearProgramButton.setBounds (buttons.removeFromLeft (130));

    const auto row = [&area] (juce::Label& label, juce::Component& c)
    {
        auto r = area.removeFromTop (26);
        area.removeFromTop (6);
        label.setBounds (r.removeFromLeft (100));
        c.setBounds (r);
    };
    area.removeFromTop (8);
    headers[0] = area.removeFromTop (18);
    area.removeFromTop (4);
    row (rootLabel, rootSlider);
    area.removeFromTop (4);
    headers[1] = area.removeFromTop (18);
    area.removeFromTop (4);
    envelopeToggle.setBounds (area.removeFromTop (24).withTrimmedLeft (96));
    area.removeFromTop (4);
    row (attackLabel, attackSlider);
    row (decayLabel, decaySlider);
    row (sustainLabel, sustainSlider);
    row (releaseLabel, releaseSlider);
    area.removeFromTop (4);
    headers[2] = area.removeFromTop (18);
    area.removeFromTop (4);
    {
        auto r = area.removeFromTop (26);
        area.removeFromTop (6);
        loopLabel.setBounds (r.removeFromLeft (100));
        loopBox.setBounds (r.removeFromLeft (120));
    }
    {
        auto r = area.removeFromTop (26);
        area.removeFromTop (6);
        loopStartLabel.setBounds (r.removeFromLeft (100));
        loopStartSlider.setBounds (r.removeFromLeft (r.getWidth() / 2 - 4));
        r.removeFromLeft (8);
        loopEndLabel.setBounds (r.removeFromLeft (70));
        loopEndSlider.setBounds (r);
    }
    area.removeFromTop (4);
    headers[3] = area.removeFromTop (18);
    area.removeFromTop (4);
    {
        auto r = area.removeFromTop (26);
        area.removeFromTop (6);
        filterLabel.setBounds (r.removeFromLeft (100));
        filterBox.setBounds (r.removeFromLeft (120));
    }
    row (cutoffLabel, cutoffSlider);
    row (resonanceLabel, resonanceSlider);
}

SamplerWindow::SamplerWindow()
    : juce::DocumentWindow ("Sampler", juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (false);
    setContentNonOwned (&content, true);
    setResizable (false, false);
    centreWithSize (content.getWidth(), content.getHeight());
    setAlwaysOnTop (true);
}
} // namespace sls
