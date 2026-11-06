#pragma once

#include <memory>
#include <string>
#include <vector>

#include "audio/Project.h"

namespace sls
{
// The Cyber-Hub sound theme (30 system sounds by Hubert, CC BY-ND 4.0) and the start-up sound, built into the program.
// The files are decoded as they are: nothing here changes the audio. Each sound has an id "cyber:<file name without
// extension>" that can be used wherever a built-in sample is (browser, channel presets, project files: the source of
// a sample is then "builtin:cyber:<name>").
//
// Roles are what a listening analysis suggested (length, brightness, noisiness, pitch) and only decide where the
// browser files a sound.
enum class CyberRole
{
    Hit,  // short and percussive: kicks, sub hits, ticks, claps and short bells
    Tone, // 0.8 - 1.5 s with a clear pitch: stingers and notification tones
    Long  // 3 s and more: risers, swells, ringing and ambient beds
};

struct CyberSound
{
    std::string id;        // "cyber:completion-success"
    std::string title;     // "Completion success"
    std::string fileName;  // "completion-success.oga"
    CyberRole role = CyberRole::Hit;
};

constexpr const char* kCyberHubCredit = "Cyber-Hub sound theme by Hubert, CC BY-ND 4.0 (used unchanged)";

const std::vector<CyberSound>& cyberHubSounds(); // sorted by file name; empty only if the data was not built in
bool isCyberHubId (const std::string& id);       // "cyber:..." (whether or not it exists)

// The decoded sound (stereo, at its own sample rate) or nullptr for an unknown id. Decoded once, then shared.
std::shared_ptr<const SampleData> makeCyberHubSample (const std::string& id);

// A drum kit made of Cyber-Hub sounds: the roles that suited them best in the analysis.
struct CyberKitPiece
{
    const char* label; // "Kick"
    const char* id;    // "cyber:button-pressed"
};
const std::vector<CyberKitPiece>& cyberHubKit();

// The start-up sound (intro.wav), decoded; nullptr if it could not be read.
std::shared_ptr<const SampleData> makeStartupSample();
constexpr const char* kStartupSampleId = "intro";
} // namespace sls
