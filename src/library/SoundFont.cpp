#include "library/SoundFont.h"

#include "library/JsonRead.h"

#include <algorithm>
#include <cmath>
#include <map>

#if defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wcast-align"
 #pragma GCC diagnostic ignored "-Wfloat-equal"
 #pragma GCC diagnostic ignored "-Wold-style-cast"
 #pragma GCC diagnostic ignored "-Wuseless-cast"
 #pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#define TSF_IMPLEMENTATION
#define TSF_STATIC
#include "tsf.h"
#if defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif

namespace sls
{
namespace
{
struct TsfDeleter
{
    void operator() (tsf* f) const noexcept { tsf_close (f); }
};
using TsfPtr = std::unique_ptr<tsf, TsfDeleter>;

TsfPtr openFile (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "The file " + file.getFullPathName() + " does not exist.";
        return {};
    }
    if (file.getSize() > kMaxSoundFontBytes)
    {
        error = file.getFileName() + " is larger than " + juce::String (kMaxSoundFontBytes / (1024LL * 1024)) + " MB.";
        return {};
    }
    TsfPtr f (tsf_load_filename (file.getFullPathName().toRawUTF8()));
    if (f == nullptr)
        error = file.getFileName() + " is not a SoundFont 2 file, or it is damaged.";
    return f;
}
} // namespace

std::vector<SoundFontPreset> listSoundFontPresets (const juce::File& file, juce::String& error)
{
    std::vector<SoundFontPreset> list;
    const auto f = openFile (file, error);
    if (f == nullptr)
        return list;
    for (int i = 0; i < f->presetNum; ++i)
    {
        SoundFontPreset p;
        p.index = i;
        p.bank = f->presets[i].bank;
        p.number = f->presets[i].preset;
        p.name = juce::String (juce::CharPointer_UTF8 (f->presets[i].presetName), 20).trim().toStdString();
        if (p.name.empty())
            p.name = "Preset " + std::to_string (p.number);
        list.push_back (std::move (p));
    }
    std::sort (list.begin(), list.end(), [] (const SoundFontPreset& a, const SoundFontPreset& b)
               { return a.bank != b.bank ? a.bank < b.bank : a.number != b.number ? a.number < b.number : a.index < b.index; });
    if (list.empty())
        error = file.getFileName() + " has no presets.";
    return list;
}

std::shared_ptr<const SamplerProgram> loadSoundFont (const juce::File& file, int presetIndex, juce::String& error, SamplerSettings* suggested)
{
    const auto f = openFile (file, error);
    if (f == nullptr)
        return nullptr;
    if (presetIndex < 0 || presetIndex >= f->presetNum)
    {
        error = file.getFileName() + " has no preset " + juce::String (presetIndex + 1) + ".";
        return nullptr;
    }

    const auto& preset = f->presets[presetIndex];
    SamplerProgram program;
    program.name = juce::String (juce::CharPointer_UTF8 (preset.presetName), 20).trim().toStdString();
    program.source = file.getFullPathName().toStdString();
    program.preset = presetIndex;

    std::map<std::pair<unsigned int, unsigned int>, std::shared_ptr<const SampleData>> samples; // one copy per sample, shared by its zones
    const tsf_region* first = nullptr;
    for (int i = 0; i < preset.regionNum; ++i)
    {
        const auto& r = preset.regions[i];
        if (r.end <= r.offset + 1 || r.sample_rate < 1000 || r.sample_rate > 384000)
            continue;
        auto& data = samples[{ r.offset, r.end }];
        if (data == nullptr)
        {
            auto s = std::make_shared<SampleData>();
            s->left.assign (f->fontSamples + r.offset, f->fontSamples + r.end);
            s->right = s->left;
            s->sampleRate = static_cast<double> (r.sample_rate);
            s->name = program.name;
            s->source = program.source;
            data = std::move (s);
        }

        SampleZone z;
        z.sample = data;
        z.rootKey = r.pitch_keycenter >= 0 ? r.pitch_keycenter : 60;
        z.keyTrack = static_cast<float> (r.pitch_keytrack) / 100.0f;
        z.tune = (static_cast<float> (r.transpose) + static_cast<float> (r.tune) / 100.0f) * std::clamp (z.keyTrack, 0.0f, 1.0f);
        z.loKey = r.lokey;
        z.hiKey = r.hikey;
        z.loVel = std::max (1, static_cast<int> (r.lovel));
        z.hiVel = r.hivel;
        // TinySoundFont keeps initialAttenuation as centibels / 100. Like EMU hardware and FluidSynth, 0.4 dB per 10 cB is applied.
        z.gain = static_cast<float> (std::pow (10.0, -4.0 * static_cast<double> (r.attenuation) / 20.0));
        z.pan = std::clamp (r.pan * 2.0f, -1.0f, 1.0f);
        if (r.loop_mode != TSF_LOOPMODE_NONE && r.loop_end > r.loop_start && r.loop_start >= r.offset && r.loop_end < r.end)
        {
            z.loop = LoopMode::Forward;
            z.loopStart = static_cast<int> (r.loop_start - r.offset);
            z.loopEnd = static_cast<int> (r.loop_end - r.offset) + 1; // TinySoundFont's loop end is the last sample of the loop
        }
        program.zones.push_back (std::move (z));
        if (first == nullptr)
            first = &r;
    }

    auto clean = std::make_shared<SamplerProgram> (program.sanitised());
    if (clean->zones.empty())
    {
        error = "The preset \"" + juce::String (program.name) + "\" holds no sound that can be played.";
        return nullptr;
    }
    if (suggested != nullptr && first != nullptr)
    {
        SamplerSettings s;
        s.envelope = true;
        s.attack = first->ampenv.delay + first->ampenv.attack;
        s.decay = first->ampenv.hold + first->ampenv.decay;
        s.sustain = first->ampenv.sustain;
        s.release = std::max (0.01f, first->ampenv.release);
        *suggested = s.sanitised();
    }
    return clean;
}

