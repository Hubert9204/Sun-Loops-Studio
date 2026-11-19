// Automation: creating a clip from a control (right click), naming what a clip moves, and the point editor window.
#include "ui/MainComponent.h"

#include "ui/AutomationEditor.h"
#include "ui/PluginParameterPicker.h"

namespace sls
{
namespace
{
// 0.86.0: "Patcher 3 > " for a parameter of the third effect inside a Patcher
juce::String patcherPrefix (int param)
{
    return isPatcherParam (param) ? "Patcher " + juce::String (patcherInner (param) + 1) + " > " : juce::String();
}

const char* effectParamNameFor (EffectType type, int param)
{
    return param >= 0 && param < effectParamCount (type) ? effectParamInfo (type, param).name : "parameter";
}
}

EffectType MainComponent::effectTypeFor (const AutomationTarget& t) const
{
    if (t.slot < 0 || t.slot >= kSlotsPerTrack)
        return EffectType::None;
    // (0.86.0: for a parameter of an effect inside a Patcher, that effect)
    if (t.kind == AutomationKind::MasterEffect)
        return effectTypeOfParam (masterTrack.slots[static_cast<size_t> (t.slot)], t.param);
    if (t.kind == AutomationKind::TrackEffect && t.index >= 0 && t.index < static_cast<int> (tracks.size()))
        return effectTypeOfParam (tracks[static_cast<size_t> (t.index)].slots[static_cast<size_t> (t.slot)], t.param);
    if (t.kind == AutomationKind::AuxEffect && t.index >= 0 && t.index < kAuxBuses) // 0.80.0
        return effectTypeOfParam (console.aux[static_cast<size_t> (t.index)].slots[static_cast<size_t> (t.slot)], t.param);
    return EffectType::None;
}

std::shared_ptr<HostedPlugin> MainComponent::pluginFor (const AutomationTarget& t) const
{
    const bool slotOk = t.slot >= 0 && t.slot < kSlotsPerTrack;
    switch (t.kind)
    {
        case AutomationKind::ChannelPlugin:
            if (t.index >= 0 && t.index < static_cast<int> (channels.size()) && channels[static_cast<size_t> (t.index)].instrument == Instrument::Plugin)
                return channels[static_cast<size_t> (t.index)].plugin;
            break;
        case AutomationKind::TrackPlugin:
            if (slotOk && t.index >= 0 && t.index < static_cast<int> (tracks.size()))
                return tracks[static_cast<size_t> (t.index)].slots[static_cast<size_t> (t.slot)].plugin;
            break;
        case AutomationKind::MasterPlugin:
            if (slotOk)
                return masterTrack.slots[static_cast<size_t> (t.slot)].plugin;
            break;
        case AutomationKind::AuxPlugin: // 0.80.0
            if (slotOk && t.index >= 0 && t.index < kAuxBuses)
                return console.aux[static_cast<size_t> (t.index)].slots[static_cast<size_t> (t.slot)].plugin;
            break;
        default:
            break;
        case AutomationKind::ChannelVolume:
        case AutomationKind::ChannelPan:
        case AutomationKind::TrackVolume:
        case AutomationKind::TrackPan:
        case AutomationKind::TrackEffect:
        case AutomationKind::MasterEffect:
        case AutomationKind::TrackSend:
        case AutomationKind::AuxVolume:
        case AutomationKind::AuxPan:
        case AutomationKind::VcaLevel: break;
    }
    return nullptr;
}

float MainComponent::currentValueOf (const AutomationTarget& t) const
{
    if (isPluginAutomation (t.kind))
    {
        const auto plugin = pluginFor (t);
        return plugin != nullptr && t.param < plugin->parameterCount() ? plugin->parameterValue (t.param) : 0.5f;
    }
    const auto at = [] (const auto& v, int i) { return i >= 0 && i < static_cast<int> (v.size()); };
    switch (t.kind)
    {
        case AutomationKind::ChannelVolume: if (at (channels, t.index)) return channels[static_cast<size_t> (t.index)].volume; break;
        case AutomationKind::ChannelPan:    if (at (channels, t.index)) return channels[static_cast<size_t> (t.index)].pan; break;
        case AutomationKind::TrackVolume:   if (at (tracks, t.index)) return tracks[static_cast<size_t> (t.index)].volume; break;
        case AutomationKind::TrackPan:      if (at (tracks, t.index)) return tracks[static_cast<size_t> (t.index)].pan; break;
        case AutomationKind::TrackEffect:
            if (at (tracks, t.index) && t.slot >= 0 && t.slot < kSlotsPerTrack && ((t.param >= 0 && t.param < kMaxEffectParams) || isPatcherParam (t.param)))
                return effectParamValueOf (tracks[static_cast<size_t> (t.index)].slots[static_cast<size_t> (t.slot)], t.param);
            break;
        case AutomationKind::MasterEffect:
            if (t.slot >= 0 && t.slot < kSlotsPerTrack && ((t.param >= 0 && t.param < kMaxEffectParams) || isPatcherParam (t.param)))
                return effectParamValueOf (masterTrack.slots[static_cast<size_t> (t.slot)], t.param);
            break;
        case AutomationKind::ChannelPlugin:
        case AutomationKind::TrackPlugin:
        case AutomationKind::MasterPlugin: break; // handled above
        case AutomationKind::TrackSend:
            if (at (tracks, t.index) && t.slot >= 0 && t.slot < kAuxBuses)
                return tracks[static_cast<size_t> (t.index)].sends[static_cast<size_t> (t.slot)];
            break;
        case AutomationKind::AuxVolume: if (t.slot >= 0 && t.slot < kAuxBuses) return console.aux[static_cast<size_t> (t.slot)].volume; break;
        case AutomationKind::AuxPan:    if (t.slot >= 0 && t.slot < kAuxBuses) return console.aux[static_cast<size_t> (t.slot)].pan; break;
        case AutomationKind::AuxMute:   if (t.slot >= 0 && t.slot < kAuxBuses) return console.aux[static_cast<size_t> (t.slot)].mute ? 1.0f : 0.0f; break; // 0.86.0
        case AutomationKind::VcaLevel:  if (t.slot >= 0 && t.slot < kVcaGroups) return console.vcaLevel[static_cast<size_t> (t.slot)]; break;
        // 0.80.0
        case AutomationKind::ChannelPitch: if (at (channels, t.index)) return channels[static_cast<size_t> (t.index)].pitch; break;
        case AutomationKind::ChannelSwing: if (at (channels, t.index)) return channels[static_cast<size_t> (t.index)].swing; break;
        case AutomationKind::ChannelMute:  if (at (channels, t.index)) return channels[static_cast<size_t> (t.index)].mute ? 1.0f : 0.0f; break;
        case AutomationKind::ChannelSolo:  if (at (channels, t.index)) return channels[static_cast<size_t> (t.index)].solo ? 1.0f : 0.0f; break;
        case AutomationKind::TrackMute:    if (at (tracks, t.index)) return tracks[static_cast<size_t> (t.index)].mute ? 1.0f : 0.0f; break;
        case AutomationKind::TrackSolo:    if (at (tracks, t.index)) return tracks[static_cast<size_t> (t.index)].solo ? 1.0f : 0.0f; break;
        case AutomationKind::MasterVolume: return static_cast<float> (volumeSlider.getValue());
        case AutomationKind::MasterPitch:  return masterPitch;
        case AutomationKind::AuxEffect:
            if (t.index >= 0 && t.index < kAuxBuses && t.slot >= 0 && t.slot < kSlotsPerTrack && ((t.param >= 0 && t.param < kMaxEffectParams) || isPatcherParam (t.param)))
                return effectParamValueOf (console.aux[static_cast<size_t> (t.index)].slots[static_cast<size_t> (t.slot)], t.param);
            break;
        case AutomationKind::AuxPlugin: break; // handled above
    }
    return automationRange (t, effectTypeFor (t)).def;
}

juce::String MainComponent::automationLabelFor (const AutomationTarget& t) const
{
    const auto channelName = [this] (int i)
    {
        return i >= 0 && i < static_cast<int> (channels.size()) ? juce::String (channels[static_cast<size_t> (i)].name)
                                                                : "Channel " + juce::String (i + 1);
    };
    const auto auxName = [this] (int a)
    {
        return a >= 0 && a < kAuxBuses && ! console.aux[static_cast<size_t> (a)].name.empty() ? juce::String (console.aux[static_cast<size_t> (a)].name)
                                                                                                : "Aux " + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + std::max (0, a)));
    };
    switch (t.kind)
    {
        case AutomationKind::ChannelVolume: return channelName (t.index) + " volume";
        case AutomationKind::ChannelPan:    return channelName (t.index) + " pan";
        case AutomationKind::TrackVolume:   return "Track " + juce::String (t.index + 1) + " volume";
        case AutomationKind::TrackPan:      return "Track " + juce::String (t.index + 1) + " balance";
        case AutomationKind::TrackSend:     return "Track " + juce::String (t.index + 1) + " send to " + auxName (t.slot);
        case AutomationKind::AuxVolume:     return auxName (t.slot) + " volume";
        case AutomationKind::AuxPan:        return auxName (t.slot) + " balance";
        case AutomationKind::AuxMute:       return auxName (t.slot) + " mute"; // 0.86.0
        case AutomationKind::VcaLevel:      return "VCA " + juce::String (t.slot + 1) + " level";
        case AutomationKind::ChannelPitch:  return channelName (t.index) + " pitch"; // 0.80.0
        case AutomationKind::ChannelSwing:  return channelName (t.index) + " swing";
        case AutomationKind::ChannelMute:   return channelName (t.index) + " mute";
        case AutomationKind::ChannelSolo:   return channelName (t.index) + " solo";
        case AutomationKind::TrackMute:     return "Track " + juce::String (t.index + 1) + " mute";
        case AutomationKind::TrackSolo:     return "Track " + juce::String (t.index + 1) + " solo";
        case AutomationKind::MasterVolume:  return "Master volume";
        case AutomationKind::MasterPitch:   return "Master pitch";
        case AutomationKind::AuxEffect:
        {
            const auto type = effectTypeFor (t);
            return auxName (t.index) + " " + patcherPrefix (t.param) + effectName (type) + " " + effectParamNameFor (type, effectParamIndex (t.param));
        }
        case AutomationKind::TrackEffect:
        {
            const auto type = effectTypeFor (t);
            return "Track " + juce::String (t.index + 1) + " " + patcherPrefix (t.param) + effectName (type) + " " + effectParamNameFor (type, effectParamIndex (t.param));
        }
        case AutomationKind::MasterEffect:
        {
            const auto type = effectTypeFor (t);
            return juce::String ("Master ") + patcherPrefix (t.param) + effectName (type) + " " + effectParamNameFor (type, effectParamIndex (t.param));
        }
        case AutomationKind::ChannelPlugin:
        case AutomationKind::TrackPlugin:
        case AutomationKind::MasterPlugin:
        case AutomationKind::AuxPlugin:
        {
            const auto plugin = pluginFor (t);
            const juce::String where = t.kind == AutomationKind::ChannelPlugin ? channelName (t.index)
                                     : t.kind == AutomationKind::TrackPlugin   ? "Track " + juce::String (t.index + 1)
                                     : t.kind == AutomationKind::AuxPlugin     ? auxName (t.index)
                                                                               : juce::String ("Master");
            if (plugin == nullptr)
                return where + " plugin parameter " + juce::String (t.param + 1) + " (no plugin there)";
            const juce::String pluginName (plugin->displayName());
            // A channel is named after its plugin when it is created: "SLS Test Synth Cutoff", not "SLS Test Synth SLS Test Synth Cutoff".
            const auto prefix = where == pluginName ? where : where + " " + pluginName;
            return prefix + " "
                 + (t.param < plugin->parameterCount() ? juce::String (plugin->parameterName (t.param)) : "parameter " + juce::String (t.param + 1));
        }
    }
    return "Automation";
}

