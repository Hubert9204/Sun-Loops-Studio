#pragma once

#include <memory>

#include "audio/Project.h"

namespace sls
{
// Small synthesised drum sounds so the sampler has something to play without any files.
std::shared_ptr<const SampleData> makeKickSample();
std::shared_ptr<const SampleData> makeSnareSample();
std::shared_ptr<const SampleData> makeHatSample();
} // namespace sls
