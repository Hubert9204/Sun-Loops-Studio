#include "library/SoundFont.h"

#include "library/JsonRead.h"
#include "audio/SoundLibrary.h"
#include "library/Sfz.h"

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
    p->name = spec.kind == ProgramSpec::Kind::Builtin ? spec.file.substr (std::string (kProgramSourcePrefix).size())
                                                      : juce::File (juce::String (spec.file)).getFileNameWithoutExtension().toStdString();
    if (spec.kind == ProgramSpec::Kind::SoundFont)
        p->preset = spec.preset;
    else if (spec.kind == ProgramSpec::Kind::Slices)
    {
        p->sliceStarts = spec.starts.empty() ? std::vector<int> { 0 } : spec.starts;
        p->sliceFirstKey = spec.firstKey;
        p->sliceSettings = spec.sliceSettings;
        p->sliceCut = spec.sliceCut;
        p->sliceBpm = spec.sliceBpm;
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
    if (program.sliceStarts.empty() && isLibraryProgramSource (program.source))
    {
        spec.kind = ProgramSpec::Kind::Builtin;
        return spec;
    }
    if (program.sliceStarts.empty() && juce::File (juce::String (program.source)).hasFileExtension ("sfz"))
    {
        spec.kind = ProgramSpec::Kind::Sfz;
        return spec;
    }
    spec.kind = program.sliceStarts.empty() || program.source.empty() ? ProgramSpec::Kind::None : ProgramSpec::Kind::Slices;
    spec.firstKey = program.sliceFirstKey;
    spec.starts = program.sliceStarts;
    spec.sliceSettings = program.sliceSettings;
    spec.sliceCut = program.sliceCut;
    spec.sliceBpm = program.sliceBpm;
    return spec;
}

juce::var programSpecToVar (const ProgramSpec& spec)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("kind", spec.kind == ProgramSpec::Kind::SoundFont ? "soundfont" : spec.kind == ProgramSpec::Kind::Slices ? "slices"
                                : spec.kind == ProgramSpec::Kind::Builtin ? "builtin" : spec.kind == ProgramSpec::Kind::Sfz ? "sfz" : "none");
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
        if (! spec.sliceSettings.empty())
        {
            juce::Array<juce::var> list;
            for (const auto& s : spec.sliceSettings)
            {
                auto* so = new juce::DynamicObject();
                if (std::abs (s.tune) > 0.0f)
                    so->setProperty ("tune", s.tune);
                if (std::abs (s.gain - 1.0f) > 0.0f)
                    so->setProperty ("gain", s.gain);
                if (std::abs (s.pan) > 0.0f)
                    so->setProperty ("pan", s.pan);
                if (s.reverse)
                    so->setProperty ("reverse", true);
                list.add (juce::var (so));
            }
            o->setProperty ("slices", list);
        }
        if (spec.sliceCut)
            o->setProperty ("cut", true);
        if (spec.sliceBpm > 0.0)
            o->setProperty ("bpm", spec.sliceBpm);
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
        if (auto* slices = o->getProperty ("slices").getArray())
            for (int i = 0; i < std::min (slices->size(), 128); ++i)
            {
                const auto& item = (*slices)[i];
                SliceSettings s;
                if (auto* so = item.getDynamicObject())
                {
                    s.tune = static_cast<float> (readNumber (so->getProperty ("tune"), 0.0, -24.0, 24.0));
                    s.gain = static_cast<float> (readNumber (so->getProperty ("gain"), 1.0, 0.0, 2.0));
                    s.pan = static_cast<float> (readNumber (so->getProperty ("pan"), 0.0, -1.0, 1.0));
                    s.reverse = readBool (so->getProperty ("reverse"), false);
                }
                spec.sliceSettings.push_back (s);
            }
        spec.sliceCut = readBool (o->getProperty ("cut"), false);
        spec.sliceBpm = readNumber (o->getProperty ("bpm"), 0.0, 0.0, 999.0);
    }
    else if (kind == "sfz")
        spec.kind = ProgramSpec::Kind::Sfz;
    else if (kind == "builtin")
        spec.kind = isLibraryProgramSource (spec.file) ? ProgramSpec::Kind::Builtin : ProgramSpec::Kind::None;
    if (spec.file.empty())
        spec.kind = ProgramSpec::Kind::None;
    return spec;
}

