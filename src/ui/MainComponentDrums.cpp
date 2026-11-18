#include "ui/MainComponent.h"

#include "library/DrumKitFile.h"
#include "audio/MixerEdit.h"

namespace sls
{
// The kit a new drum channel starts with: the "Electronic Punch" kit (16 pads in the General MIDI order, hats in choke group 1).
std::shared_ptr<const DrumKit> MainComponent::defaultDrumKit()
{
    juce::StringArray problems;
    return std::make_shared<const DrumKit> (loadKit (builtinKitSpec ("gen:punch"), problems));
}

// "Drum machine..." on a drum channel: one window, for one channel at a time. It edits the channel's kit and its notes in the
// current pattern, through the callbacks below (the project is published and the rack and piano roll follow).
void MainComponent::openDrumMachine (int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()) || channels[static_cast<size_t> (channelIndex)].instrument != Instrument::Drums)
        return;
    selectChannel (channelIndex);

    const bool needsKit = channels[static_cast<size_t> (channelIndex)].kit == nullptr;
    if (needsKit)
        channels[static_cast<size_t> (channelIndex)].kit = defaultDrumKit();

    if (drumWindow == nullptr)
    {
        drumWindow = std::make_unique<DrumMachineWindow> (engine);
        drumWindow->onClose = [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeDrumMachine(); });
        };
        auto& view = drumWindow->view();
        view.getChannel = [safe = juce::Component::SafePointer<MainComponent> (this)]() -> Channel*
        {
            if (safe == nullptr)
                return nullptr;
            const int i = safe->drumWindowChannel;
            if (i < 0 || i >= static_cast<int> (safe->channels.size()) || safe->channels[static_cast<size_t> (i)].instrument != Instrument::Drums)
                return nullptr;
            return &safe->channels[static_cast<size_t> (i)];
        };
        view.onKitChanged = [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe == nullptr)
                return;
            const int i = safe->drumWindowChannel;
            if (i >= 0 && i < static_cast<int> (safe->strips.size()))
                safe->strips[static_cast<size_t> (i)]->refreshFromChannel();
            safe->publishProject();
        };
        view.onNotesChanged = [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe == nullptr)
                return;
            const int i = safe->drumWindowChannel;
            if (i < 0 || i >= static_cast<int> (safe->strips.size()))
                return;
            safe->strips[static_cast<size_t> (i)]->refreshFromChannel();
            if (i == safe->selected)
                safe->pianoRoll.setNotes (safe->channels[static_cast<size_t> (i)].notes);
            safe->publishProject();
        };
        view.onStatus = [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::String& text)
        {
            if (safe != nullptr)
                safe->statusLabel.setText (text, juce::dontSendNotification);
        };
        view.getTrackCount = [safe = juce::Component::SafePointer<MainComponent> (this)]
        { return safe != nullptr ? static_cast<int> (safe->tracks.size()) : 8; }; // 0.84.0
        view.onPadsToFreeTracks = [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->drumPadsToFreeTracks (safe->drumWindowChannel);
        };
        view.setKitsFolder (library.drumKits);
        view.setLoopTicks (currentPatternTicks());
    }

    drumWindowChannel = channelIndex;
    drumWindow->view().setChannelIndex (channelIndex);
    drumWindow->view().selectPad (0);
    drumWindow->setName ("Drum machine - " + juce::String (channels[static_cast<size_t> (channelIndex)].name));
    drumWindow->setVisible (true);
    drumWindow->toFront (true);
    if (needsKit)
        publishProject(); // the kit was just created
}

void MainComponent::closeDrumMachine()
{
    drumWindow.reset();
    drumWindowChannel = -1;
}
// 0.84.0, as FL Studio's FPC with its outputs: every pad of the channel's kit that has a sound and no mixer track of its own gets a free one
// (nothing plays through it, no effects, nothing sent to it; new tracks are added at the end when there are not enough), named after the pad.
void MainComponent::drumPadsToFreeTracks (int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()) || channels[static_cast<size_t> (channelIndex)].kit == nullptr)
        return;
    auto kit = *channels[static_cast<size_t> (channelIndex)].kit;
    std::vector<int> wanting;
    for (int p = 0; p < kDrumPads; ++p)
        if (kit.pads[static_cast<size_t> (p)].hasSample() && kit.pads[static_cast<size_t> (p)].outputTrack < 0)
            wanting.push_back (p);
    if (wanting.empty())
    {
        statusLabel.setText ("Every pad with a sound already has its own mixer track", juce::dontSendNotification);
        return;
    }
    std::vector<int> free;
    for (int t = 1; t < static_cast<int> (tracks.size()) && free.size() < wanting.size(); ++t)
        if (mixerTrackUse (tracks, channels, arrangement, console, masterTrack.slots, t).empty())
            free.push_back (t);
    const int missing = static_cast<int> (wanting.size() - free.size());
    if (missing > 0)
    {
        const int room = kMaxTracks - static_cast<int> (tracks.size());
        const int first = static_cast<int> (tracks.size());
        setTrackCount (tracks.size() + static_cast<size_t> (std::clamp (missing, 0, room)));
        for (int t = first; t < static_cast<int> (tracks.size()); ++t)
            free.push_back (t);
    }
    int given = 0;
    for (size_t i = 0; i < wanting.size() && i < free.size(); ++i)
    {
        auto& pad = kit.pads[static_cast<size_t> (wanting[i])];
        pad.outputTrack = free[i];
        tracks[static_cast<size_t> (free[i])].name = pad.name.substr (0, 24);
        mixerPanel.refreshTrack (free[i]);
        ++given;
    }
    channels[static_cast<size_t> (channelIndex)].kit = std::make_shared<const DrumKit> (std::move (kit));
    if (drumWindow != nullptr)
        drumWindow->view().refresh();
    statusLabel.setText (juce::String (given) + (given == 1 ? " pad plays" : " pads play") + " into a mixer track of its own"
                             + (given < static_cast<int> (wanting.size()) ? " (the mixer is full: the others still play through the channel)" : juce::String()),
                         juce::dontSendNotification);
    publishProject();
}
} // namespace sls
