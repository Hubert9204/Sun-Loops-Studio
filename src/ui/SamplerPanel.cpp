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
    return juce::String (names[((key % 12) + 12) % 12]) + juce::String (key / 12); // FL Studio's names: MIDI 60 = C5
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
                     &filterLabel, &cutoffLabel, &resonanceLabel, &startLabel, &fadeInLabel, &fadeOutLabel })
    {
        l->setColour (juce::Label::textColourId, kPanelText);
        addAndMakeVisible (*l);
    }

    speechButton.setTooltip ("The speech synthesizer: type a text and this channel plays it, spoken (as FL Studio's Speech Synthesizer)");
    speechButton.onClick = [this] { if (onSpeech) onSpeech(); };
    for (auto* b : { &loadSampleButton, &loadFontButton, &clearProgramButton, &speechButton })
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
    loopBox.addItem ("Ping-pong", 3); // 0.62.0
    addAndMakeVisible (loopBox);
    loopBox.onChange = [this]
    {
        write ([this] (Channel& c) { c.sampler.loop = loopBox.getSelectedId() == 2 ? LoopMode::Forward : loopBox.getSelectedId() == 3 ? LoopMode::PingPong : LoopMode::Off; });
    };
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

    // 0.61.0: reverse, start point and fades (they apply to notes that do not loop)
    reverseToggle.setTooltip ("Play the sample backwards (notes that do not loop)");
    reverseToggle.onClick = [this] { write ([this] (Channel& c) { c.sampler.reverse = reverseToggle.getToggleState(); }); };
    addAndMakeVisible (reverseToggle);
    slider (startSlider, 0.0, 99.0, 0.1, " %");
    startSlider.setTooltip ("Where in the sample a note starts (FL Studio's SMP START)");
    startSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.start = static_cast<float> (startSlider.getValue() / 100.0); }); };
    slider (fadeInSlider, 0.0, 100.0, 0.1, " %");
    fadeInSlider.setTooltip ("Fade the start of the sample in over this much of its length (FL Studio's IN)");
    fadeInSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.fadeIn = static_cast<float> (fadeInSlider.getValue() / 100.0); }); };
    slider (fadeOutSlider, 0.0, 100.0, 0.1, " %");
    fadeOutSlider.setTooltip ("Fade the end of the sample out over this much of its length (FL Studio's OUT)");
    fadeOutSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.fadeOut = static_cast<float> (fadeOutSlider.getValue() / 100.0); }); };

    // 0.62.0: the loop's crossfade, the envelope's hold on the cutoff, the LFO and the pitch envelope
    for (auto* l : { &crossfadeLabel, &cutoffEnvLabel, &lfoLabel, &lfoRateLabel, &lfoDepthLabel, &pitchEnvLabel, &pitchEnvTimeLabel })
    {
        l->setColour (juce::Label::textColourId, kPanelText);
        addAndMakeVisible (*l);
    }
    slider (crossfadeSlider, 0.0, 100.0, 1.0, " %");
    crossfadeSlider.setTooltip ("Forward loops: the end of the loop fades into its start over this much of the loop, so the join does not click");
    crossfadeSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.crossfade = static_cast<float> (crossfadeSlider.getValue() / 100.0); }); };
    slider (cutoffEnvSlider, -5.0, 5.0, 0.1, " oct");
    cutoffEnvSlider.setTooltip ("How far the envelope moves the cutoff, in octaves (needs the envelope and a filter)");
    cutoffEnvSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.cutoffEnv = static_cast<float> (cutoffEnvSlider.getValue()); }); };
    lfoBox.addItem ("Off", 1);
    lfoBox.addItem ("Pitch (vibrato)", 2);
    lfoBox.addItem ("Cutoff", 3);
    lfoBox.addItem ("Pan", 4);
    lfoBox.addItem ("Volume (tremolo)", 5);
    lfoBox.addItem ("Resonance", 6); // 0.70.0
    lfoBox.setTooltip ("What the LFO moves: the pitch (up to 2 semitones), the filter's cutoff (up to 3 octaves), the pan, the volume or the "
                       "filter's resonance");
    addAndMakeVisible (lfoBox);
    lfoBox.onChange = [this] { write ([this] (Channel& c) { c.sampler.lfoTarget = static_cast<SamplerLfoTarget> (std::clamp (lfoBox.getSelectedId() - 1, 0, 5)); }); };
    // 0.70.0: the envelope's delay and hold, and a second LFO with its own target
    for (auto* l : { &delayLabel, &holdLabel, &lfo2Label, &lfo2RateLabel, &lfo2DepthLabel })
    {
        l->setColour (juce::Label::textColourId, kPanelText);
        addAndMakeVisible (*l);
    }
    slider (delaySlider, 0.0, 5.0, 0.001, " s").setSkewFactorFromMidPoint (0.3);
    delaySlider.setTooltip ("The envelope waits this long before its attack (FL Studio's DEL)");
    delaySlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.delay = static_cast<float> (delaySlider.getValue()); }); };
    slider (holdSlider, 0.0, 5.0, 0.001, " s").setSkewFactorFromMidPoint (0.3);
    holdSlider.setTooltip ("The envelope stays at full level this long after the attack, before it decays (FL Studio's HOLD)");
    holdSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.hold = static_cast<float> (holdSlider.getValue()); }); };
    for (const char* name : { "Off", "Pitch (vibrato)", "Cutoff", "Pan", "Volume (tremolo)", "Resonance" })
        lfo2Box.addItem (name, lfo2Box.getNumItems() + 1);
    lfo2Box.setTooltip ("A second LFO with its own target, rate and depth");
    addAndMakeVisible (lfo2Box);
    lfo2Box.onChange = [this] { write ([this] (Channel& c) { c.sampler.lfo2Target = static_cast<SamplerLfoTarget> (std::clamp (lfo2Box.getSelectedId() - 1, 0, 5)); }); };
    slider (lfo2RateSlider, 0.05, 20.0, 0.05, " Hz");
    lfo2RateSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.lfo2Rate = static_cast<float> (lfo2RateSlider.getValue()); }); };
    slider (lfo2DepthSlider, 0.0, 100.0, 1.0, " %");
    lfo2DepthSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.lfo2Depth = static_cast<float> (lfo2DepthSlider.getValue() / 100.0); }); };
    slider (lfoRateSlider, 0.05, 20.0, 0.05, " Hz");
    lfoRateSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.lfoRate = static_cast<float> (lfoRateSlider.getValue()); }); };
    slider (lfoDepthSlider, 0.0, 100.0, 1.0, " %");
    lfoDepthSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.lfoDepth = static_cast<float> (lfoDepthSlider.getValue() / 100.0); }); };
    slider (pitchEnvSlider, -24.0, 24.0, 0.5, " st");
    pitchEnvSlider.setTooltip ("Pitch envelope: every note starts this many semitones away from its pitch and settles on it over Time");
    pitchEnvSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.pitchEnv = static_cast<float> (pitchEnvSlider.getValue()); }); };
    slider (pitchEnvTimeSlider, 0.005, 5.0, 0.005, " s");
    pitchEnvTimeSlider.setSkewFactorFromMidPoint (0.3);
    pitchEnvTimeSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.pitchEnvTime = static_cast<float> (pitchEnvTimeSlider.getValue()); }); };

    // 0.71.0: the envelopes of pan, cutoff, resonance and pitch
    for (auto* l : { &envLabel, &envALabel, &envDLabel, &envSLabel, &envRLabel, &envAmountLabel })
    {
        l->setColour (juce::Label::textColourId, kPanelText);
        addAndMakeVisible (*l);
    }
    for (const char* name : { "Pan", "Cutoff", "Resonance", "Pitch" })
        envTargetBox.addItem (name, envTargetBox.getNumItems() + 1);
    envTargetBox.setSelectedId (1, juce::dontSendNotification);
    envTargetBox.setTooltip ("Which envelope the controls on its right show: each of pan, cutoff, resonance and pitch has its own");
    envTargetBox.onChange = [this] { envShown = std::clamp (envTargetBox.getSelectedId() - 1, 0, 3); refresh(); };
    addAndMakeVisible (envTargetBox);
    envOnToggle.setColour (juce::ToggleButton::textColourId, kPanelText);
    envOnToggle.setTooltip ("Switch this envelope on: it moves its target from the start of each note (cutoff and resonance need the filter)");
    envOnToggle.onClick = [this] { writeEnvelope ([this] (SamplerSettings::ModEnvelope& e) { e.on = envOnToggle.getToggleState(); }); };
    addAndMakeVisible (envOnToggle);
    slider (envAttackSlider, 0.0, 10.0, 0.001, " s").setSkewFactorFromMidPoint (0.3);
    envAttackSlider.onValueChange = [this] { writeEnvelope ([this] (SamplerSettings::ModEnvelope& e) { e.attack = static_cast<float> (envAttackSlider.getValue()); }); };
    slider (envDecaySlider, 0.0, 10.0, 0.001, " s").setSkewFactorFromMidPoint (0.5);
    envDecaySlider.onValueChange = [this] { writeEnvelope ([this] (SamplerSettings::ModEnvelope& e) { e.decay = static_cast<float> (envDecaySlider.getValue()); }); };
    slider (envSustainSlider, 0.0, 100.0, 1.0, " %");
    envSustainSlider.onValueChange = [this] { writeEnvelope ([this] (SamplerSettings::ModEnvelope& e) { e.sustain = static_cast<float> (envSustainSlider.getValue() / 100.0); }); };
    slider (envReleaseSlider, 0.0, 10.0, 0.001, " s").setSkewFactorFromMidPoint (0.5);
    envReleaseSlider.onValueChange = [this] { writeEnvelope ([this] (SamplerSettings::ModEnvelope& e) { e.release = static_cast<float> (envReleaseSlider.getValue()); }); };
    slider (envAmountSlider, -100.0, 100.0, 1.0, " %");
    envAmountSlider.setTooltip ("How far the envelope moves its target: pan fully left / right, cutoff 5 octaves, resonance its whole range, "
                                "pitch 2 octaves (negative: the other way)");
    envAmountSlider.onValueChange = [this] { writeEnvelope ([this] (SamplerSettings::ModEnvelope& e) { e.amount = static_cast<float> (envAmountSlider.getValue() / 100.0); }); };

    // 0.74.0: the play mode
    for (auto* l : { &playModeLabel, &grainLabel, &densityLabel, &curveLabel, &travelTimeLabel, &grainPositionLabel, &grainSpeedLabel,
                     &grainSpreadLabel, &grainPanLabel })
    {
        l->setColour (juce::Label::textColourId, kPanelText);
        addAndMakeVisible (*l);
    }
    for (const char* name : { "Normal", "Granular (Granulizer)", "Travel curve (Wave Traveller)" })
        playModeBox.addItem (name, playModeBox.getNumItems() + 1);
    playModeBox.setTooltip ("Normal: the sample as it is. Granular: short overlapping grains read from a place that moves through the sample at "
                            "its own speed; the key sets their pitch only. Travel curve: the place in the sample follows a curve (a tape stop, a "
                            "spin back, a scratch...), then the sample plays on");
    playModeBox.onChange = [this] { write ([this] (Channel& c) { c.sampler.playMode = static_cast<SamplerSettings::PlayMode> (std::clamp (playModeBox.getSelectedId() - 1, 0, 2)); }); };
    addAndMakeVisible (playModeBox);
    for (const char* name : { "Forward", "Backward", "Tape stop", "Spin back", "Scratch", "Stutter", "Wobble" })
        curveBox.addItem (name, curveBox.getNumItems() + 1);
    curveBox.onChange = [this] { write ([this] (Channel& c) { c.sampler.travelCurve = std::clamp (curveBox.getSelectedId() - 1, 0, SamplerSettings::kTravelCurves - 1); }); };
    addAndMakeVisible (curveBox);
    slider (grainSlider, 10.0, 500.0, 1.0, " ms").setSkewFactorFromMidPoint (80.0);
    grainSlider.setTooltip ("The length of each grain");
    grainSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.grainMs = static_cast<float> (grainSlider.getValue()); }); };
    slider (densitySlider, 1.0, 8.0, 0.1, {});
    densitySlider.setTooltip ("How many grains sound at once");
    densitySlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.grainDensity = static_cast<float> (densitySlider.getValue()); }); };
    slider (travelTimeSlider, 0.05, 10.0, 0.01, " s").setSkewFactorFromMidPoint (1.0);
    travelTimeSlider.setTooltip ("How long the curve takes; then the sample plays on (a tape stop ends the note)");
    travelTimeSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.travelTime = static_cast<float> (travelTimeSlider.getValue()); }); };
    slider (grainPositionSlider, 0.0, 100.0, 0.1, " %");
    grainPositionSlider.setTooltip ("Where in the sample the grains start reading");
    grainPositionSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.grainPosition = static_cast<float> (grainPositionSlider.getValue() / 100.0); }); };
    slider (grainSpeedSlider, -200.0, 200.0, 1.0, " %");
    grainSpeedSlider.setTooltip ("How fast that place moves: 100 % the sample's own speed, 0 % frozen, negative backwards");
    grainSpeedSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.grainSpeed = static_cast<float> (grainSpeedSlider.getValue() / 100.0); }); };
    slider (grainSpreadSlider, 0.0, 100.0, 1.0, " %");
    grainSpreadSlider.setTooltip ("Each grain starts up to 200 ms away from that place, at random");
    grainSpreadSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.grainSpread = static_cast<float> (grainSpreadSlider.getValue() / 100.0); }); };
    slider (grainPanSlider, 0.0, 100.0, 1.0, " %");
    grainPanSlider.setTooltip ("Each grain's random pan");
    grainPanSlider.onValueChange = [this] { write ([this] (Channel& c) { c.sampler.grainPan = static_cast<float> (grainPanSlider.getValue() / 100.0); }); };
    for (auto* sl : { &grainPositionSlider, &grainSpeedSlider, &grainSpreadSlider, &grainPanSlider })
        sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22);
    // the one-letter labels of the envelopes' A D S R and the play mode's short names fit their narrow cells (no "...")
    for (auto* l : { &envALabel, &envDLabel, &envSLabel, &envRLabel, &grainPositionLabel, &grainSpeedLabel, &grainSpreadLabel, &grainPanLabel })
    {
        l->setBorderSize (juce::BorderSize<int> (0, 2, 0, 2));
        l->setMinimumHorizontalScale (0.7f);
    }
    setSize (600, 924);
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

