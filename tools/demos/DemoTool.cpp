// sun-loops-demo-tool: a console helper for making and checking the demo projects (resources/demos).
//
//   DemoTool render <project.slp> <out.wav> [--stems <folder>]   renders the song as the export does, and prints its levels
//   DemoTool catalog <out.json>                                  what a demo can use: synth patches, sampler instruments,
//                                                                kits, built-in sounds, SunBurst / HexForge presets (with their
//                                                                saved state) and parameter names
//   DemoTool sunrise <out.slp>                                   writes the built-in demo song ("Sunrise Drive") as a project file
//
// The program's own plugins are looked for next to the build (build/<Plugin>_artefacts/Release/VST3), where a normal build puts them.
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/AudioExport.h"
#include "audio/AudioImport.h"
#include "audio/BuiltinSamples.h"
#include "audio/Loudness.h"
#include "audio/OfflineRenderer.h"
#include "audio/Player.h"
#include "audio/SoundLibrary.h"
#include "library/ChannelPreset.h"
#include "library/DemoSong.h"
#include "library/DrumKitFile.h"
#include "library/ProjectFile.h"
#include "library/SoundFont.h"
#include "plugins/PluginHost.h"

using namespace sls;

namespace
{
juce::File buildFolder;

void addOwnPlugins (PluginHost& host)
{
    for (auto* format : host.formats().getFormats())
        if (format->getName() == "VST3")
            for (const char* name : { "SunBurst", "HexForge" })
            {
                const auto bundle = buildFolder.getChildFile (juce::String (name) + "_artefacts/Release/VST3/" + name + ".vst3");
                juce::OwnedArray<juce::PluginDescription> types;
                format->findAllTypesForFile (types, bundle.getFullPathName());
                for (auto* t : types)
                    host.known().addType (*t);
                if (types.isEmpty())
                    std::cerr << "warning: " << bundle.getFullPathName() << " not found\n";
            }
}

std::shared_ptr<JucePlugin> makeOwn (PluginHost& host, const juce::String& name)
{
    for (const auto& d : host.known().getTypes())
        if (d.name == name)
        {
            juce::String error;
            auto p = host.create (d, 48000.0, Player::kChunk, error);
            if (p == nullptr)
                std::cerr << "cannot create " << name << ": " << error << "\n";
            return p;
        }
    return nullptr;
}

// What MainComponent::applyDocument does, without a window: plugins, samples, kits, programs and audio clips.
std::vector<Channel> loadChannels (ProjectDocument& doc, PluginHost& host, juce::StringArray& problems)
{
    for (const auto& spec : doc.pluginSpecs)
    {
        juce::PluginDescription description;
        if (! PluginHost::descriptionFromText (juce::String (spec.description), description))
        {
            problems.add ("unreadable plugin description");
            continue;
        }
        for (const auto& d : host.known().getTypes())
            if (d.pluginFormatName == description.pluginFormatName && d.name == description.name && d.uniqueId == description.uniqueId)
                description = d;
        juce::String error;
        auto plugin = host.create (description, 48000.0, Player::kChunk, error);
        if (plugin == nullptr)
        {
            problems.add ("plugin not loaded: " + description.name + " " + error);
            continue;
        }
        juce::MemoryBlock state;
        state.fromBase64Encoding (juce::String (spec.state));
        plugin->setState (state);
        const auto slot = static_cast<size_t> (std::clamp (spec.slot, 0, kSlotsPerTrack - 1));
        switch (spec.place)
        {
            case PluginSpec::Place::Channel: doc.channels[static_cast<size_t> (spec.index)].plugin = plugin; break;
            case PluginSpec::Place::Track:   doc.tracks[static_cast<size_t> (spec.index)].slots[slot].plugin = plugin; break;
            case PluginSpec::Place::Aux:     doc.console.aux[static_cast<size_t> (spec.index)].slots[slot].plugin = plugin; break;
            case PluginSpec::Place::Master:  doc.masterSlots[slot].plugin = plugin; break;
        }
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (size_t i = 0; i < doc.channels.size(); ++i)
    {
        auto& c = doc.channels[i];
        const auto& source = i < doc.sampleSources.size() ? doc.sampleSources[i] : std::string();
        if (c.instrument == Instrument::Sampler && ! source.empty())
        {
            if (source.rfind ("builtin:", 0) == 0)
                c.sample = makeBuiltinSample (source.substr (8));
            if (c.sample == nullptr)
                problems.add ("sample not loaded: " + juce::String (source));
        }
        if (c.instrument == Instrument::Drums)
            c.kit = std::make_shared<const DrumKit> (loadKit (i < doc.kitSpecs.size() ? doc.kitSpecs[i] : KitSpec::empty(), problems));
        if (c.instrument == Instrument::Sampler && i < doc.programSpecs.size() && ! doc.programSpecs[i].empty())
        {
            juce::String error;
            c.program = loadProgram (doc.programSpecs[i], [] (const juce::File& f, juce::String& e) -> std::shared_ptr<const SampleData>
                                     {
                                         auto r = importAudioFile (f);
                                         if (r.data == nullptr)
                                             e = "unreadable";
                                         return r.data;
                                     },
                                     error);
            if (c.program == nullptr)
                problems.add ("program not loaded: " + error);
        }
    }
    loadAudioForClips (doc.arrangement, problems);
    doc.arrangement.ensureChannelCount (doc.channels.size());
    return doc.channels;
}

struct Levels
{
    double peakDb = -200, rmsDb = -200;
};

Levels levelsOf (const juce::AudioBuffer<float>& b, int start, int end)
{
    double sum = 0, peak = 0;
    int n = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = std::max (0, start); i < std::min (end, b.getNumSamples()); ++i)
        {
            const double v = b.getSample (ch, i);
            sum += v * v;
            peak = std::max (peak, std::abs (v));
            ++n;
        }
    Levels l;
    if (n > 0 && sum > 0)
        l.rmsDb = 10.0 * std::log10 (sum / n);
    if (peak > 0)
        l.peakDb = 20.0 * std::log10 (peak);
    return l;
}

// Energy in a few bands (one-pole splits), as dB relative to the full signal: a quick look at the balance of a mix.
void printBands (const juce::AudioBuffer<float>& b)
{
    const double rate = 48000.0;
    const double edges[] = { 60, 150, 400, 1200, 3500, 8000 };
    std::vector<double> energy (std::size (edges) + 1, 0.0);
    std::vector<double> lp (std::size (edges), 0.0);
    std::vector<double> coeff;
    for (double e : edges)
        coeff.push_back (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * e / rate));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double x = 0.5 * (b.getSample (0, i) + b.getSample (1, i));
        double prev = 0;
        for (size_t k = 0; k < lp.size(); ++k)
        {
            lp[k] += coeff[k] * (x - lp[k]);
            energy[k] += (lp[k] - prev) * (lp[k] - prev);
            prev = lp[k];
        }
        energy.back() += (x - prev) * (x - prev);
    }
    double total = 0;
    for (double e : energy)
        total += e;
    const char* names[] = { "<60", "60-150", "150-400", "400-1.2k", "1.2-3.5k", "3.5-8k", ">8k" };
    std::printf ("bands (dB of total):");
    for (size_t k = 0; k < energy.size(); ++k)
        std::printf (" %s %.1f", names[k], 10.0 * std::log10 (std::max (1e-12, energy[k] / total)));
    std::printf ("\n");
}

