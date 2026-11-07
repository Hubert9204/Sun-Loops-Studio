#pragma once

#include <memory>
#include <string>
#include <vector>

#include "audio/Project.h"

namespace sls
{
// Small synthesised drum sounds so the sampler has something to play without any files.
std::shared_ptr<const SampleData> makeKickSample();
std::shared_ptr<const SampleData> makeSnareSample();
std::shared_ptr<const SampleData> makeHatSample();

// The ids "kick", "snare" and "hat", the sounds of the library ("lib:...", see SoundLibrary.h), the generated drums ("gen:...",
// see DrumSynth.h) and "intro"; nullptr for anything else (the Cyber-Hub sounds "cyber:..." were removed in 0.22.0).
std::shared_ptr<const SampleData> makeBuiltinSample (const std::string& id);
const std::vector<std::string>& builtinSampleIds(); // the three small synthesised sounds

// The start-up sound (resources/sounds/intro.wav), decoded; nullptr if it could not be read.
std::shared_ptr<const SampleData> makeStartupSample();
constexpr const char* kStartupSampleId = "intro";

// Ids of sounds that earlier versions had and this one no longer has, with a message for the user (empty when `id` is not one).
std::string removedBuiltinMessage (const std::string& id);
} // namespace sls
