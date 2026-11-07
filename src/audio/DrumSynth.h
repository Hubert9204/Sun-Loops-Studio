#pragma once

#include <memory>
#include <string>
#include <vector>

#include "audio/SampleData.h"

namespace sls
{
// Drum and percussion sounds made by code (0.22.0): three electronic kits in the manner of the classic drum machines (analog
// "boom" kicks and metallic hats, a punchier kit with clicky kicks and noisy snares, a modern kit with long distorted sub kicks)
// and a hand-percussion kit made with modal synthesis (congas, bongos, timbales, bells, woods, shaker, tambourine, triangle).
//
// Every sound is generated the same way on every computer (fixed random seeds) at 44.1 kHz, in four layers of increasing
// strength for the velocity layers of the drum machine: a harder hit is not only louder but brighter, with more attack and,
// for drums, a deeper pitch drop. The id of a sound is "gen:<kit>/<piece>/<layer 1-4>" (its source "builtin:gen:...").

struct GeneratedPad
{
    std::string name;  // "Closed Hat"
    std::string piece; // "closed-hat": the sound, together with the kit
    int chokeGroup = 0;
    float pan = 0.0f;
};

struct GeneratedKitInfo
{
    std::string id;   // "boom"
    std::string name; // "Electronic Boom"
    std::vector<GeneratedPad> pads; // 16, in the General MIDI drum order from key 36 where the kit has such a sound
};

constexpr int kGeneratedLayers = 4;

const std::vector<GeneratedKitInfo>& generatedKits();
bool isGeneratedId (const std::string& id); // "gen:..."

// The sound (shared while in use), or nullptr for an id that names no sound.
std::shared_ptr<const SampleData> makeGeneratedSample (const std::string& id);
} // namespace sls
