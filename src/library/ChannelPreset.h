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
    std::string id;   // factory presets only
    std::string name = "Preset";
    Instrument instrument = Instrument::Synth;
    float volume = 0.8f;
    float pan = 0.0f;
    float pitch = 0.0f;
    float swing = 0.0f;
    int rootPitch = 60;
    std::string sampleSource; // "builtin:<id>", an absolute path, or empty

    static ChannelPreset fromChannel (const Channel& channel, const std::string& presetName);

    // Applies everything except the sample, which the caller resolves from sampleSource. Notes, routing and
    // mute/solo are left alone.
    void applyTo (Channel& channel) const;

    juce::String toJson() const;

    // Strict about the structure (a JSON object with a known instrument); lenient about numbers: they are
    // clamped into their valid range and a NaN becomes the default.
    static std::optional<ChannelPreset> fromJson (const juce::String& text);
};

// Ready-made presets that ship with the program.
const std::vector<ChannelPreset>& factoryPresets();

constexpr const char* kPresetExtension = ".slspreset";
} // namespace sls