void SamplerPanel::writeEnvelope (const std::function<void (SamplerSettings::ModEnvelope&)>& change)
{
    const auto shown = static_cast<size_t> (std::clamp (envShown, 0, 3));
    write ([&] (Channel& c) { change (c.sampler.modEnvelopes[shown]); });
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
    {
        // 0.74.0: the play mode (the channel's own sample only)
        const auto& smp = c->sampler;
        const bool granular = smp.playMode == SamplerSettings::PlayMode::Granular;
        const bool travels = smp.playMode == SamplerSettings::PlayMode::Traveller;
        playModeBox.setEnabled (! program);
        playModeBox.setSelectedId (static_cast<int> (smp.playMode) + 1, juce::dontSendNotification);
        for (juce::Component* x : { static_cast<juce::Component*> (&grainLabel), static_cast<juce::Component*> (&grainSlider),
                                    static_cast<juce::Component*> (&densityLabel), static_cast<juce::Component*> (&densitySlider) })
            x->setVisible (! travels);
        for (juce::Component* x : { static_cast<juce::Component*> (&curveLabel), static_cast<juce::Component*> (&curveBox),
                                    static_cast<juce::Component*> (&travelTimeLabel), static_cast<juce::Component*> (&travelTimeSlider) })
            x->setVisible (travels);
        for (auto* sl : { &grainSlider, &densitySlider, &grainPositionSlider, &grainSpeedSlider, &grainSpreadSlider, &grainPanSlider })
            sl->setEnabled (granular && ! program);
        curveBox.setSelectedId (smp.travelCurve + 1, juce::dontSendNotification);
        travelTimeSlider.setValue (smp.travelTime, juce::dontSendNotification);
        grainSlider.setValue (smp.grainMs, juce::dontSendNotification);
        densitySlider.setValue (smp.grainDensity, juce::dontSendNotification);
        grainPositionSlider.setValue (smp.grainPosition * 100.0, juce::dontSendNotification);
        grainSpeedSlider.setValue (smp.grainSpeed * 100.0, juce::dontSendNotification);
        grainSpreadSlider.setValue (smp.grainSpread * 100.0, juce::dontSendNotification);
        grainPanSlider.setValue (smp.grainPan * 100.0, juce::dontSendNotification);
    }
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
    loopBox.setSelectedId (s.loop == LoopMode::Forward ? 2 : s.loop == LoopMode::PingPong ? 3 : 1, juce::dontSendNotification);
    crossfadeSlider.setEnabled (! program && s.loop == LoopMode::Forward);
    crossfadeSlider.setValue (s.crossfade * 100.0, juce::dontSendNotification);
    cutoffEnvSlider.setEnabled (s.filter != SamplerFilter::Off && s.envelope);
    cutoffEnvSlider.setValue (s.cutoffEnv, juce::dontSendNotification);
    lfoBox.setSelectedId (static_cast<int> (s.lfoTarget) + 1, juce::dontSendNotification);
    lfoRateSlider.setEnabled (s.lfoTarget != SamplerLfoTarget::Off);
    lfoDepthSlider.setEnabled (s.lfoTarget != SamplerLfoTarget::Off);
    lfoRateSlider.setValue (s.lfoRate, juce::dontSendNotification);
    delaySlider.setEnabled (s.envelope); // 0.70.0
    holdSlider.setEnabled (s.envelope);
    delaySlider.setValue (s.delay, juce::dontSendNotification);
    holdSlider.setValue (s.hold, juce::dontSendNotification);
    lfo2Box.setSelectedId (static_cast<int> (s.lfo2Target) + 1, juce::dontSendNotification);
    {
        const auto& env = s.modEnvelopes[static_cast<size_t> (std::clamp (envShown, 0, 3))]; // 0.71.0
        envTargetBox.setSelectedId (envShown + 1, juce::dontSendNotification);
        envOnToggle.setToggleState (env.on, juce::dontSendNotification);
        for (auto* sl : { &envAttackSlider, &envDecaySlider, &envSustainSlider, &envReleaseSlider, &envAmountSlider })
            sl->setEnabled (env.on);
        envAttackSlider.setValue (env.attack, juce::dontSendNotification);
        envDecaySlider.setValue (env.decay, juce::dontSendNotification);
        envSustainSlider.setValue (env.sustain * 100.0, juce::dontSendNotification);
        envReleaseSlider.setValue (env.release, juce::dontSendNotification);
        envAmountSlider.setValue (env.amount * 100.0, juce::dontSendNotification);
    }
    lfo2RateSlider.setEnabled (s.lfo2Target != SamplerLfoTarget::Off);
    lfo2DepthSlider.setEnabled (s.lfo2Target != SamplerLfoTarget::Off);
    lfo2RateSlider.setValue (s.lfo2Rate, juce::dontSendNotification);
    lfo2DepthSlider.setValue (s.lfo2Depth * 100.0, juce::dontSendNotification);
    lfoDepthSlider.setValue (s.lfoDepth * 100.0, juce::dontSendNotification);
    pitchEnvSlider.setValue (s.pitchEnv, juce::dontSendNotification);
    pitchEnvTimeSlider.setValue (s.pitchEnvTime, juce::dontSendNotification);
    loopStartSlider.setValue (s.loopStart * 100.0, juce::dontSendNotification);
    loopEndSlider.setValue (s.loopEnd * 100.0, juce::dontSendNotification);
    filterBox.setSelectedId (static_cast<int> (s.filter) + 1, juce::dontSendNotification);
    cutoffSlider.setEnabled (s.filter != SamplerFilter::Off);
    resonanceSlider.setEnabled (s.filter != SamplerFilter::Off);
    cutoffSlider.setValue (s.cutoff, juce::dontSendNotification);
    resonanceSlider.setValue (s.resonance * 100.0, juce::dontSendNotification);
    reverseToggle.setToggleState (s.reverse, juce::dontSendNotification);
    startSlider.setValue (s.start * 100.0, juce::dontSendNotification);
    fadeInSlider.setValue (s.fadeIn * 100.0, juce::dontSendNotification);
    fadeOutSlider.setValue (s.fadeOut * 100.0, juce::dontSendNotification);
}

