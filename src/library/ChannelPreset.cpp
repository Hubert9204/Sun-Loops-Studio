#include "library/ChannelPreset.h"

#include <algorithm>
#include <cmath>

#include "audio/SoundLibrary.h"
#include "library/JsonRead.h"
#include "library/SoundFont.h"

namespace sls
{
namespace
{
float readFloat (const juce::var& v, float fallback, float lo, float hi)
{
    if (! (v.isDouble() || v.isInt() || v.isInt64()))
        return fallback;

    const double d = static_cast<double> (v);
    return std::isfinite (d) ? std::clamp (static_cast<float> (d), lo, hi) : fallback;
}
} // namespace

ChannelPreset ChannelPreset::fromChannel (const Channel& channel, const std::string& presetName)
{
    ChannelPreset p;
    p.name = presetName;
    p.instrument = channel.instrument;
    p.volume = channel.volume;
    p.pan = channel.pan;
    p.pitch = channel.pitch;
    p.swing = channel.swing;
    p.rootPitch = channel.rootPitch;
    if (channel.instrument == Instrument::Sampler && channel.sample != nullptr)
        p.sampleSource = channel.sample->source;
    if (channel.instrument == Instrument::Sampler)
    {
        p.sampler = channel.sampler;
        if (channel.program != nullptr && isLibraryProgramSource (channel.program->source))
            p.programSource = channel.program->source;
    }
    if (channel.instrument == Instrument::Synth)
        p.synth = channel.synth;
    return p;
}

void ChannelPreset::applyTo (Channel& channel) const
{
    channel.name = name.substr (0, 16);
    channel.instrument = instrument;
    channel.volume = std::clamp (volume, 0.0f, 1.5f);
    channel.pan = std::clamp (pan, -1.0f, 1.0f);
    channel.pitch = std::clamp (pitch, -24.0f, 24.0f);
    channel.swing = std::clamp (swing, 0.0f, 1.0f);
    channel.rootPitch = std::clamp (rootPitch, 0, 127);
    if (instrument == Instrument::Synth)
        channel.synth = synth.sanitised();
    if (instrument == Instrument::Sampler && sampler.has_value())
        channel.sampler = sampler->sanitised();
}

juce::String ChannelPreset::toJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "sun-loops-studio-channel-preset");
    o->setProperty ("version", 1);
    o->setProperty ("name", juce::String (name));
    o->setProperty ("instrument", instrument == Instrument::Sampler ? "sampler" : "synth");
    o->setProperty ("volume", static_cast<double> (volume));
    o->setProperty ("pan", static_cast<double> (pan));
    o->setProperty ("pitch", static_cast<double> (pitch));
    o->setProperty ("swing", static_cast<double> (swing));
    o->setProperty ("rootPitch", rootPitch);
    o->setProperty ("sample", juce::String (sampleSource));
    if (instrument == Instrument::Synth)
        o->setProperty ("synth", synthPatchToVar (synth));
    if (instrument == Instrument::Sampler && ! programSource.empty())
        o->setProperty ("program", juce::String (programSource));
    if (instrument == Instrument::Sampler && sampler.has_value())
        o->setProperty ("sampler", samplerSettingsToVar (*sampler));
    return juce::JSON::toString (juce::var (o), false);
}

std::optional<ChannelPreset> ChannelPreset::fromJson (const juce::String& text)
{
    const auto parsed = juce::JSON::parse (text);
    auto* o = parsed.getDynamicObject();
    if (o == nullptr || o->getProperty ("format").toString() != "sun-loops-studio-channel-preset")
        return std::nullopt;

    ChannelPreset p;
    const auto instrument = o->getProperty ("instrument").toString();
    if (instrument == "synth")
        p.instrument = Instrument::Synth;
    else if (instrument == "sampler")
        p.instrument = Instrument::Sampler;
    else
        return std::nullopt;

    const auto name = o->getProperty ("name").toString().trim();
    p.name = name.isEmpty() ? "Preset" : name.substring (0, 16).toStdString();
    p.volume = readFloat (o->getProperty ("volume"), p.volume, 0.0f, 1.5f);
    p.pan = readFloat (o->getProperty ("pan"), p.pan, -1.0f, 1.0f);
    p.pitch = readFloat (o->getProperty ("pitch"), p.pitch, -24.0f, 24.0f);
    p.swing = readFloat (o->getProperty ("swing"), p.swing, 0.0f, 1.0f);
    p.rootPitch = static_cast<int> (readFloat (o->getProperty ("rootPitch"), 60.0f, 0.0f, 127.0f));
    p.sampleSource = o->getProperty ("sample").toString().toStdString();
    p.synth = synthPatchFromVar (o->getProperty ("synth"));
    if (const auto program = o->getProperty ("program").toString().toStdString(); isLibraryProgramSource (program))
        p.programSource = program;
    if (o->hasProperty ("sampler"))
        p.sampler = samplerSettingsFromVar (o->getProperty ("sampler"));
    return p;
}