std::shared_ptr<const SamplerProgram> missingProgram (const ProgramSpec& spec)
{
    auto p = std::make_shared<SamplerProgram>();
    p->source = spec.file;
    p->name = juce::File (juce::String (spec.file)).getFileNameWithoutExtension().toStdString();
    if (spec.kind == ProgramSpec::Kind::SoundFont)
        p->preset = spec.preset;
    else if (spec.kind == ProgramSpec::Kind::Slices)
    {
        p->sliceStarts = spec.starts.empty() ? std::vector<int> { 0 } : spec.starts;
        p->sliceFirstKey = spec.firstKey;
    }
    return p;
}

ProgramSpec specFromProgram (const SamplerProgram& program)
{
    ProgramSpec spec;
    spec.file = program.source;
    if (program.preset >= 0)
    {
        spec.kind = ProgramSpec::Kind::SoundFont;
        spec.preset = program.preset;
        return spec;
    }
    spec.kind = program.sliceStarts.empty() || program.source.empty() ? ProgramSpec::Kind::None : ProgramSpec::Kind::Slices;
    spec.firstKey = program.sliceFirstKey;
    spec.starts = program.sliceStarts;
    return spec;
}

juce::var programSpecToVar (const ProgramSpec& spec)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("kind", spec.kind == ProgramSpec::Kind::SoundFont ? "soundfont" : spec.kind == ProgramSpec::Kind::Slices ? "slices" : "none");
    o->setProperty ("file", juce::String (spec.file));
    if (spec.kind == ProgramSpec::Kind::SoundFont)
        o->setProperty ("preset", spec.preset);
    if (spec.kind == ProgramSpec::Kind::Slices)
    {
        juce::Array<juce::var> starts;
        for (const int f : spec.starts)
            starts.add (f);
        o->setProperty ("starts", starts);
        o->setProperty ("firstKey", spec.firstKey);
    }
    return juce::var (o);
}

ProgramSpec programSpecFromVar (const juce::var& v)
{
    ProgramSpec spec;
    auto* o = v.getDynamicObject();
    if (o == nullptr)
        return spec;
    const auto kind = o->getProperty ("kind").toString();
    spec.file = o->getProperty ("file").toString().toStdString();
    if (kind == "soundfont")
    {
        spec.kind = ProgramSpec::Kind::SoundFont;
        spec.preset = readInt (o->getProperty ("preset"), -1, -1, 65535);
        if (spec.preset < 0)
            spec.kind = ProgramSpec::Kind::None;
    }
    else if (kind == "slices")
    {
        spec.kind = ProgramSpec::Kind::Slices;
        spec.firstKey = readInt (o->getProperty ("firstKey"), 60, 0, 127);
        if (auto* starts = o->getProperty ("starts").getArray())
            for (int i = 0; i < std::min (starts->size(), 128); ++i)
                spec.starts.push_back (readInt ((*starts)[i], 0, 0, 1 << 30));
    }
    if (spec.file.empty())
        spec.kind = ProgramSpec::Kind::None;
    return spec;
}

