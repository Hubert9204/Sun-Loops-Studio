#pragma once

#include <array>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "audio/DrumKit.h"

namespace sls
{
// Drum kits on disk and in project files.
//
// A kit is described by a KitSpec: for every pad its name, settings, and for every layer where its sound comes from ("builtin:<id>",
// an absolute file path, or empty) and the lowest velocity it answers to. The audio itself is read by loadKit().
//
// The kit format is a folder with a `kit.json` and, usually, a `samples` sub-folder holding copies of the sounds, so the folder can be
// moved or shared. Sounds that are built into the program stay `builtin:` references (they are not copied). A plain folder of audio
// files, without a kit.json, also works as a kit: its files, sorted by name, fill pads 1 to 16.
constexpr const char* kKitFileName = "kit.json";
constexpr const char* kKitFormatName = "sun-loops-studio-drum-kit";
constexpr int kKitFormatVersion = 1;
constexpr double kMaxPadSampleSeconds = 30.0; // like a sampler channel's sample

struct PadSpec
{
    std::string name = "Pad";
    std::array<std::string, kPadLayers> sources {};
    std::array<int, kPadLayers> minVelocity { 1, 1, 1, 1 };
    float volume = 1.0f, pan = 0.0f, pitch = 0.0f, swing = 0.0f;
    int chokeGroup = 0;
    bool mute = false, solo = false;
};

struct KitSpec
{
    std::string name = "Kit";
    std::array<PadSpec, kDrumPads> pads {};

    static KitSpec empty();
};

// What a kit in memory refers to (the `source` of each layer's sample).
KitSpec specFromKit (const DrumKit& kit);

// JSON for the project file and the kit file. Reading is lenient: numbers are clamped, a bad pad keeps its defaults.
juce::var kitSpecToVar (const KitSpec& spec);
KitSpec kitSpecFromVar (const juce::var& v);

// One sound: "builtin:<id>" or a file path, at most kMaxPadSampleSeconds long. nullptr and a message in `problem` when it cannot be read.
std::shared_ptr<const SampleData> loadKitSample (const std::string& source, juce::String& problem);

// Reads the audio of every layer. A source that cannot be read gives a silent placeholder that keeps the source (so saving again does not lose it) and an
// entry in `problems`. The same source is read once. Pads, names and settings are kept as they are.
DrumKit loadKit (const KitSpec& spec, juce::StringArray& problems);

// Writes `kit` as a kit folder: kit.json (replaced atomically) and copies of the sound files in `samples/`. `folder` must not exist, or be empty, or already
// hold a kit.json. Returns an error message, empty on success.
juce::String saveKitFolder (const DrumKit& kit, const juce::File& folder);

bool isKitFolder (const juce::File& folder);

// A kit folder (kit.json) or a plain folder of audio files. Relative paths in kit.json are made absolute. Problems and repairs go to `problems`.
KitSpec readKitFolder (const juce::File& folder, juce::StringArray& problems);

// The kits that are built into the program.
KitSpec basicKitSpec();    // kick, snare, hat (the hat in choke group 1)

// Every kit built into the program (0.22.0): the sampled kits of the sound library ("lib:<id>"), the kits made by code ("gen:<id>")
// and the basic kit ("basic"). The id is what builtinKitSpec takes; the spec refers to the sounds as "builtin:..." sources.
struct BuiltinKit
{
    std::string id, name, credit;
};
const std::vector<BuiltinKit>& builtinKits();
KitSpec builtinKitSpec (const std::string& id); // an empty kit for an unknown id

// The lowest velocity of each layer of a generated kit (soft, medium, hard, hardest).
constexpr std::array<int, kPadLayers> kGeneratedMinVelocity { 1, 45, 80, 110 };

// The spec as it should be shown for a loaded kit whose layers use `source` strings: used by the tests and by saveKitFolder.
KitSpec resolvedForFolder (const KitSpec& spec, const juce::File& folder);
} // namespace sls
