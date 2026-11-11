#include "hexforge/HexParams.h"

#include "hexforge/HexWaves.h"

namespace hexforge
{
namespace
{
juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
{
    juce::NormalisableRange<float> r (lo, hi);
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
    void env (const juce::String& prefix, const juce::String& name, float a, float d, float s, float r)
    {
        real (prefix + "_a", name + " attack", skewed (0.0f, 10.0f, 0.5f), a, "s", 3);
        real (prefix + "_d", name + " decay", skewed (0.001f, 10.0f, 0.5f), d, "s", 3);
        real (prefix + "_s", name + " sustain", { 0.0f, 1.0f }, s);
        real (prefix + "_r", name + " release", skewed (0.001f, 15.0f, 0.8f), r, "s", 3);
    }
};

juce::String one (int n) { return juce::String (n + 1); }
} // namespace

const juce::StringArray& waveNames()
{
    static const juce::StringArray names = []
    {
        juce::StringArray n;
        for (int w = 0; w < kNumClassicWaves; ++w)
            n.add (HexWaves::classicName (w));
        n.add ("Spectral A");
        n.add ("Spectral B");
        return n;
    }();
    return names;
}
const juce::StringArray& ratioNames()
{
    static const juce::StringArray names { "1/4", "1/2", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" };
    return names;
}
double ratioOf (int index)
{
    index = std::clamp (index, 0, 17);
    return index == 0 ? 0.25 : index == 1 ? 0.5 : static_cast<double> (index - 1);
}
const juce::StringArray& ladderNames()
{
    static const juce::StringArray names { "Ladder LP 24", "Ladder LP 12", "Ladder HP 24", "Ladder HP 12", "Ladder BP" };
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
    static const juce::StringArray names { "-", "Filter LFO", "LFO 2", "Filter env", "Velocity", "Mod wheel", "Aftertouch", "Key track", "Random",
                                           "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    return names;
}
const juce::StringArray& destNames()
{
    static const juce::StringArray names { "-", "Op 1 level", "Op 2 level", "Op 3 level", "Op 4 level", "Op 5 level", "Op 6 level", "FM depth", "Pitch",
                                           "Cutoff", "Resonance", "Filter drive", "Unison detune", "Pan", "Amp", "Distortion drive", "LFO 2 rate" };
    return names;
}
const juce::StringArray& distNames()
{
    static const juce::StringArray names { "Soft", "Hard", "Tube", "Fold", "Crush" };
    return names;
}

juce::String opId (int op, const char* name) { return "op" + one (op) + "_" + name; }
juce::String fmId (int target, int source) { return "fm" + one (target) + "_" + one (source); }
juce::String slotId (int slot, const char* name) { return "mx" + one (slot) + "_" + name; }
juce::String macroId (int macro) { return "macro" + one (macro); }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    Adder a { params };
    for (int o = 0; o < kNumOps; ++o)
    {
        const auto n = "Op " + one (o) + " ";
        a.choice (opId (o, "wave"), n + "wave", waveNames(), 0);
        a.choice (opId (o, "ratio"), n + "ratio", ratioNames(), 2);
        a.real (opId (o, "fine"), n + "fine", { -100.0f, 100.0f }, 0.0f, "ct", 0);
        a.real (opId (o, "level"), n + "level", { 0.0f, 1.0f }, 1.0f);
        a.real (opId (o, "out"), n + "output", { 0.0f, 1.0f }, o == 0 ? 0.8f : 0.0f);
        a.real (opId (o, "pan"), n + "pan", { -1.0f, 1.0f }, 0.0f);
        a.real (opId (o, "vel"), n + "velocity", { 0.0f, 1.0f }, 0.3f);
        a.env (opId (o, "env"), n + "env", 0.002f, 0.5f, 1.0f, 0.3f);
    }
    for (int t = 0; t < kNumOps; ++t)
        for (int s = 0; s < kNumOps; ++s)
            a.real (fmId (t, s), "FM " + one (s) + " > " + one (t), { 0.0f, 1.0f }, 0.0f);
    a.real ("fm_depth", "FM depth", { 0.0f, 2.0f }, 1.0f);
    a.integer ("uni_voices", "Unison voices", 1, kMaxUnison, 1);
    a.real ("uni_detune", "Unison detune", { 0.0f, 1.0f }, 0.2f);
    a.real ("uni_spread", "Unison spread", { 0.0f, 1.0f }, 0.7f);
    a.integer ("octave", "Octave", -3, 3, 0);
    a.real ("drop_amt", "Pitch drop", { -48.0f, 48.0f }, 0.0f, "st", 1);
    a.real ("drop_time", "Pitch drop time", skewed (0.005f, 2.0f, 0.15f), 0.08f, "s", 3);

    a.toggle ("flt_on", "Filter on", true);
    a.choice ("flt_mode", "Filter mode", ladderNames(), 0);
    a.real ("flt_cut", "Cutoff", skewed (20.0f, 20000.0f, 1000.0f), 6000.0f, "Hz", 0);
    a.real ("flt_res", "Resonance", { 0.0f, 1.0f }, 0.2f);
    a.real ("flt_drive", "Filter drive", { 0.0f, 1.0f }, 0.2f);
    a.real ("flt_env", "Filter env amount", { -1.0f, 1.0f }, 0.0f);
    a.real ("flt_key", "Filter key track", { 0.0f, 1.0f }, 0.5f);
    a.real ("flt_vel", "Filter velocity", { 0.0f, 1.0f }, 0.0f);
    a.env ("fenv", "Filter env", 0.002f, 0.4f, 0.3f, 0.3f);
    a.real ("flfo_amt", "Filter LFO amount", { -1.0f, 1.0f }, 0.0f);
    a.choice ("flfo_shape", "Filter LFO shape", lfoShapeNames(), 0);
    a.real ("flfo_rate", "Filter LFO rate", skewed (0.01f, 40.0f, 2.0f), 2.0f, "Hz", 2);
    a.toggle ("flfo_sync", "Filter LFO sync", true);
    a.choice ("flfo_div", "Filter LFO division", syncNames(), 9);
    a.choice ("lfo2_shape", "LFO 2 shape", lfoShapeNames(), 0);
    a.real ("lfo2_rate", "LFO 2 rate", skewed (0.01f, 40.0f, 2.0f), 5.0f, "Hz", 2);
    a.toggle ("lfo2_sync", "LFO 2 sync", false);
    a.choice ("lfo2_div", "LFO 2 division", syncNames(), 6);

    a.env ("aenv", "Amp env", 0.002f, 0.3f, 1.0f, 0.15f);
    a.real ("amp_vel", "Velocity to amp", { 0.0f, 1.0f }, 0.4f);
    a.choice ("mode", "Voice mode", { "Poly", "Mono", "Legato" }, 0);
    a.integer ("voices", "Polyphony", 1, kMaxVoices, 8);
    a.real ("glide", "Glide", skewed (0.0f, 2.0f, 0.2f), 0.0f, "s", 3);
    a.integer ("bend", "Pitch bend range", 0, 24, 2);
    a.real ("master", "Master", { -48.0f, 6.0f }, -6.0f, "dB", 1);

    for (int s = 0; s < kMatrixSlots; ++s)
    {
        const auto n = "Mod " + one (s) + " ";
        a.choice (slotId (s, "src"), n + "source", sourceNames(), 0);
        a.choice (slotId (s, "dst"), n + "destination", destNames(), 0);
        a.real (slotId (s, "amt"), n + "amount", { -1.0f, 1.0f }, 0.0f);
    }
    for (int m = 0; m < kNumMacros; ++m)
        a.real (macroId (m), "Macro " + one (m), { 0.0f, 1.0f }, 0.0f);

    a.toggle ("dist_on", "Distortion on", true);
    a.choice ("dist_type", "Distortion type", distNames(), 2);
    a.real ("dist_drive", "Distortion drive", { 0.0f, 1.0f }, 0.25f);
    a.real ("dist_mix", "Distortion mix", { 0.0f, 1.0f }, 1.0f);
    a.toggle ("eq_on", "EQ on", false);
    a.real ("eq_low", "EQ low", { -15.0f, 15.0f }, 0.0f, "dB", 1);
    a.real ("eq_mid", "EQ mid", { -15.0f, 15.0f }, 0.0f, "dB", 1);
    a.real ("eq_midf", "EQ mid frequency", skewed (200.0f, 8000.0f, 1200.0f), 1000.0f, "Hz", 0);
    a.real ("eq_high", "EQ high", { -15.0f, 15.0f }, 0.0f, "dB", 1);
    a.toggle ("cho_on", "Chorus on", false);
    a.real ("cho_rate", "Chorus rate", skewed (0.05f, 5.0f, 0.8f), 0.6f, "Hz", 2);
    a.real ("cho_depth", "Chorus depth", { 0.0f, 1.0f }, 0.5f);
    a.real ("cho_mix", "Chorus mix", { 0.0f, 1.0f }, 0.3f);
    a.toggle ("dly_on", "Delay on", false);
    a.choice ("dly_div", "Delay division", syncNames(), 8);
    a.real ("dly_fb", "Delay feedback", { 0.0f, 0.95f }, 0.35f);
    a.toggle ("dly_pp", "Delay ping-pong", true);
    a.real ("dly_tone", "Delay tone", { 0.0f, 1.0f }, 0.5f);
    a.real ("dly_mix", "Delay mix", { 0.0f, 1.0f }, 0.2f);
    a.toggle ("rev_on", "Reverb on", false);
    a.real ("rev_size", "Reverb size", { 0.0f, 1.0f }, 0.5f);
    a.real ("rev_damp", "Reverb damping", { 0.0f, 1.0f }, 0.5f);
    a.real ("rev_mix", "Reverb mix", { 0.0f, 1.0f }, 0.2f);
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
    const auto env = [&p] (const juce::String& prefix) { return Env { p (prefix + "_a"), p (prefix + "_d"), p (prefix + "_s"), p (prefix + "_r") }; };
    for (int o = 0; o < kNumOps; ++o)
        op[static_cast<size_t> (o)] = { p (opId (o, "wave")), p (opId (o, "ratio")), p (opId (o, "fine")), p (opId (o, "level")), p (opId (o, "out")),
                                        p (opId (o, "pan")), p (opId (o, "vel")), env (opId (o, "env")) };
    for (int t = 0; t < kNumOps; ++t)
        for (int src = 0; src < kNumOps; ++src)
            fm[static_cast<size_t> (t)][static_cast<size_t> (src)] = p (fmId (t, src));
    fmDepth = p ("fm_depth"); unisonVoices = p ("uni_voices"); unisonDetune = p ("uni_detune"); unisonSpread = p ("uni_spread");
    octave = p ("octave"); dropAmount = p ("drop_amt"); dropTime = p ("drop_time");
    filterOn = p ("flt_on"); filterMode = p ("flt_mode"); cutoff = p ("flt_cut"); resonance = p ("flt_res"); drive = p ("flt_drive");
    filterEnvAmount = p ("flt_env"); filterKey = p ("flt_key"); filterVelocity = p ("flt_vel");
    filterEnv = env ("fenv");
    ampEnv = env ("aenv");
    filterLfoAmount = p ("flfo_amt"); filterLfoShape = p ("flfo_shape"); filterLfoRate = p ("flfo_rate"); filterLfoSync = p ("flfo_sync");
    filterLfoDivision = p ("flfo_div");
    lfo2Shape = p ("lfo2_shape"); lfo2Rate = p ("lfo2_rate"); lfo2Sync = p ("lfo2_sync"); lfo2Division = p ("lfo2_div");
    ampVelocity = p ("amp_vel"); mode = p ("mode"); voices = p ("voices"); glide = p ("glide"); bendRange = p ("bend"); master = p ("master");
    for (int m = 0; m < kMatrixSlots; ++m)
        matrix[static_cast<size_t> (m)] = { p (slotId (m, "src")), p (slotId (m, "dst")), p (slotId (m, "amt")) };
    for (int m = 0; m < kNumMacros; ++m)
        macro[static_cast<size_t> (m)] = p (macroId (m));
    distOn = p ("dist_on"); distType = p ("dist_type"); distDrive = p ("dist_drive"); distMix = p ("dist_mix");
    eqOn = p ("eq_on"); eqLow = p ("eq_low"); eqMid = p ("eq_mid"); eqMidFreq = p ("eq_midf"); eqHigh = p ("eq_high");
    chorusOn = p ("cho_on"); chorusRate = p ("cho_rate"); chorusDepth = p ("cho_depth"); chorusMix = p ("cho_mix");
    delayOn = p ("dly_on"); delayDivision = p ("dly_div"); delayFeedback = p ("dly_fb"); delayPingPong = p ("dly_pp"); delayTone = p ("dly_tone");
    delayMix = p ("dly_mix");
    reverbOn = p ("rev_on"); reverbSize = p ("rev_size"); reverbDamping = p ("rev_damp"); reverbMix = p ("rev_mix");
}
} // namespace hexforge