juce::String MainComponent::automationValueText (const AutomationTarget& t, float v) const
{
    switch (t.kind)
    {
        case AutomationKind::ChannelVolume:
        case AutomationKind::TrackVolume:
        case AutomationKind::TrackSend:
        case AutomationKind::AuxVolume:
        case AutomationKind::MasterVolume:
        case AutomationKind::VcaLevel:      return MixerStrip::volumeToText (v);
        case AutomationKind::ChannelPan:
        case AutomationKind::TrackPan:
        case AutomationKind::AuxPan:        return MixerStrip::panToText (v);
        case AutomationKind::ChannelPitch:
        case AutomationKind::MasterPitch:   return (v > 0.0f ? "+" : "") + juce::String (v, 2) + " st"; // 0.80.0
        case AutomationKind::ChannelSwing:  return juce::String (juce::roundToInt (v * 100.0f)) + " %";
        case AutomationKind::ChannelMute:
        case AutomationKind::TrackMute:
        case AutomationKind::AuxMute:       return v >= 0.5f ? "Muted" : "On";
        case AutomationKind::ChannelSolo:
        case AutomationKind::TrackSolo:     return v >= 0.5f ? "Solo" : "Off";
        case AutomationKind::TrackEffect:
        case AutomationKind::MasterEffect:
        case AutomationKind::AuxEffect:
        {
            const auto info = effectParamInfo (effectTypeFor (t), effectParamIndex (t.param));
            const double range = static_cast<double> (info.max) - static_cast<double> (info.min);
            const int decimals = range >= 100.0 ? 0 : (range >= 10.0 ? 1 : 2);
            const juce::String unit (info.unit);
            return juce::String (v, decimals) + (unit.isEmpty() ? juce::String() : " " + unit);
        }
        case AutomationKind::ChannelPlugin:
        case AutomationKind::TrackPlugin:
        case AutomationKind::MasterPlugin:
        case AutomationKind::AuxPlugin:
        {
            const auto plugin = pluginFor (t);
            const auto text = plugin != nullptr && t.param < plugin->parameterCount() ? juce::String (plugin->parameterText (t.param, v)) : juce::String();
            return text.isNotEmpty() ? text : juce::String (juce::roundToInt (v * 100.0f)) + " %";
        }
    }
    return juce::String (v, 2);
}

