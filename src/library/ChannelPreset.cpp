#include "library/ChannelPreset.h"

#include <algorithm>
#include <cmath>

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
    return p;
}

const std::vector<ChannelPreset>& factoryPresets()
{
    static const std::vector<ChannelPreset> presets = []
    {
        std::vector<ChannelPreset> v;

        auto sampler = [&] (const char* id, const char* name, const char* sample, float volume)
        {
            ChannelPreset p;
            p.id = id;
            p.name = name;
            p.instrument = Instrument::Sampler;
            p.volume = volume;
            p.sampleSource = std::string ("builtin:") + sample;
            v.push_back (p);
        };
        sampler ("kick", "Kick", "kick", 0.95f);
        sampler ("snare", "Snare", "snare", 0.7f);
        sampler ("hat", "Hi-hat", "hat", 0.5f);

        ChannelPreset lead;
        lead.id = "lead";
        lead.name = "Soft Lead";
        lead.volume = 0.55f;
        v.push_back (lead);

        ChannelPreset bass;
        bass.id = "bass";
        bass.name = "Sub Bass";
        bass.volume = 0.9f;
        bass.pitch = -12.0f;
        v.push_back (bass);

        ChannelPreset swungHat;
        swungHat.id = "swung-hat";
        swungHat.name = "Swung Hat";
        swungHat.instrument = Instrument::Sampler;
        swungHat.volume = 0.5f;
        swungHat.swing = 0.6f;
        swungHat.sampleSource = "builtin:hat";
        v.push_back (swungHat);

        return v;
    }();
    return presets;
}
} // namespace sls