int render (const juce::File& in, const juce::File& out, const juce::File& stems)
{
    auto parsed = readProjectFile (in);
    if (! parsed.document)
    {
        std::cerr << parsed.error << "\n";
        return 1;
    }
    for (const auto& w : parsed.warnings)
        std::cerr << "warning: " << w << "\n";
    auto doc = std::move (*parsed.document);
    PluginHost host;
    addOwnPlugins (host);
    juce::StringArray problems;
    auto channels = loadChannels (doc, host, problems);
    for (const auto& p : problems)
        std::cerr << "problem: " << p << "\n";

    auto project = compileSong (channels, doc.arrangement, doc.tracks, doc.masterSlots, doc.console);
    project.masterPitch = doc.masterPitch;
    RenderOptions options;
    options.bpm = doc.bpm;
    options.sampleRate = 48000.0;
    options.tailSeconds = 3.0;
    host.prepareAll (options.sampleRate, Player::kChunk);
    auto buffer = renderToBuffer (project, options);

    // The same file the export writes (24-bit WAV).
    out.deleteFile();
    {
        std::unique_ptr<juce::FileOutputStream> stream (out.createOutputStream());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), options.sampleRate, 2, 24, {}, 0));
        if (writer == nullptr)
            return 1;
        stream.release();
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }

    const double secondsPerBar = 4 * 60.0 / doc.bpm;
    const auto all = levelsOf (buffer, 0, buffer.getNumSamples());
    LoudnessMeter meter;
    meter.prepare (options.sampleRate);
    meter.process (buffer.getReadPointer (0), buffer.getReadPointer (1), buffer.getNumSamples());
    std::printf ("%s: %.1f s, peak %.2f dBFS, rms %.2f dBFS, %.1f LUFS integrated, LRA %.1f LU, true peak %.2f dBTP\n", out.getFileName().toRawUTF8(),
                 buffer.getNumSamples() / options.sampleRate, all.peakDb, all.rmsDb, meter.integrated(), meter.range(), meter.truePeakDb());
    printBands (buffer);
    std::printf ("per 4 bars (rms / peak):\n");
    const int bars = static_cast<int> (std::ceil (project.lengthTicks / (4.0 * kTicksPerBeat)));
    for (int b = 0; b < bars; b += 4)
    {
        const int s = static_cast<int> (b * secondsPerBar * options.sampleRate), e = static_cast<int> ((b + 4) * secondsPerBar * options.sampleRate);
        const auto l = levelsOf (buffer, s, e);
        std::printf ("  bar %3d: %6.1f / %5.1f\n", b + 1, l.rmsDb, l.peakDb);
    }

    if (stems != juce::File())
    {
        stems.createDirectory();
        std::vector<juce::File> written;
        host.prepareAll (options.sampleRate, Player::kChunk);
        const auto r = exportStems (project, options, stems.getChildFile ("stem.wav"), ExportFormat::Wav, written);
        if (r.failed())
            std::cerr << r.getErrorMessage() << "\n";
        for (const auto& f : written)
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
            if (reader == nullptr)
                continue;
            juce::AudioBuffer<float> sb (2, static_cast<int> (reader->lengthInSamples));
            reader->read (&sb, 0, sb.getNumSamples(), 0, true, true);
            const auto l = levelsOf (sb, 0, sb.getNumSamples());
            std::printf ("  stem %-40s peak %6.1f rms %6.1f\n", f.getFileNameWithoutExtension().toRawUTF8(), l.peakDb, l.rmsDb);
        }
    }
    return 0;
}