void SamplerPanel::paint (juce::Graphics& g)
{
    g.fillAll (kPanelBackground);
    g.setColour (kPanelDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    const char* names[] = { "SOUND", "ENVELOPE", "LOOP", "FILTER", "MODULATION", "PLAY MODE" };
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
    buttons.removeFromLeft (8);
    speechButton.setBounds (buttons); // 0.74.0

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
    {
        // 0.61.0: Reverse and Start on one row, the two fades on the next
        auto r = area.removeFromTop (26);
        area.removeFromTop (6);
        reverseToggle.setBounds (r.removeFromLeft (100));
        startLabel.setBounds (r.removeFromLeft (70));
        startSlider.setBounds (r);
        r = area.removeFromTop (26);
        area.removeFromTop (6);
        fadeInLabel.setBounds (r.removeFromLeft (100));
        fadeInSlider.setBounds (r.removeFromLeft (r.getWidth() / 2 - 4));
        r.removeFromLeft (8);
        fadeOutLabel.setBounds (r.removeFromLeft (70));
        fadeOutSlider.setBounds (r);
    }
    area.removeFromTop (4);
    headers[1] = area.removeFromTop (18);
    area.removeFromTop (4);
    envelopeToggle.setBounds (area.removeFromTop (24).withTrimmedLeft (96));
    area.removeFromTop (4);
    {
        // 0.70.0: the delay and the hold share a row
        auto r = area.removeFromTop (26);
        area.removeFromTop (6);
        delayLabel.setBounds (r.removeFromLeft (100));
        delaySlider.setBounds (r.removeFromLeft (r.getWidth() / 2 - 4));
        r.removeFromLeft (8);
        holdLabel.setBounds (r.removeFromLeft (70));
        holdSlider.setBounds (r);
    }
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
        r.removeFromLeft (16);
        crossfadeLabel.setBounds (r.removeFromLeft (70)); // 0.62.0
        crossfadeSlider.setBounds (r);
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
        r.removeFromLeft (16);
        cutoffEnvLabel.setBounds (r.removeFromLeft (70)); // 0.62.0
        cutoffEnvSlider.setBounds (r);
    }
    row (cutoffLabel, cutoffSlider);
    row (resonanceLabel, resonanceSlider);
    // 0.62.0: the LFO and the pitch envelope
    area.removeFromTop (4);
    headers[4] = area.removeFromTop (18);
    area.removeFromTop (4);
    {
        auto r = area.removeFromTop (26);
        area.removeFromTop (6);
        lfoLabel.setBounds (r.removeFromLeft (100));
        lfoBox.setBounds (r.removeFromLeft (120));
        r.removeFromLeft (8);
        lfoRateLabel.setBounds (r.removeFromLeft (36));
        lfoRateSlider.setBounds (r.removeFromLeft ((r.getWidth() - 52) / 2));
        r.removeFromLeft (8);
        lfoDepthLabel.setBounds (r.removeFromLeft (44));
        lfoDepthSlider.setBounds (r);
    }
    {
        auto r = area.removeFromTop (26); // 0.70.0
        area.removeFromTop (6);
        lfo2Label.setBounds (r.removeFromLeft (100));
        lfo2Box.setBounds (r.removeFromLeft (120));
        r.removeFromLeft (8);
        lfo2RateLabel.setBounds (r.removeFromLeft (36));
        lfo2RateSlider.setBounds (r.removeFromLeft ((r.getWidth() - 52) / 2));
        r.removeFromLeft (8);
        lfo2DepthLabel.setBounds (r.removeFromLeft (44));
        lfo2DepthSlider.setBounds (r);
    }
    {
        auto r = area.removeFromTop (26); // 0.71.0: the envelope chosen, on / off, its amount
        area.removeFromTop (6);
        envLabel.setBounds (r.removeFromLeft (100));
        envTargetBox.setBounds (r.removeFromLeft (120));
        r.removeFromLeft (8);
        envOnToggle.setBounds (r.removeFromLeft (52));
        envAmountLabel.setBounds (r.removeFromLeft (56));
        envAmountSlider.setBounds (r);
    }
    {
        auto r = area.removeFromTop (26); // its attack, decay, sustain and release
        area.removeFromTop (6);
        r.removeFromLeft (100);
        const int w = r.getWidth() / 4;
        for (auto [label, sl] : { std::pair { &envALabel, &envAttackSlider }, std::pair { &envDLabel, &envDecaySlider },
                                  std::pair { &envSLabel, &envSustainSlider }, std::pair { &envRLabel, &envReleaseSlider } })
        {
            auto cell = r.removeFromLeft (w);
            label->setBounds (cell.removeFromLeft (16));
            sl->setBounds (cell.reduced (2, 0));
        }
    }
    {
        auto r = area.removeFromTop (26);
        pitchEnvLabel.setBounds (r.removeFromLeft (100));
        pitchEnvSlider.setBounds (r.removeFromLeft (r.getWidth() / 2 - 4));
        r.removeFromLeft (8);
        pitchEnvTimeLabel.setBounds (r.removeFromLeft (70));
        pitchEnvTimeSlider.setBounds (r);
    }
    // 0.74.0: the play mode
    area.removeFromTop (10);
    headers[5] = area.removeFromTop (18);
    area.removeFromTop (4);
    {
        auto r = area.removeFromTop (26);
        area.removeFromTop (6);
        playModeLabel.setBounds (r.removeFromLeft (100));
        playModeBox.setBounds (r.removeFromLeft (150));
        r.removeFromLeft (8);
        auto first = r.removeFromLeft (r.getWidth() / 2 - 4);
        r.removeFromLeft (8);
        grainLabel.setBounds (first.removeFromLeft (40));
        grainSlider.setBounds (first);
        curveLabel.setBounds (grainLabel.getBounds());
        curveBox.setBounds (first);
        densityLabel.setBounds (r.removeFromLeft (52));
        densitySlider.setBounds (r);
        travelTimeLabel.setBounds (densityLabel.getBounds());
        travelTimeSlider.setBounds (r);
    }
    {
        auto r = area.removeFromTop (26);
        r.removeFromLeft (100);
        const int w = r.getWidth() / 4;
        for (auto [label, sl] : { std::pair { &grainPositionLabel, &grainPositionSlider }, std::pair { &grainSpeedLabel, &grainSpeedSlider },
                                  std::pair { &grainSpreadLabel, &grainSpreadSlider }, std::pair { &grainPanLabel, &grainPanSlider } })
        {
            auto cell = r.removeFromLeft (w);
            label->setBounds (cell.removeFromLeft (label == &grainPositionLabel || label == &grainPanLabel ? 28 : 44));
            sl->setBounds (cell.reduced (2, 0));
        }
    }
}

SamplerWindow::SamplerWindow()
    : juce::DocumentWindow ("Sampler", juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (false);
    // 0.70.0: the panel has grown taller than a 768-pixel screen: it sits in a viewport as tall as the screen allows.
    int available = content.getHeight();
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        available = std::max (300, display->userArea.getHeight() - 60);
    const bool scrolls = content.getHeight() > available;
    scroller.setViewedComponent (&content, false);
    scroller.setScrollBarsShown (scrolls, false);
    scroller.setSize (content.getWidth() + (scrolls ? scroller.getScrollBarThickness() : 0), std::min (content.getHeight(), available));
    setContentNonOwned (&scroller, true);
    setResizable (false, false);
    centreWithSize (scroller.getWidth(), scroller.getHeight());
    setAlwaysOnTop (true);
}
} // namespace sls
