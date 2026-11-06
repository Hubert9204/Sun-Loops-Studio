// Part of MainComponent: the synth window (0.18.0, block F).
#include "ui/MainComponent.h"

namespace sls
{
void MainComponent::openSynthSettings (int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()) || channels[static_cast<size_t> (channelIndex)].instrument != Instrument::Synth)
        return;
    using Safe = juce::Component::SafePointer<MainComponent>;
    if (synthWindow == nullptr)
    {
        synthWindow = std::make_unique<SynthWindow>();
        synthWindow->onClose = [safe = Safe (this)]
        {
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeSynthSettings(); });
        };
        auto& panel = synthWindow->panel();
        panel.getChannel = [safe = Safe (this)]() -> Channel*
        {
            if (safe == nullptr)
                return nullptr;
            const int i = safe->synthWindowChannel;
            if (i < 0 || i >= static_cast<int> (safe->channels.size()) || safe->channels[static_cast<size_t> (i)].instrument != Instrument::Synth)
                return nullptr;
            return &safe->channels[static_cast<size_t> (i)];
        };
        panel.onChanged = [safe = Safe (this)]
        {
            if (safe != nullptr)
                safe->publishProject();
        };
        panel.onPreview = [safe = Safe (this)] (bool down)
        {
            if (safe == nullptr || safe->synthWindowChannel < 0)
                return;
            if (down)
                safe->engine.liveNoteOn (safe->synthWindowChannel, 60);
            else
                safe->engine.liveNoteOff (safe->synthWindowChannel, 60);
        };
    }
    if (synthWindowChannel >= 0 && synthWindowChannel != channelIndex)
        engine.liveNoteOff (synthWindowChannel, 60);
    synthWindowChannel = channelIndex;
    selectChannel (channelIndex);
    synthWindow->setName ("Synth - " + juce::String (channels[static_cast<size_t> (channelIndex)].name));
    synthWindow->panel().refresh();
    synthWindow->setVisible (true);
    synthWindow->toFront (true);
}

void MainComponent::closeSynthSettings()
{
    if (synthWindowChannel >= 0)
        engine.liveNoteOff (synthWindowChannel, 60);
    synthWindow.reset();
    synthWindowChannel = -1;
}
} // namespace sls