void MainComponent::createAutomationFor (const AutomationTarget& target)
{
    if (isPluginAutomation (target.kind))
    {
        const auto plugin = pluginFor (target);
        if (plugin == nullptr || ! plugin->isLoaded() || plugin->parameterCount() == 0)
        {
            statusLabel.setText (plugin == nullptr ? "There is no plugin here to automate."
                                                   : juce::String (plugin->displayName()) + " has no parameters to automate"
                                                         + (plugin->isLoaded() ? "." : " (it is not installed on this computer)."),
                                 juce::dontSendNotification);
            return;
        }
        if (target.param < 0 || target.param >= plugin->parameterCount())
        {
            // Which parameter: the plugin's list, the one last moved in its window chosen at first.
            PluginParameterPicker::show (plugin, "Automate a parameter of " + juce::String (plugin->displayName()),
                                         [safe = juce::Component::SafePointer<MainComponent> (this), target] (int param)
                                         {
                                             if (safe == nullptr)
                                                 return;
                                             auto t = target;
                                             t.param = param;
                                             safe->createAutomationFor (t);
                                         });
            return;
        }
    }

    const auto type = effectTypeFor (target);
    if (isEffectAutomation (target.kind) && (type == EffectType::None || target.param >= effectParamCount (type)))
    {
        statusLabel.setText ("There is no effect parameter to automate here.", juce::dontSendNotification);
        return;
    }

    // A clip lasts one length of the current pattern. It starts at the playhead's while the song plays, otherwise at the start of the song.
    const int playhead = songMode && engine.isPlaying() ? engine.getPlayheadTick() : 0;
    const int unit = currentPatternTicks();
    const int start = (std::max (0, playhead) / unit) * unit;
    const auto range = automationRange (target, type);
    const float value = std::clamp (currentValueOf (target), range.min, range.max);

    auto clip = makeFlatAutomationClip (target, 0, start, unit, value);
    if (isSwitchAutomation (target.kind)) // 0.80.0: a switch jumps, it does not slide between on and off
        for (auto& p : clip.points)
            p.shape = AutomationShape::Hold;
    const int index = playlistView.addAutomationClip (clip, start);
    if (index < 0)
    {
        statusLabel.setText ("No room for another automation clip (the four lanes are full there, or the limit of "
                                 + juce::String (kMaxAutomationClips) + " clips is reached).", juce::dontSendNotification);
        return;
    }

    showPanel (playlistPanel);
    statusLabel.setText ("Automation clip created for " + automationLabelFor (target)
                             + " - it plays in Song mode. Double-click it to edit the points.", juce::dontSendNotification);
    openAutomationEditor (index);
}

