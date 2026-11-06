// Automation: creating a clip from a control (right click), naming what a clip moves, and the point editor window.
#include "ui/MainComponent.h"

#include "ui/AutomationEditor.h"

namespace sls
{
namespace
{
const char* effectParamNameFor (EffectType type, int param)
{
    return param >= 0 && param < effectParamCount (type) ? effectParamInfo (type, param).name : "parameter";
}
}

EffectType MainComponent::effectTypeFor (const AutomationTarget& t) const
{
    if (t.slot < 0 || t.slot >= kSlotsPerTrack)
        return EffectType::None;
    if (t.kind == AutomationKind::MasterEffect)
        return masterTrack.slots[static_cast<size_t> (t.slot)].type;
    if (t.kind == AutomationKind::TrackEffect && t.index >= 0 && t.index < static_cast<int> (tracks.size()))
        return tracks[static_cast<size_t> (t.index)].slots[static_cast<size_t> (t.slot)].type;
    return EffectType::None;
}

float MainComponent::currentValueOf (const AutomationTarget& t) const
{
    const auto at = [] (const auto& v, int i) { return i >= 0 && i < static_cast<int> (v.size()); };
    switch (t.kind)
    {
        case AutomationKind::ChannelVolume: if (at (channels, t.index)) return channels[static_cast<size_t> (t.index)].volume; break;
        case AutomationKind::ChannelPan:    if (at (channels, t.index)) return channels[static_cast<size_t> (t.index)].pan; break;
        case AutomationKind::TrackVolume:   if (at (tracks, t.index)) return tracks[static_cast<size_t> (t.index)].volume; break;
        case AutomationKind::TrackPan:      if (at (tracks, t.index)) return tracks[static_cast<size_t> (t.index)].pan; break;
        case AutomationKind::TrackEffect:
            if (at (tracks, t.index) && t.slot >= 0 && t.slot < kSlotsPerTrack && t.param >= 0 && t.param < kMaxEffectParams)
                return tracks[static_cast<size_t> (t.index)].slots[static_cast<size_t> (t.slot)].params[static_cast<size_t> (t.param)];
            break;
        case AutomationKind::MasterEffect:
            if (t.slot >= 0 && t.slot < kSlotsPerTrack && t.param >= 0 && t.param < kMaxEffectParams)
                return masterTrack.slots[static_cast<size_t> (t.slot)].params[static_cast<size_t> (t.param)];
            break;
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
    switch (t.kind)
    {
        case AutomationKind::ChannelVolume: return channelName (t.index) + " volume";
        case AutomationKind::ChannelPan:    return channelName (t.index) + " pan";
        case AutomationKind::TrackVolume:   return "Track " + juce::String (t.index + 1) + " volume";
        case AutomationKind::TrackPan:      return "Track " + juce::String (t.index + 1) + " balance";
        case AutomationKind::TrackEffect:
        {
            const auto type = effectTypeFor (t);
            return "Track " + juce::String (t.index + 1) + " " + effectName (type) + " " + effectParamNameFor (type, t.param);
        }
        case AutomationKind::MasterEffect:
        {
            const auto type = effectTypeFor (t);
            return juce::String ("Master ") + effectName (type) + " " + effectParamNameFor (type, t.param);
        }
    }
    return "Automation";
}

juce::String MainComponent::automationValueText (const AutomationTarget& t, float v) const
{
    switch (t.kind)
    {
        case AutomationKind::ChannelVolume:
        case AutomationKind::TrackVolume:   return MixerStrip::volumeToText (v);
        case AutomationKind::ChannelPan:
        case AutomationKind::TrackPan:      return MixerStrip::panToText (v);
        case AutomationKind::TrackEffect:
        case AutomationKind::MasterEffect:
        {
            const auto info = effectParamInfo (effectTypeFor (t), t.param);
            const double range = static_cast<double> (info.max) - static_cast<double> (info.min);
            const int decimals = range >= 100.0 ? 0 : (range >= 10.0 ? 1 : 2);
            const juce::String unit (info.unit);
            return juce::String (v, decimals) + (unit.isEmpty() ? juce::String() : " " + unit);
        }
    }
    return juce::String (v, 2);
}

void MainComponent::createAutomationFor (const AutomationTarget& target)
{
    const auto type = effectTypeFor (target);
    if ((target.kind == AutomationKind::TrackEffect || target.kind == AutomationKind::MasterEffect)
        && (type == EffectType::None || target.param >= effectParamCount (type)))
    {
        statusLabel.setText ("There is no effect parameter to automate here.", juce::dontSendNotification);
        return;
    }

    // A clip lasts one pattern. It starts at the playhead's pattern while the song plays, otherwise at the start of the song.
    const int playhead = songMode && engine.isPlaying() ? engine.getPlayheadTick() : 0;
    const int start = (std::max (0, playhead) / kLoopTicks) * kLoopTicks;
    const auto range = automationRange (target, type);
    const float value = std::clamp (currentValueOf (target), range.min, range.max);

    const int index = playlistView.addAutomationClip (makeFlatAutomationClip (target, 0, start, kLoopTicks, value), start);
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
} // namespace sls
