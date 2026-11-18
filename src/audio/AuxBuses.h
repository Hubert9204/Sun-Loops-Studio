#pragma once

#include <string>
#include <vector>

#include "audio/Project.h"

namespace sls
{
// Adding and removing aux buses (0.78.0: the mixer's + and - after the aux strips). Pure, message thread; tested headless.

// Adds a bus after the last one, at its defaults ("Aux C"...): false when the project already has kAuxBuses.
bool addAuxBus (Console& console);

// Why the last aux bus must stay, in a few words for the status bar ("Bass sends to it", "it has effects", "an automation clip
// controls it"), or empty when it can go: like the last mixer track, a bus is removed only when nothing would be lost.
std::string whyKeepLastAuxBus (const Console& console, const std::vector<MixerTrack>& tracks, const std::vector<AutomationClip>& clips);

// Removes the last aux bus (when whyKeepLastAuxBus is empty): it goes back to its defaults, unused. False when there is none.
bool removeLastAuxBus (Console& console);
} // namespace sls