namespace
{
const char* waveKey (Waveform w)
{
    switch (w)
    {
        case Waveform::Sine:     return "sine";
        case Waveform::Triangle: return "triangle";
        case Waveform::Saw:      return "saw";
        case Waveform::Square:   return "square";
    }
    return "saw";
}

Waveform waveFromKey (const juce::String& k, Waveform fallback)
{
    return k == "sine" ? Waveform::Sine : k == "triangle" ? Waveform::Triangle : k == "saw" ? Waveform::Saw : k == "square" ? Waveform::Square : fallback;
}

const char* filterKey (SamplerFilter f)
{
    return f == SamplerFilter::LowPass ? "lowpass" : f == SamplerFilter::HighPass ? "highpass" : f == SamplerFilter::BandPass ? "bandpass" : "off";
}

SamplerFilter filterFromKey (const juce::String& k, SamplerFilter fallback)
{
    return k == "lowpass" ? SamplerFilter::LowPass : k == "highpass" ? SamplerFilter::HighPass : k == "bandpass" ? SamplerFilter::BandPass
         : k == "off" ? SamplerFilter::Off : fallback;
}

const char* targetKey (LfoTarget t)
{
    switch (t)
    {
        case LfoTarget::Pitch:      return "pitch";
        case LfoTarget::Cutoff:     return "cutoff";
        case LfoTarget::Amp:        return "amp";
        case LfoTarget::PulseWidth: return "pulsewidth";
        case LfoTarget::Off:        break;
    }
    return "off";
}

LfoTarget targetFromKey (const juce::String& k)
{
    return k == "pitch" ? LfoTarget::Pitch : k == "cutoff" ? LfoTarget::Cutoff : k == "amp" ? LfoTarget::Amp : k == "pulsewidth" ? LfoTarget::PulseWidth
         : LfoTarget::Off;
}

juce::var adsrToVar (const Adsr& a)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("attack", static_cast<double> (a.attack));
    o->setProperty ("decay", static_cast<double> (a.decay));
    o->setProperty ("sustain", static_cast<double> (a.sustain));
    o->setProperty ("release", static_cast<double> (a.release));
    return juce::var (o);
}

Adsr adsrFromVar (const juce::var& v, const Adsr& d)
{
    Adsr a = d;
    if (auto* o = v.getDynamicObject())
    {
        a.attack = static_cast<float> (readNumber (o->getProperty ("attack"), d.attack, 0.0, 10.0));
        a.decay = static_cast<float> (readNumber (o->getProperty ("decay"), d.decay, 0.0, 10.0));
        a.sustain = static_cast<float> (readNumber (o->getProperty ("sustain"), d.sustain, 0.0, 1.0));
        a.release = static_cast<float> (readNumber (o->getProperty ("release"), d.release, 0.0, 10.0));
    }
    return a;
}
} // namespace