juce::var pluginInfo (PluginHost& host, const juce::String& name, const juce::StringArray& programs)
{
    auto* o = new juce::DynamicObject();
    auto plugin = makeOwn (host, name);
    if (plugin == nullptr)
        return {};
    o->setProperty ("description", PluginHost::descriptionToText (plugin->getDescription()));
    juce::Array<juce::var> params;
    for (int i = 0; i < plugin->parameterCount(); ++i)
        params.add (juce::String (plugin->parameterName (i)));
    o->setProperty ("parameters", params);
    auto* presets = new juce::DynamicObject();
    auto* instance = plugin->getInstance();
    for (int i = 0; i < instance->getNumPrograms(); ++i)
    {
        const auto programName = instance->getProgramName (i);
        if (! plugin->selectProgram (programName.toStdString()))
            continue;
        presets->setProperty (programName, plugin->getState().toBase64Encoding());
    }
    juce::ignoreUnused (programs);
    o->setProperty ("presets", juce::var (presets));
    return juce::var (o);
}

int catalog (const juce::File& out)
{
    PluginHost host;
    addOwnPlugins (host);
    auto* root = new juce::DynamicObject();

    auto* synths = new juce::DynamicObject();
    for (const auto& p : synthFactoryPatches())
        synths->setProperty (juce::String (p.name), synthPatchToVar (p.patch));
    root->setProperty ("synthPatches", juce::var (synths));

    juce::Array<juce::var> programs;
    for (const auto& p : libraryPrograms())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", juce::String (p.id));
        o->setProperty ("name", juce::String (p.name));
        o->setProperty ("category", juce::String (p.category));
        o->setProperty ("sampler", samplerSettingsToVar (libraryProgramSettings (p.id)));
        programs.add (juce::var (o));
    }
    root->setProperty ("programs", programs);

    juce::Array<juce::var> kits;
    for (const auto& k : builtinKits())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", juce::String (k.id));
        o->setProperty ("name", juce::String (k.name));
        o->setProperty ("spec", kitSpecToVar (builtinKitSpec (k.id)));
        kits.add (juce::var (o));
    }
    root->setProperty ("kits", kits);

    juce::Array<juce::var> samples;
    for (const auto& id : builtinSampleIds())
        samples.add (juce::String (id));
    root->setProperty ("builtinSamples", samples);

    // The built-in effects: each parameter's name, range and default (in the order a project file lists them).
    auto* effects = new juce::DynamicObject();
    for (int t = 0; t < kEffectTypeCount; ++t)
    {
        const auto type = static_cast<EffectType> (t);
        juce::Array<juce::var> params;
        for (int i = 0; i < effectParamCount (type); ++i)
        {
            const auto info = effectParamInfo (type, i);
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", juce::String (info.name));
            o->setProperty ("min", info.min);
            o->setProperty ("max", info.max);
            o->setProperty ("def", info.def);
            params.add (juce::var (o));
        }
        effects->setProperty (effectName (type), params);
    }
    root->setProperty ("effects", juce::var (effects));

    root->setProperty ("SunBurst", pluginInfo (host, "SunBurst", {}));
    root->setProperty ("HexForge", pluginInfo (host, "HexForge", {}));
    out.replaceWithText (juce::JSON::toString (juce::var (root), false));
    return 0;
}

