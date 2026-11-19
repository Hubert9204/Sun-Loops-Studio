#include "audio/AuxBuses.h"

namespace sls
{
namespace
{
MixerTrack defaultBus (int index)
{
    MixerTrack bus;
    bus.name = std::string ("Aux ") + static_cast<char> ('A' + index);
    return bus;
}
} // namespace

bool addAuxBus (Console& console)
{
    if (console.auxCount < 0 || console.auxCount >= kAuxBuses)
        return false;
    console.aux[static_cast<size_t> (console.auxCount)] = defaultBus (console.auxCount);
    ++console.auxCount;
    return true;
}

std::string whyKeepLastAuxBus (const Console& console, const std::vector<MixerTrack>& tracks, const std::vector<AutomationClip>& clips)
{
    const int last = console.auxCount - 1;
    if (last < 0 || last >= kAuxBuses)
        return "there is no aux bus";
    for (const auto& t : tracks)
        if (t.sends[static_cast<size_t> (last)] > 0.0f)
            return t.name + " sends to it";
    for (const auto& slot : console.aux[static_cast<size_t> (last)].slots)
        if (slot.type != EffectType::None)
            return "it has effects";
    for (const auto& c : clips)
        if (((c.target.kind == AutomationKind::AuxVolume || c.target.kind == AutomationKind::AuxPan || c.target.kind == AutomationKind::AuxMute
              || c.target.kind == AutomationKind::TrackSend)
             && c.target.slot == last)
            || ((c.target.kind == AutomationKind::AuxEffect || c.target.kind == AutomationKind::AuxPlugin) && c.target.index == last)) // 0.80.0
            return "an automation clip controls it";
    return {};
}

bool removeLastAuxBus (Console& console)
{
    if (console.auxCount <= 0 || console.auxCount > kAuxBuses)
        return false;
    --console.auxCount;
    console.aux[static_cast<size_t> (console.auxCount)] = defaultBus (console.auxCount);
    return true;
}
} // namespace sls