juce::var synthPatchToVar (const SynthPatch& patch)
{
    const auto p = patch.sanitised();
    auto* o = new juce::DynamicObject();
    o->setProperty ("engine", p.engine == SynthEngine::Subtractive ? "subtractive" : p.engine == SynthEngine::FM ? "fm" : "classic");
    juce::Array<juce::var> oscs;
    for (const auto& osc : p.osc)
    {
        auto* x = new juce::DynamicObject();
        x->setProperty ("wave", waveKey (osc.wave));
        x->setProperty ("level", static_cast<double> (osc.level));
        x->setProperty ("octave", osc.octave);
        x->setProperty ("semitones", osc.semitones);
        x->setProperty ("fine", static_cast<double> (osc.fine));
        x->setProperty ("pulseWidth", static_cast<double> (osc.pulseWidth));
        oscs.add (juce::var (x));
    }
    o->setProperty ("oscillators", oscs);
    o->setProperty ("noise", static_cast<double> (p.noise));
    o->setProperty ("filter", filterKey (p.filter));
    o->setProperty ("steep", p.steep);
    o->setProperty ("cutoff", static_cast<double> (p.cutoff));
    o->setProperty ("resonance", static_cast<double> (p.resonance));
    o->setProperty ("envAmount", static_cast<double> (p.envAmount));
    o->setProperty ("keyTrack", static_cast<double> (p.keyTrack));
    o->setProperty ("amp", adsrToVar (p.amp));
    o->setProperty ("filterEnvelope", adsrToVar (p.filterEnv));
    o->setProperty ("lfoWave", waveKey (p.lfoWave));
    o->setProperty ("lfoRate", static_cast<double> (p.lfoRate));
    o->setProperty ("lfoTarget", targetKey (p.lfoTarget));
    o->setProperty ("lfoDepth", static_cast<double> (p.lfoDepth));
    o->setProperty ("gain", static_cast<double> (p.gain));

    // FM (0.19.0)
    juce::Array<juce::var> ops, rows, outs;
    for (size_t i = 0; i < p.op.size(); ++i)
    {
        auto* x = new juce::DynamicObject();
        x->setProperty ("ratio", static_cast<double> (p.op[i].ratio));
        x->setProperty ("level", static_cast<double> (p.op[i].level));
        x->setProperty ("envelope", adsrToVar (p.op[i].env));
        ops.add (juce::var (x));
        juce::Array<juce::var> row;
        for (const float m : p.matrix.fm[i])
            row.add (static_cast<double> (m));
        rows.add (row);
        outs.add (static_cast<double> (p.matrix.out[i]));
    }
    o->setProperty ("operators", ops);
    o->setProperty ("fmMatrix", rows);
    o->setProperty ("fmOutputs", outs);
    return juce::var (o);
}

SynthPatch synthPatchFromVar (const juce::var& v)
{
    const SynthPatch d;
    SynthPatch p;
    auto* o = v.getDynamicObject();
    if (o == nullptr)
        return p;
    const auto engine = o->getProperty ("engine").toString();
    p.engine = engine == "subtractive" ? SynthEngine::Subtractive : engine == "fm" ? SynthEngine::FM : SynthEngine::Classic;
    if (auto* oscs = o->getProperty ("oscillators").getArray())
        for (int i = 0; i < std::min (oscs->size(), 2); ++i)
            if (auto* x = (*oscs)[i].getDynamicObject())
            {
                auto& osc = p.osc[static_cast<size_t> (i)];
                const auto& od = d.osc[static_cast<size_t> (i)];
                osc.wave = waveFromKey (x->getProperty ("wave").toString(), od.wave);
                osc.level = static_cast<float> (readNumber (x->getProperty ("level"), od.level, 0.0, 1.0));
                osc.octave = readInt (x->getProperty ("octave"), od.octave, -3, 3);
                osc.semitones = readInt (x->getProperty ("semitones"), od.semitones, -12, 12);
                osc.fine = static_cast<float> (readNumber (x->getProperty ("fine"), od.fine, -100.0, 100.0));
                osc.pulseWidth = static_cast<float> (readNumber (x->getProperty ("pulseWidth"), od.pulseWidth, 0.05, 0.95));
            }
    p.noise = static_cast<float> (readNumber (o->getProperty ("noise"), d.noise, 0.0, 1.0));
    p.filter = filterFromKey (o->getProperty ("filter").toString(), d.filter);
    p.steep = readBool (o->getProperty ("steep"), d.steep);
    p.cutoff = static_cast<float> (readNumber (o->getProperty ("cutoff"), d.cutoff, 0.0, 1.0));
    p.resonance = static_cast<float> (readNumber (o->getProperty ("resonance"), d.resonance, 0.0, 1.0));
    p.envAmount = static_cast<float> (readNumber (o->getProperty ("envAmount"), d.envAmount, -1.0, 1.0));
    p.keyTrack = static_cast<float> (readNumber (o->getProperty ("keyTrack"), d.keyTrack, 0.0, 1.0));
    p.amp = adsrFromVar (o->getProperty ("amp"), d.amp);
    p.filterEnv = adsrFromVar (o->getProperty ("filterEnvelope"), d.filterEnv);
    p.lfoWave = waveFromKey (o->getProperty ("lfoWave").toString(), d.lfoWave);
    p.lfoRate = static_cast<float> (readNumber (o->getProperty ("lfoRate"), d.lfoRate, 0.05, 20.0));
    p.lfoTarget = targetFromKey (o->getProperty ("lfoTarget").toString());
    p.lfoDepth = static_cast<float> (readNumber (o->getProperty ("lfoDepth"), d.lfoDepth, 0.0, 1.0));
    p.gain = static_cast<float> (readNumber (o->getProperty ("gain"), d.gain, 0.0, 1.5));
    if (auto* ops = o->getProperty ("operators").getArray())
        for (int i = 0; i < std::min (ops->size(), kFmOperators); ++i)
            if (auto* x = (*ops)[i].getDynamicObject())
            {
                auto& op = p.op[static_cast<size_t> (i)];
                const auto& od = d.op[static_cast<size_t> (i)];
                op.ratio = static_cast<float> (readNumber (x->getProperty ("ratio"), od.ratio, 0.125, 32.0));
                op.level = static_cast<float> (readNumber (x->getProperty ("level"), od.level, 0.0, 1.0));
                op.env = adsrFromVar (x->getProperty ("envelope"), od.env);
            }
    if (auto* rows = o->getProperty ("fmMatrix").getArray())
        for (int t = 0; t < std::min (rows->size(), kFmOperators); ++t)
            if (auto* row = (*rows)[t].getArray())
                for (int s = 0; s < std::min (row->size(), kFmOperators); ++s)
                    p.matrix.fm[static_cast<size_t> (t)][static_cast<size_t> (s)] = static_cast<float> (readNumber ((*row)[s], 0.0, 0.0, 1.0));
    if (auto* outs = o->getProperty ("fmOutputs").getArray())
        for (int t = 0; t < std::min (outs->size(), kFmOperators); ++t)
            p.matrix.out[static_cast<size_t> (t)] = static_cast<float> (readNumber ((*outs)[t], d.matrix.out[static_cast<size_t> (t)], 0.0, 1.0));
    return p.sanitised();
}