std::shared_ptr<const SamplerProgram> loadSfz (const juce::File& file,
                                               const std::function<std::shared_ptr<const SampleData> (const juce::File&, juce::String&)>& loadAudio,
                                               juce::String& error, juce::StringArray* notes)
{
    if (! file.existsAsFile() || file.getSize() > 16LL * 1024 * 1024)
    {
        error = file.existsAsFile() ? "The SFZ file is too large: " + file.getFileName() : "Cannot find " + file.getFullPathName();
        return nullptr;
    }
    const auto instrument = sfz::parse (file.loadFileAsString().toStdString());
    auto program = std::make_shared<SamplerProgram>();
    program->name = file.getFileNameWithoutExtension().toStdString();
    program->source = file.getFullPathName().toStdString();

    std::map<std::string, std::shared_ptr<const SampleData>> samples; // each file read once
    int missing = 0;
    for (const auto& r : instrument.regions)
    {
        const auto path = file.getParentDirectory().getChildFile (juce::String::fromUTF8 (r.sample.c_str())).getFullPathName().toStdString();
        auto it = samples.find (path);
        if (it == samples.end())
        {
            juce::String why;
            it = samples.emplace (path, loadAudio != nullptr ? loadAudio (juce::File (juce::String::fromUTF8 (path.c_str())), why) : nullptr).first;
            if (it->second == nullptr)
            {
                ++missing;
                if (notes != nullptr)
                    notes->addIfNotAlreadyThere ("Sample not found or not readable: " + juce::String::fromUTF8 (r.sample.c_str()));
            }
        }
        if (it->second == nullptr || it->second->numFrames() < 2)
            continue;
        SampleZone z;
        z.sample = it->second;
        z.loKey = r.loKey;
        z.hiKey = r.hiKey;
        z.rootKey = r.rootKey;
        z.loVel = r.loVel;
        z.hiVel = r.hiVel;
        z.tune = static_cast<float> (r.tuneCents / 100.0);
        z.gain = static_cast<float> (std::pow (10.0, r.volumeDb / 20.0));
        z.pan = static_cast<float> (r.pan / 100.0);
        if (r.loop && r.loopEnd > r.loopStart && r.loopStart >= 0)
        {
            z.loop = LoopMode::Forward;
            z.loopStart = static_cast<int> (std::min<long long> (r.loopStart, z.sample->numFrames() - 1));
            z.loopEnd = static_cast<int> (std::min<long long> (r.loopEnd + 1, z.sample->numFrames())); // SFZ's loop_end is the last frame
        }
        program->zones.push_back (std::move (z));
    }
    if (notes != nullptr)
    {
        if (instrument.skippedOpcodes > 0)
            notes->add (instrument.skippedOpcodes == 1 ? juce::String ("1 setting this sampler does not have was left out (filters, envelopes, round robin...)")
                                                       : juce::String (instrument.skippedOpcodes) + " settings this sampler does not have were left out (filters, envelopes, round robin...)");
        for (const auto& inc : instrument.includes)
            notes->add ("#include not followed: " + juce::String::fromUTF8 (inc.c_str()));
    }
    auto sane = std::make_shared<SamplerProgram> (program->sanitised());
    if (sane->zones.empty())
    {
        error = instrument.regions.empty() ? "No regions with samples in " + file.getFileName()
                                           : "None of the " + juce::String (static_cast<int> (instrument.regions.size())) + " regions' samples could be read ("
                                                 + juce::String (missing) + " files missing)";
        return nullptr;
    }
    return sane;
}

