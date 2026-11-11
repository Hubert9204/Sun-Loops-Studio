#include "ui/MainComponent.h"

#include "library/DrumKitFile.h"

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
        view.setKitsFolder (library.drumKits);
        view.setLoopTicks (arrangement.patternTicks);
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
} // namespace sls