void MainComponent::closeAutomationEditor()
{
    automationWindow.reset();
    automationEditing = -1;
}

void MainComponent::openAutomationEditor (int clipIndex)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int> (arrangement.automationClips.size()))
        return;

    if (automationWindow == nullptr)
    {
        automationWindow = std::make_unique<AutomationEditorWindow>();
        automationWindow->onClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
                                                                               { if (safe != nullptr) safe->closeAutomationEditor(); }); };
        auto& ed = automationWindow->editor();
        ed.getClip = [this]() -> AutomationClip*
        {
            return automationEditing >= 0 && automationEditing < static_cast<int> (arrangement.automationClips.size())
                       ? &arrangement.automationClips[static_cast<size_t> (automationEditing)] : nullptr;
        };
        ed.getRange = [this]
        {
            auto* c = automationEditing >= 0 && automationEditing < static_cast<int> (arrangement.automationClips.size())
                          ? &arrangement.automationClips[static_cast<size_t> (automationEditing)] : nullptr;
            return c != nullptr ? automationRange (c->target, effectTypeFor (c->target)) : AutomationRange {};
        };
        ed.getTitle = [this]
        {
            return automationEditing >= 0 && automationEditing < static_cast<int> (arrangement.automationClips.size())
                       ? automationLabelFor (arrangement.automationClips[static_cast<size_t> (automationEditing)].target) : juce::String();
        };
        ed.valueToText = [this] (float v)
        {
            return automationEditing >= 0 && automationEditing < static_cast<int> (arrangement.automationClips.size())
                       ? automationValueText (arrangement.automationClips[static_cast<size_t> (automationEditing)].target, v) : juce::String (v, 2);
        };
        ed.beforeChange = [this] { playlistView.pushUndoPoint(); };
        ed.onChanged = [this]
        {
            markDirty();
            if (songMode)
                publishProject();
            playlistView.refreshTimeline();
        };
    }

    automationEditing = clipIndex;
    automationWindow->setName ("Automation - " + automationLabelFor (arrangement.automationClips[static_cast<size_t> (clipIndex)].target));
    automationWindow->setVisible (true);
    automationWindow->toFront (true);
    automationWindow->editor().refresh();
}
// While the song plays, the faders, knobs and send sliders that have automation move with it; when it stops they show the
// project's own values again (the automation never changes them).
void MainComponent::followAutomation()
{
    if (! (songMode && engine.isPlaying() && ! followEvents.empty()))
    {
        if (followingAutomation)
        {
            followingAutomation = false;
            mixerPanel.refreshControls();
            mixerPanel.setMasterVolume (volumeSlider.getValue()); // 0.80.0: the master strip and the toolbar's pitch show their own again
            pitchSlider.setValue (masterPitch, juce::dontSendNotification);
            for (auto& strip : strips)
                strip->refreshFromChannel();
        }
        return;
    }
    followingAutomation = true;
    const double tick = engine.getPlayheadTick();
    for (size_t first = 0; first < followEvents.size();)
    {
        const auto target = followEvents[first].target;
        size_t last = first + 1;
        while (last < followEvents.size() && followEvents[last].target.key() == target.key())
            ++last;
        float v = 0.0f;
        if (automationValueFor (followEvents, first, last, tick, v))
        {
            if (isChannelAutomation (target.kind))
            {
                for (auto& strip : strips)
                    if (strip->getIndex() == target.index)
                        strip->showAutomated (target.kind, v);
            }
            else if (target.kind == AutomationKind::MasterVolume) // 0.80.0: on the master strip (the toolbar knob keeps the setting)
                mixerPanel.setMasterVolume (v);
            else if (target.kind == AutomationKind::MasterPitch)
                pitchSlider.setValue (v, juce::dontSendNotification);
            else
                mixerPanel.showAutomated (target, v);
        }
        first = last;
    }
}

} // namespace sls