std::shared_ptr<const SamplerProgram> loadProgram (const ProgramSpec& spec, const std::function<std::shared_ptr<const SampleData> (const juce::File&, juce::String&)>& loadAudio,
                                                   juce::String& error)
{
    if (spec.empty())
    {
        error = "No program.";
        return nullptr;
    }
    if (spec.kind == ProgramSpec::Kind::Builtin)
    {
        std::string message;
        auto program = makeLibraryProgram (spec.file.substr (std::string (kProgramSourcePrefix).size()), nullptr, message);
        error = message;
        return program;
    }
    const juce::File file { juce::String (spec.file) };
    if (spec.kind == ProgramSpec::Kind::SoundFont)
        return loadSoundFont (file, spec.preset, error);
    if (spec.kind == ProgramSpec::Kind::Sfz)
        return loadSfz (file, loadAudio, error);

    const auto audio = loadAudio != nullptr ? loadAudio (file, error) : nullptr;
    if (audio == nullptr)
    {
        if (error.isEmpty())
            error = "Cannot read " + file.getFullPathName();
        return nullptr;
    }
    auto program = std::make_shared<SamplerProgram> (
        makeSliceProgram (audio, spec.starts, spec.firstKey, file.getFileNameWithoutExtension().toStdString(), spec.sliceSettings).sanitised());
    program->source = spec.file;
    program->sliceStarts = spec.starts.empty() ? std::vector<int> { 0 } : spec.starts;
    program->sliceFirstKey = spec.firstKey;
    program->sliceCut = spec.sliceCut;
    program->sliceBpm = spec.sliceBpm;
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
    o->setProperty ("loop", s.loop == LoopMode::Forward ? "forward" : s.loop == LoopMode::PingPong ? "pingpong" : "off");
    // 0.62.0
    if (s.crossfade > 0.0f)
        o->setProperty ("crossfade", s.crossfade);
    if (s.lfoTarget != SamplerLfoTarget::Off)
    {
        static const char* const targets[] = { "off", "pitch", "cutoff", "pan", "volume" };
        o->setProperty ("lfoTarget", targets[static_cast<int> (s.lfoTarget)]);
        o->setProperty ("lfoRate", s.lfoRate);
        o->setProperty ("lfoDepth", s.lfoDepth);
    }
    if (std::abs (s.cutoffEnv) > 0.0f)
        o->setProperty ("cutoffEnv", s.cutoffEnv);
    if (std::abs (s.pitchEnv) > 0.0f)
    {
        o->setProperty ("pitchEnv", s.pitchEnv);
        o->setProperty ("pitchEnvTime", s.pitchEnvTime);
    }
    o->setProperty ("loopStart", s.loopStart);
    o->setProperty ("loopEnd", s.loopEnd);
    o->setProperty ("filter", s.filter == SamplerFilter::LowPass ? "lowpass" : s.filter == SamplerFilter::HighPass ? "highpass"
                                : s.filter == SamplerFilter::BandPass ? "bandpass" : "off");
    o->setProperty ("cutoff", s.cutoff);
    o->setProperty ("resonance", s.resonance);
    if (s.reverse) // 0.61.0
        o->setProperty ("reverse", true);
    if (s.start > 0.0f)
        o->setProperty ("start", s.start);
    if (s.fadeIn > 0.0f)
        o->setProperty ("fadeIn", s.fadeIn);
    if (s.fadeOut > 0.0f)
        o->setProperty ("fadeOut", s.fadeOut);
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
    const auto loopText = o->getProperty ("loop").toString();
    s.loop = loopText == "forward" ? LoopMode::Forward : loopText == "pingpong" ? LoopMode::PingPong : LoopMode::Off;
    s.crossfade = static_cast<float> (readNumber (o->getProperty ("crossfade"), 0.0, 0.0, 1.0)); // 0.62.0
    const auto lfoText = o->getProperty ("lfoTarget").toString();
    s.lfoTarget = lfoText == "pitch" ? SamplerLfoTarget::Pitch : lfoText == "cutoff" ? SamplerLfoTarget::Cutoff : lfoText == "pan" ? SamplerLfoTarget::Pan
                  : lfoText == "volume" ? SamplerLfoTarget::Volume : SamplerLfoTarget::Off;
    s.lfoRate = static_cast<float> (readNumber (o->getProperty ("lfoRate"), 5.0, 0.05, 20.0));
    s.lfoDepth = static_cast<float> (readNumber (o->getProperty ("lfoDepth"), 0.5, 0.0, 1.0));
    s.cutoffEnv = static_cast<float> (readNumber (o->getProperty ("cutoffEnv"), 0.0, -5.0, 5.0));
    s.pitchEnv = static_cast<float> (readNumber (o->getProperty ("pitchEnv"), 0.0, -24.0, 24.0));
    s.pitchEnvTime = static_cast<float> (readNumber (o->getProperty ("pitchEnvTime"), 0.1, 0.005, 5.0));
    s.loopStart = static_cast<float> (readNumber (o->getProperty ("loopStart"), 0.0, 0.0, 1.0));
    s.loopEnd = static_cast<float> (readNumber (o->getProperty ("loopEnd"), 1.0, 0.0, 1.0));
    const auto filter = o->getProperty ("filter").toString();
    s.filter = filter == "lowpass" ? SamplerFilter::LowPass : filter == "highpass" ? SamplerFilter::HighPass : filter == "bandpass" ? SamplerFilter::BandPass : SamplerFilter::Off;
    s.cutoff = static_cast<float> (readNumber (o->getProperty ("cutoff"), 1.0, 0.0, 1.0));
    s.resonance = static_cast<float> (readNumber (o->getProperty ("resonance"), 0.0, 0.0, 1.0));
    s.reverse = readBool (o->getProperty ("reverse"), false); // 0.61.0
    s.start = static_cast<float> (readNumber (o->getProperty ("start"), 0.0, 0.0, 0.99));
    s.fadeIn = static_cast<float> (readNumber (o->getProperty ("fadeIn"), 0.0, 0.0, 1.0));
    s.fadeOut = static_cast<float> (readNumber (o->getProperty ("fadeOut"), 0.0, 0.0, 1.0));
    return s.sanitised();
}
} // namespace sls
