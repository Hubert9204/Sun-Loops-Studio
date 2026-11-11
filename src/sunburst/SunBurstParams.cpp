#include "sunburst/SunBurstParams.h"

namespace sunburst
{
namespace
{
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::NormalisableRange<float> skewed (float lo, float hi, float centre, float step = 0.0f)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    r.setSkewForCentre (centre);
    return r;
}

struct Adder
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>>& out;

    void real (const juce::String& id, const juce::String& name, const juce::NormalisableRange<float>& range, float def, const juce::String& unit = {},
               int decimals = 2)
    {
        auto attributes = juce::AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (
            [decimals, unit] (float v, int) { return juce::String (v, decimals) + (unit.isEmpty() ? juce::String() : " " + unit); });
        out.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def, attributes));
    }
    void integer (const juce::String& id, const juce::String& name, int lo, int hi, int def)
    {
        out.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, lo, hi, def));
    }
    void choice (const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
    {
        out.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def));
    }
    void toggle (const juce::String& id, const juce::String& name, bool def)
    {
        out.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    }
};

juce::String twoDigits (int n) { return juce::String (n + 1); }
} // namespace

const juce::StringArray& waveNames()
{
    static const juce::StringArray names { "Sine", "Triangle", "Saw", "Square", "Noise", "Wavetable" };
    return names;
}
const juce::StringArray& wavetableNames()
{
    static const juce::StringArray names { "Analog Morph", "Pulse Width", "Harmonic Sweep", "Formant", "Digital Bells", "Organ", "Hard Sync", "Growl",
                                           "Brass", "FM Bell", "Wavefold", "Vowel Bass", "Chord Saw", "Metallic" }; // the last six since 0.49.0
    return names;
}
const juce::StringArray& filterNames()
{
    static const juce::StringArray names { "Low-pass 12", "Low-pass 24", "High-pass 12", "High-pass 24", "Band-pass 12", "Band-pass 24", "Notch", "Peak" };
    return names;
}
const juce::StringArray& lfoShapeNames()
{
    static const juce::StringArray names { "Sine", "Triangle", "Saw up", "Saw down", "Square", "Sample & hold", "Smooth random" };
    return names;
}
const juce::StringArray& syncNames()
{
    static const juce::StringArray names { "4 bars", "2 bars", "1 bar", "1/2", "1/2T", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T", "1/32" };
    return names;
}
double syncBeats (int index)
{
    static const double beats[] { 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125 };
    return beats[std::clamp (index, 0, static_cast<int> (std::size (beats)) - 1)];
}
const juce::StringArray& sourceNames()
{
    static const juce::StringArray names { "-", "LFO 1", "LFO 2", "LFO 3", "Env 2 (filter)", "Env 3 (mod)", "Velocity", "Mod wheel", "Aftertouch",
                                           "Pitch bend", "Key track", "Random", "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    return names;
}
const juce::StringArray& destNames()
{
    static const juce::StringArray names {
        "-",
        "Osc 1 pitch", "Osc 1 WT position", "Osc 1 pulse width", "Osc 1 level", "Osc 1 detune", "Osc 1 pan",
        "Osc 2 pitch", "Osc 2 WT position", "Osc 2 pulse width", "Osc 2 level", "Osc 2 detune", "Osc 2 pan",
        "Osc 3 pitch", "Osc 3 WT position", "Osc 3 pulse width", "Osc 3 level", "Osc 3 detune", "Osc 3 pan",
        "All pitch", "Sub level", "Noise level",
        "Filter cutoff", "Filter resonance", "Filter drive", "Amp level", "Pan",
        "LFO 1 rate", "LFO 2 rate", "LFO 3 rate",
        "FX distortion drive", "FX filter cutoff", "FX chorus mix", "FX delay mix", "FX reverb mix" };
    return names;
}
const juce::StringArray& distNames()
{
    static const juce::StringArray names { "Soft", "Hard", "Tube", "Fold", "Crush" };
    return names;
}
const juce::StringArray& arpModeNames()
{
    static const juce::StringArray names { "Up", "Down", "Up / down", "Random", "As played", "Chord" };
    return names;
}
const juce::StringArray& arpRateNames()
{
    static const juce::StringArray names { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    return names;
}
double arpRateBeats (int index)
{
    static const double beats[] { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return beats[std::clamp (index, 0, static_cast<int> (std::size (beats)) - 1)];
}

juce::String oscId (int osc, const char* name) { return "o" + twoDigits (osc) + "_" + name; }
juce::String envId (int env, const char* name) { return "env" + twoDigits (env) + "_" + name; }
juce::String lfoId (int lfo, const char* name) { return "lfo" + twoDigits (lfo) + "_" + name; }
juce::String matrixId (int slot, const char* name) { return "mx" + twoDigits (slot) + "_" + name; }
juce::String macroId (int macro) { return "macro" + twoDigits (macro); }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    Adder a { params };
    for (int o = 0; o < kNumOscs; ++o)
    {
        const auto n = "Osc " + juce::String (o + 1) + " ";
        a.toggle (oscId (o, "on"), n + "on", o == 0);
        a.choice (oscId (o, "wave"), n + "wave", waveNames(), static_cast<int> (Wave::Saw));
        a.choice (oscId (o, "table"), n + "wavetable", wavetableNames(), 0);
        a.real (oscId (o, "pos"), n + "WT position", { 0.0f, 1.0f }, 0.0f);
        a.real (oscId (o, "pw"), n + "pulse width", { 0.05f, 0.95f }, 0.5f);
        a.integer (oscId (o, "oct"), n + "octave", -3, 3, 0);
        a.integer (oscId (o, "semi"), n + "semitones", -12, 12, 0);
        a.real (oscId (o, "fine"), n + "fine", { -100.0f, 100.0f }, 0.0f, "ct", 0);
        a.real (oscId (o, "level"), n + "level", { 0.0f, 1.0f }, 0.8f);
        a.real (oscId (o, "pan"), n + "pan", { -1.0f, 1.0f }, 0.0f);
        a.integer (oscId (o, "voices"), n + "unison", 1, kMaxUnison, 1);
        a.real (oscId (o, "detune"), n + "detune", { 0.0f, 1.0f }, 0.25f);
        a.real (oscId (o, "blend"), n + "blend", { 0.0f, 1.0f }, 0.5f);
        a.real (oscId (o, "spread"), n + "stereo spread", { 0.0f, 1.0f }, 0.7f);
        a.toggle (oscId (o, "rphase"), n + "random phase", true);
    }
    a.real ("sub_level", "Sub level", { 0.0f, 1.0f }, 0.0f);
    a.choice ("sub_wave", "Sub wave", { "Sine", "Square" }, 0);
    a.choice ("sub_oct", "Sub octave", { "-1", "-2" }, 0);
    a.real ("noise_level", "Noise level", { 0.0f, 1.0f }, 0.0f);
    a.real ("noise_colour", "Noise colour", { 0.0f, 1.0f }, 0.0f);

    a.toggle ("flt_on", "Filter on", true);
    a.choice ("flt_type", "Filter type", filterNames(), static_cast<int> (FilterType::LP24));
    a.real ("flt_cut", "Cutoff", skewed (20.0f, 20000.0f, 1000.0f), 9000.0f, "Hz", 0);
    a.real ("flt_res", "Resonance", { 0.0f, 1.0f }, 0.15f);
    a.real ("flt_drive", "Filter drive", { 0.0f, 1.0f }, 0.0f);
    a.real ("flt_env", "Filter env amount", { -1.0f, 1.0f }, 0.0f);
    a.real ("flt_key", "Filter key track", { 0.0f, 1.0f }, 0.5f);
    a.real ("flt_vel", "Filter velocity", { 0.0f, 1.0f }, 0.0f);

    const float envDefaults[kNumEnvs][4] { { 0.003f, 0.3f, 0.85f, 0.25f }, { 0.003f, 0.6f, 0.2f, 0.4f }, { 0.003f, 0.3f, 0.0f, 0.2f } };
    const char* envNames[kNumEnvs] { "Amp env ", "Filter env ", "Mod env " };
    for (int e = 0; e < kNumEnvs; ++e)
    {
        const juce::String n (envNames[e]);
        a.real (envId (e, "a"), n + "attack", skewed (0.0f, 10.0f, 0.5f), envDefaults[e][0], "s", 3);
        a.real (envId (e, "d"), n + "decay", skewed (0.001f, 10.0f, 0.5f), envDefaults[e][1], "s", 3);
        a.real (envId (e, "s"), n + "sustain", { 0.0f, 1.0f }, envDefaults[e][2]);
        a.real (envId (e, "r"), n + "release", skewed (0.001f, 15.0f, 0.8f), envDefaults[e][3], "s", 3);
    }
    a.real ("amp_vel", "Velocity to amp", { 0.0f, 1.0f }, 0.5f);
    a.real ("pitch_env", "Mod env to pitch", { -48.0f, 48.0f }, 0.0f, "st", 1);

    for (int l = 0; l < kNumLfos; ++l)
    {
        const auto n = "LFO " + juce::String (l + 1) + " ";
        a.choice (lfoId (l, "shape"), n + "shape", lfoShapeNames(), 0);
        a.real (lfoId (l, "rate"), n + "rate", skewed (0.01f, 40.0f, 2.0f), 2.0f, "Hz", 2);
        a.toggle (lfoId (l, "sync"), n + "tempo sync", false);
        a.choice (lfoId (l, "div"), n + "division", syncNames(), 6);
        a.toggle (lfoId (l, "retrig"), n + "retrigger", true);
        a.real (lfoId (l, "fade"), n + "fade in", skewed (0.0f, 5.0f, 1.0f), 0.0f, "s", 2);
        a.toggle (lfoId (l, "uni"), n + "unipolar", false);
    }
    for (int s = 0; s < kMatrixSlots; ++s)
    {
        const auto n = "Matrix " + juce::String (s + 1) + " ";
        a.choice (matrixId (s, "src"), n + "source", sourceNames(), 0);
        a.choice (matrixId (s, "dst"), n + "destination", destNames(), 0);
        a.real (matrixId (s, "amt"), n + "amount", { -1.0f, 1.0f }, 0.0f);
    }
    for (int m = 0; m < kNumMacros; ++m)
        a.real (macroId (m), "Macro " + juce::String (m + 1), { 0.0f, 1.0f }, 0.0f);

    a.choice ("mode", "Voice mode", { "Poly", "Mono", "Legato" }, 0);
    a.integer ("voices", "Polyphony", 1, kMaxVoices, 12);
    a.real ("glide", "Glide", skewed (0.0f, 2.0f, 0.2f), 0.0f, "s", 3);
    a.integer ("bend", "Pitch bend range", 0, 24, 2);
    a.real ("master", "Master", { -48.0f, 6.0f }, -3.0f, "dB", 1);

    a.toggle ("dist_on", "Distortion on", false);
    a.choice ("dist_type", "Distortion type", distNames(), 0);
    a.real ("dist_drive", "Distortion drive", { 0.0f, 1.0f }, 0.3f);
    a.real ("dist_mix", "Distortion mix", { 0.0f, 1.0f }, 1.0f);
    a.toggle ("eq_on", "EQ on", false);
    a.real ("eq_low", "EQ low", { -15.0f, 15.0f }, 0.0f, "dB", 1);
    a.real ("eq_mid", "EQ mid", { -15.0f, 15.0f }, 0.0f, "dB", 1);
    a.real ("eq_midf", "EQ mid frequency", skewed (200.0f, 8000.0f, 1200.0f), 1000.0f, "Hz", 0);
    a.real ("eq_high", "EQ high", { -15.0f, 15.0f }, 0.0f, "dB", 1);
    a.toggle ("ff_on", "FX filter on", false);
    a.choice ("ff_type", "FX filter type", { "Low-pass", "High-pass", "Band-pass" }, 0);
    a.real ("ff_cut", "FX filter cutoff", skewed (20.0f, 20000.0f, 1000.0f), 12000.0f, "Hz", 0);
    a.real ("ff_res", "FX filter resonance", { 0.0f, 1.0f }, 0.1f);
    a.toggle ("cho_on", "Chorus on", false);
    a.real ("cho_rate", "Chorus rate", skewed (0.05f, 5.0f, 0.8f), 0.6f, "Hz", 2);
    a.real ("cho_depth", "Chorus depth", { 0.0f, 1.0f }, 0.5f);
    a.real ("cho_mix", "Chorus mix", { 0.0f, 1.0f }, 0.35f);
    a.toggle ("dly_on", "Delay on", false);
    a.toggle ("dly_sync", "Delay tempo sync", true);
    a.choice ("dly_div", "Delay division", syncNames(), 8);
    a.real ("dly_time", "Delay time", skewed (1.0f, 2000.0f, 300.0f), 375.0f, "ms", 0);
    a.real ("dly_fb", "Delay feedback", { 0.0f, 0.95f }, 0.35f);
    a.toggle ("dly_pp", "Delay ping-pong", true);
    a.real ("dly_tone", "Delay tone", { 0.0f, 1.0f }, 0.6f);
    a.real ("dly_mix", "Delay mix", { 0.0f, 1.0f }, 0.25f);
    a.toggle ("rev_on", "Reverb on", false);
    a.real ("rev_size", "Reverb size", { 0.0f, 1.0f }, 0.6f);
    a.real ("rev_damp", "Reverb damping", { 0.0f, 1.0f }, 0.4f);
    a.real ("rev_width", "Reverb width", { 0.0f, 1.0f }, 1.0f);
    a.real ("rev_pre", "Reverb pre-delay", { 0.0f, 200.0f }, 10.0f, "ms", 0);
    a.real ("rev_mix", "Reverb mix", { 0.0f, 1.0f }, 0.25f);

    a.toggle ("arp_on", "Arpeggiator on", false);
    a.choice ("arp_mode", "Arpeggiator mode", arpModeNames(), 0);
    a.choice ("arp_rate", "Arpeggiator rate", arpRateNames(), 3);
    a.integer ("arp_oct", "Arpeggiator octaves", 1, 4, 1);
    a.real ("arp_gate", "Arpeggiator gate", { 0.05f, 1.0f }, 0.6f);

    return { params.begin(), params.end() };
}

void ParamRefs::attach (juce::AudioProcessorValueTreeState& s)
{
    const auto p = [&s] (const juce::String& id)
    {
        auto* v = s.getRawParameterValue (id);
        jassert (v != nullptr);
        return v;
    };
    for (int o = 0; o < kNumOscs; ++o)
    {
        auto& r = osc[static_cast<size_t> (o)];
        r = { p (oscId (o, "on")), p (oscId (o, "wave")), p (oscId (o, "table")), p (oscId (o, "pos")), p (oscId (o, "pw")),
              p (oscId (o, "oct")), p (oscId (o, "semi")), p (oscId (o, "fine")), p (oscId (o, "level")), p (oscId (o, "pan")),
              p (oscId (o, "voices")), p (oscId (o, "detune")), p (oscId (o, "blend")), p (oscId (o, "spread")), p (oscId (o, "rphase")) };
    }
    subLevel = p ("sub_level"); subWave = p ("sub_wave"); subOctave = p ("sub_oct");
    noiseLevel = p ("noise_level"); noiseColour = p ("noise_colour");
    filterOn = p ("flt_on"); filterType = p ("flt_type"); cutoff = p ("flt_cut"); resonance = p ("flt_res");
    filterDrive = p ("flt_drive"); filterEnv = p ("flt_env"); filterKey = p ("flt_key"); filterVelocity = p ("flt_vel");
    for (int e = 0; e < kNumEnvs; ++e)
        env[static_cast<size_t> (e)] = { p (envId (e, "a")), p (envId (e, "d")), p (envId (e, "s")), p (envId (e, "r")) };
    ampVelocity = p ("amp_vel"); pitchEnv = p ("pitch_env");
    for (int l = 0; l < kNumLfos; ++l)
        lfo[static_cast<size_t> (l)] = { p (lfoId (l, "shape")), p (lfoId (l, "rate")), p (lfoId (l, "sync")), p (lfoId (l, "div")),
                                         p (lfoId (l, "retrig")), p (lfoId (l, "fade")), p (lfoId (l, "uni")) };
    for (int m = 0; m < kMatrixSlots; ++m)
        matrix[static_cast<size_t> (m)] = { p (matrixId (m, "src")), p (matrixId (m, "dst")), p (matrixId (m, "amt")) };
    for (int m = 0; m < kNumMacros; ++m)
        macro[static_cast<size_t> (m)] = p (macroId (m));
    mode = p ("mode"); voices = p ("voices"); glide = p ("glide"); bendRange = p ("bend"); master = p ("master");
    distOn = p ("dist_on"); distType = p ("dist_type"); distDrive = p ("dist_drive"); distMix = p ("dist_mix");
    eqOn = p ("eq_on"); eqLow = p ("eq_low"); eqMid = p ("eq_mid"); eqMidFreq = p ("eq_midf"); eqHigh = p ("eq_high");
    fxFilterOn = p ("ff_on"); fxFilterType = p ("ff_type"); fxCutoff = p ("ff_cut"); fxResonance = p ("ff_res");
    chorusOn = p ("cho_on"); chorusRate = p ("cho_rate"); chorusDepth = p ("cho_depth"); chorusMix = p ("cho_mix");
    delayOn = p ("dly_on"); delaySync = p ("dly_sync"); delayDivision = p ("dly_div"); delayTime = p ("dly_time");
    delayFeedback = p ("dly_fb"); delayPingPong = p ("dly_pp"); delayTone = p ("dly_tone"); delayMix = p ("dly_mix");
    reverbOn = p ("rev_on"); reverbSize = p ("rev_size"); reverbDamping = p ("rev_damp"); reverbWidth = p ("rev_width");
    reverbPredelay = p ("rev_pre"); reverbMix = p ("rev_mix");
    arpOn = p ("arp_on"); arpMode = p ("arp_mode"); arpRate = p ("arp_rate"); arpOctaves = p ("arp_oct"); arpGate = p ("arp_gate");
}
} // namespace sunburst