int sunrise (const juce::File& out)
{
    PluginHost host;
    addOwnPlugins (host);
    auto demo = makeDemoSong();
    const auto problems = attachDemoPlugins (demo, [&host] (const std::string& name) -> std::shared_ptr<HostedPlugin> { return makeOwn (host, name); });
    for (const auto& p : problems)
        std::cerr << "problem: " << p << "\n";
    auto& doc = demo.document;
    // As MainComponent::addPluginSpecs: each plugin channel's description and state.
    for (size_t i = 0; i < doc.channels.size(); ++i)
        if (doc.channels[i].instrument == Instrument::Plugin && doc.channels[i].plugin != nullptr)
            if (auto* j = dynamic_cast<JucePlugin*> (doc.channels[i].plugin.get()))
            {
                PluginSpec s;
                s.place = PluginSpec::Place::Channel;
                s.index = static_cast<int> (i);
                s.instrument = true;
                s.description = PluginHost::descriptionToText (j->getDescription()).toStdString();
                s.state = j->getState().toBase64Encoding().toStdString();
                doc.pluginSpecs.push_back (std::move (s));
            }
    for (auto& c : doc.channels)
    {
        c.notes.clear();
        c.plugin = nullptr;
    }
    const auto error = writeProjectFile (out, doc);
    if (error.isNotEmpty())
    {
        std::cerr << error << "\n";
        return 1;
    }
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    buildFolder = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getParentDirectory().getParentDirectory();
    if (const char* env = std::getenv ("SLS_BUILD_DIR"))
        buildFolder = juce::File (env);
    const juce::StringArray args (argv + 1, argc - 1);
    if (args.size() >= 3 && args[0] == "render")
        return render (juce::File (args[1]), juce::File (args[2]), args.size() >= 5 && args[3] == "--stems" ? juce::File (args[4]) : juce::File());
    if (args.size() >= 2 && args[0] == "catalog")
        return catalog (juce::File (args[1]));
    if (args.size() >= 2 && args[0] == "length") // the length of built-in sounds, in seconds
    {
        for (int i = 1; i < args.size(); ++i)
            if (const auto data = makeBuiltinSample (args[i].toStdString()); data != nullptr && data->sampleRate > 0)
                std::printf ("%s %.3f\n", args[i].toRawUTF8(), data->numFrames() / data->sampleRate);
            else
                std::printf ("%s unknown\n", args[i].toRawUTF8());
        return 0;
    }
    if (args.size() >= 2 && args[0] == "sunrise")
        return sunrise (juce::File (args[1]));
    std::cerr << "usage: DemoTool render <in.slp> <out.wav> [--stems <dir>] | catalog <out.json> | sunrise <out.slp>\n";
    return 2;
}