const std::vector<ChannelPreset>& factoryPresets()
{
    static const std::vector<ChannelPreset> presets = []
    {
        std::vector<ChannelPreset> v;

        // The built-in instruments of the sound library (0.22.0): grand piano, electric bass.
        for (const auto& info : libraryPrograms())
        {
            ChannelPreset p;
            p.id = "program-" + info.id;
            p.category = info.category.empty() ? "Instruments" : info.category;
            p.name = info.name.substr (0, 16);
            p.instrument = Instrument::Sampler;
            p.volume = 0.8f;
            p.programSource = kProgramSourcePrefix + info.id;
            p.sampler = libraryProgramSettings (info.id);
            v.push_back (p);
        }

        auto sampler = [&] (const char* id, const char* name, const char* sample, float volume)
        {
            ChannelPreset p;
            p.id = id;
            p.category = "Drums";
            p.name = name;
            p.instrument = Instrument::Sampler;
            p.volume = volume;
            p.sampleSource = std::string ("builtin:") + sample;
            v.push_back (p);
        };
        sampler ("kick", "Kick", "kick", 0.95f);
        sampler ("snare", "Snare", "snare", 0.7f);
        sampler ("hat", "Hi-hat", "hat", 0.5f);

        ChannelPreset swungHat;
        swungHat.id = "swung-hat";
        swungHat.category = "Drums";
        swungHat.name = "Swung Hat";
        swungHat.instrument = Instrument::Sampler;
        swungHat.volume = 0.5f;
        swungHat.swing = 0.6f;
        swungHat.sampleSource = "builtin:hat";
        v.push_back (swungHat);

        ChannelPreset lead;
        lead.id = "lead";
        lead.category = "Synth";
        lead.name = "Soft Lead";
        lead.volume = 0.55f;
        v.push_back (lead);

        ChannelPreset bass;
        bass.id = "bass";
        bass.category = "Synth";
        bass.name = "Sub Bass";
        bass.volume = 0.9f;
        bass.pitch = -12.0f;
        v.push_back (bass);

        // The synth's ready-made sounds (subtractive 0.18.0, FM 0.19.0), as channel presets that can be dragged from the browser.
        for (const auto& named : synthFactoryPatches())
        {
            if (named.patch.engine == SynthEngine::Classic)
                continue;
            ChannelPreset p;
            p.id = "synth-" + juce::String (named.name).toLowerCase().replaceCharacter (' ', '-').toStdString();
            p.category = "Synth";
            p.name = named.name.substr (0, 16);
            p.volume = 0.7f;
            p.synth = named.patch;
            v.push_back (p);
        }

        return v;
    }();
    return presets;
}
} // namespace sls
