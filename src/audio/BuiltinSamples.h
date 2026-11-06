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

// The ids "kick", "snare" and "hat", the Cyber-Hub sounds ("cyber:<name>", see CyberHub.h) and "intro"; nullptr for anything else.
std::shared_ptr<const SampleData> makeBuiltinSample (const std::string& id);
const std::vector<std::string>& builtinSampleIds();
} // namespace sls
