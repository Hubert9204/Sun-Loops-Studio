#pragma once

#include <optional>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "audio/Project.h"

namespace sls
{
// A saved channel setup: instrument, mix settings and (optionally) where its sample comes from.
// Stored as a small JSON file (.slspreset); dropping one on a channel applies it.
struct ChannelPreset
{
    std::string id;       // factory presets only
    std::string category; // factory presets only: the browser folder ("Keys", "Bass", "Synth", "Drums")
    std::string name = "Preset";
    Instrument instrument = Instrument::Synth;
    float volume = 0.8f;
    float pan = 0.0f;
    float pitch = 0.0f;
    float swing = 0.0f;
    int rootPitch = 60;
    std::string sampleSource; // "builtin:<id>", an absolute path, or empty
    SynthPatch synth;         // synth presets (0.18.0): the engine and its settings; older preset files give the Classic engine
    std::string programSource; // sampler (0.22.0): a built-in multi-sample instrument ("builtin:program:<id>"), used instead of the sample
    std::optional<SamplerSettings> sampler; // sampler (0.22.0): envelope, loop, filter; older preset files leave the channel's own

    static ChannelPreset fromChannel (const Channel& channel, const std::string& presetName);

    // Applies everything except the sample and the program, which the caller resolves from sampleSource and programSource.
    // Notes, routing and mute/solo are left alone.
    void applyTo (Channel& channel) const;

    juce::String toJson() const;

    // Strict about the structure (a JSON object with a known instrument); lenient about numbers: they are
    // clamped into their valid range and a NaN becomes the default.
    static std::optional<ChannelPreset> fromJson (const juce::String& text);
};

// A synth patch as JSON (project and preset files, 0.18.0). Missing or bad values keep their defaults; everything is clamped.
juce::var synthPatchToVar (const SynthPatch& patch);
SynthPatch synthPatchFromVar (const juce::var& v);

// Ready-made presets that ship with the program.
const std::vector<ChannelPreset>& factoryPresets();

constexpr const char* kPresetExtension = ".slspreset";
} // namespace sls
