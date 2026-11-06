#include "ui/MainComponent.h"

namespace sls
{
// ------------------------------------------------------------------------- Channels: delete, move, clone (0.15.0)
//
// All three go through remapChannels(): the new list of channels is described by the old index each new channel comes from
// (an index may appear twice: a clone; a missing index: deleted). Every pattern's notes and the automation clips that move a
// channel's volume or pan follow, and the strips are built again (they hold references into `channels`).

void MainComponent::remapChannels (const std::vector<int>& newToOld, int newSelected)
{
    commitCurrentPattern();
    closeDrumMachine();          // it points at a channel by index
    closeSamplerSettings();      // so does the sampler window
    closeAutomationEditor();     // its clip index may change
    sampleEditorWindow.reset();  // it may edit a channel that moves or goes
    pianoRoll.setNotes ({});     // also ends a note played by hand

    const size_t oldCount = channels.size();
    std::vector<Channel> rebuilt;
    rebuilt.reserve (newToOld.size());
    for (const int old : newToOld)
    {
        jassert (old >= 0 && static_cast<size_t> (old) < oldCount);
        rebuilt.push_back (channels[static_cast<size_t> (old)]);
    }

    remapArrangementChannels (arrangement, newToOld, oldCount);

    // The strips refer to `channels`: tear them down, refill the vector within its capacity, build them again.
    rackView.setStrips ({});
    strips.clear();
    channels.clear();
    for (auto& c : rebuilt)
        channels.push_back (std::move (c));
    jassert (channels.size() <= channels.capacity());
    for (size_t i = 0; i < channels.size(); ++i)
        createStrip (i);
    refreshRackStrips();

    playlistView.clearHistory(); // its snapshots hold automation clips with the old channel numbers
    resetNoteHistory();
    playlistView.refreshTimeline();
    selectChannel (std::clamp (newSelected, 0, static_cast<int> (channels.size()) - 1));
    updateTrackRouting();
    publishProject();
}

void MainComponent::deleteChannel (int index)
{
    if (index < 0 || index >= static_cast<int> (channels.size()))
        return;
    if (channels.size() <= 1)
    {
        statusLabel.setText ("The last channel cannot be deleted", juce::dontSendNotification);
        return;
    }
    const auto name = juce::String (channels[static_cast<size_t> (index)].name);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete channel",
                                        "Delete \"" + name + "\" and its notes in every pattern?\n(Edit > Undo brings it back.)",
                                        "Delete", "Cancel", this,
                                        juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this), index, name] (int result)
                                        {
                                            if (safe == nullptr || result == 0 || index >= static_cast<int> (safe->channels.size()))
                                                return;
                                            std::vector<int> order;
                                            for (int i = 0; i < static_cast<int> (safe->channels.size()); ++i)
                                                if (i != index)
                                                    order.push_back (i);
                                            safe->remapChannels (order, std::min (index, static_cast<int> (order.size()) - 1));
                                            safe->statusLabel.setText ("Deleted channel " + name, juce::dontSendNotification);
                                        }));
}

void MainComponent::moveChannel (int index, int delta)
{
    const int target = index + delta;
    if (index < 0 || index >= static_cast<int> (channels.size()) || target < 0 || target >= static_cast<int> (channels.size()))
        return;
    std::vector<int> order (channels.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = static_cast<int> (i);
    std::swap (order[static_cast<size_t> (index)], order[static_cast<size_t> (target)]);
    remapChannels (order, target);
}

void MainComponent::cloneChannel (int index)
{
    if (index < 0 || index >= static_cast<int> (channels.size()))
        return;
    if (channels.size() >= static_cast<size_t> (kMaxChannels))
    {
        statusLabel.setText ("All " + juce::String (kMaxChannels) + " channels are in use", juce::dontSendNotification);
        return;
    }
    std::vector<int> order;
    for (int i = 0; i < static_cast<int> (channels.size()); ++i)
    {
        order.push_back (i);
        if (i == index)
            order.push_back (i);
    }
    remapChannels (order, index + 1);
    auto& copy = channels[static_cast<size_t> (index) + 1];
    copy.name = (juce::String (copy.name).substring (0, 12) + " 2").toStdString();
    if (copy.instrument == Instrument::Plugin && copy.plugin != nullptr)
        copy.plugin = duplicatePlugin (*copy.plugin); // one instance per channel: a plugin is never run twice per block
    strips[static_cast<size_t> (index) + 1]->refreshFromChannel();
    publishProject();
    statusLabel.setText ("Cloned channel " + juce::String (channels[static_cast<size_t> (index)].name) + " (with its notes in every pattern)",
                         juce::dontSendNotification);
}
} // namespace sls