std::shared_ptr<const SamplerProgram> loadProgram (const ProgramSpec& spec, const std::function<std::shared_ptr<const SampleData> (const juce::File&, juce::String&)>& loadAudio,
                                                   juce::String& error)
{
    if (spec.empty())
    {
        error = "No program.";
        return nullptr;
    }
    const juce::File file { juce::String (spec.file) };
    if (spec.kind == ProgramSpec::Kind::SoundFont)
        return loadSoundFont (file, spec.preset, error);

    const auto audio = loadAudio != nullptr ? loadAudio (file, error) : nullptr;
    if (audio == nullptr)
    {
        if (error.isEmpty())
            error = "Cannot read " + file.getFullPathName();
        return nullptr;
    }
    auto program = std::make_shared<SamplerProgram> (makeSliceProgram (audio, spec.starts, spec.firstKey, file.getFileNameWithoutExtension().toStdString()).sanitised());
    program->source = spec.file;
    program->sliceStarts = spec.starts.empty() ? std::vector<int> { 0 } : spec.starts;
    program->sliceFirstKey = spec.firstKey;
    if (program->zones.empty())
    {
        error = "No slices could be made from " + file.getFileName();
        return nullptr;
    }
    return program;
}

juce::var samplerSettingsToVar (const SamplerSettings& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("envelope", s.envelope);
    o->setProperty ("attack", s.attack);
    o->setProperty ("decay", s.decay);
    o->setProperty ("sustain", s.sustain);
    o->setProperty ("release", s.release);
    o->setProperty ("loop", s.loop == LoopMode::Forward ? "forward" : "off");
    o->setProperty ("loopStart", s.loopStart);
    o->setProperty ("loopEnd", s.loopEnd);
    o->setProperty ("filter", s.filter == SamplerFilter::LowPass ? "lowpass" : s.filter == SamplerFilter::HighPass ? "highpass"
                                : s.filter == SamplerFilter::BandPass ? "bandpass" : "off");
    o->setProperty ("cutoff", s.cutoff);
    o->setProperty ("resonance", s.resonance);
    return juce::var (o);
}

SamplerSettings samplerSettingsFromVar (const juce::var& v)
{
    SamplerSettings s;
    auto* o = v.getDynamicObject();
    if (o == nullptr)
        return s;
    s.envelope = readBool (o->getProperty ("envelope"), false);
    s.attack = static_cast<float> (readNumber (o->getProperty ("attack"), s.attack, 0.0, 10.0));
    s.decay = static_cast<float> (readNumber (o->getProperty ("decay"), s.decay, 0.0, 10.0));
    s.sustain = static_cast<float> (readNumber (o->getProperty ("sustain"), s.sustain, 0.0, 1.0));
    s.release = static_cast<float> (readNumber (o->getProperty ("release"), s.release, 0.0, 10.0));
    s.loop = o->getProperty ("loop").toString() == "forward" ? LoopMode::Forward : LoopMode::Off;
    s.loopStart = static_cast<float> (readNumber (o->getProperty ("loopStart"), 0.0, 0.0, 1.0));
    s.loopEnd = static_cast<float> (readNumber (o->getProperty ("loopEnd"), 1.0, 0.0, 1.0));
    const auto filter = o->getProperty ("filter").toString();
    s.filter = filter == "lowpass" ? SamplerFilter::LowPass : filter == "highpass" ? SamplerFilter::HighPass : filter == "bandpass" ? SamplerFilter::BandPass : SamplerFilter::Off;
    s.cutoff = static_cast<float> (readNumber (o->getProperty ("cutoff"), 1.0, 0.0, 1.0));
    s.resonance = static_cast<float> (readNumber (o->getProperty ("resonance"), 0.0, 0.0, 1.0));
    return s.sanitised();
}
} // namespace sls
