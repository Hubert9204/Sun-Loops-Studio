#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "library/ProjectFile.h"

namespace sls
{
// The demo song (0.52.0) that the program opens with: "Sunrise Drive", 44 bars of synthwave in A minor at 110 BPM that uses
// what the program can do - a drum machine, the sampled piano and bass, the four synth engines (subtractive, FM, additive,
// physical), SunBurst and HexForge with their presets, audio clips (the start-up sound, a reversed and a pitched cymbal), insert
// effects on every track and on the master, two aux buses, two VCA groups, a sidechain, automation of plugins, effects, sends,
// pans and a VCA, and tempo changes at the end.
//
// The library part knows nothing of plugin hosting: the document's plugin channels have no plugin yet. attachDemoPlugins()
// puts them in, chooses their presets and points the plugin automation at the right parameters.
struct DemoSong
{
    // A channel that wants one of the program's own plugins.
    struct PluginChannel
    {
        int channel = 0;
        std::string plugin;   // "SunBurst" or "HexForge"
        std::string preset;   // a factory preset of it
        SynthPatch fallback;  // the built-in synth that stands in when the plugin is not there
    };
    // An automation clip of a plugin channel: its parameter is found by name once the plugin exists.
    struct PluginAutomation
    {
        size_t clip = 0;       // in document.arrangement.automationClips
        std::string parameter; // the plugin's name for it ("Cutoff")
    };

    ProjectDocument document;
    std::vector<PluginChannel> pluginChannels;
    std::vector<PluginAutomation> pluginAutomation;
};

constexpr double kDemoSongBpm = 110.0;
constexpr const char* kDemoSongName = "Sunrise Drive";

DemoSong makeDemoSong();

// Creates the plugins with `make` (by name; nullptr when that plugin cannot be loaded here) and selects their presets with
// HostedPlugin::selectProgram. A channel whose plugin is missing becomes a built-in synth and loses its plugin automation.
// Returns what could not be done, for the log (empty when everything is in place).
std::vector<std::string> attachDemoPlugins (DemoSong& demo, const std::function<std::shared_ptr<HostedPlugin> (const std::string&)>& make);
} // namespace sls
