// Part of MainComponent (0.45.0): effect-chain presets - a track's slots saved as a .slchain file in Presets/Effect chains,
// loaded onto any track, aux bus or the master, or cleared.
#include "ui/MainComponent.h"

#include "ui/TextPrompt.h"

namespace sls
{
namespace
{
juce::File chainFolder (const LibraryPaths& library)
{
    return library.presets.getChildFile ("Effect chains");
}
} // namespace

void MainComponent::showEffectChainMenu (juce::Component& button, MixerTrack& track, int trackIndex)
{
    const auto folder = chainFolder (library);
    auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kEffectChainExtension);
    files.sort();

    juce::PopupMenu load;
    for (int i = 0; i < files.size(); ++i)
        load.addItem (100 + i, files[i].getFileNameWithoutExtension());
    bool anyEffect = false;
    for (const auto& s : track.slots)
        anyEffect = anyEffect || s.type != EffectType::None;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Effect chain");
    menu.addItem (1, "Save this chain as a preset...", anyEffect);
    menu.addSubMenu ("Load a chain (replaces every slot)", load, ! files.isEmpty());
    menu.addItem (2, "Clear every slot", anyEffect);
    menu.addSeparator();
    menu.addItem (3, "Show the chains folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&button),
                        [safe = juce::Component::SafePointer<MainComponent> (this), trackPtr = &track, trackIndex, files, folder] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& self = *safe;
        const auto applied = [&self, trackIndex] (const juce::String& message)
        {
            self.mixerPanel.refreshTrack (trackIndex);
            self.mixerPanel.refreshEffects();
            self.publishProject();
            self.statusLabel.setText (message, juce::dontSendNotification);
        };
        if (result == 1)
        {
            promptForText (self, "Save effect chain", "A name for the preset (saved in " + folder.getFullPathName() + ")", trackPtr->name, "Save",
                           [safe, trackPtr, folder] (const juce::String& typed)
            {
                const auto name = juce::File::createLegalFileName (typed.trim());
                if (safe == nullptr || name.isEmpty())
                    return;
                folder.createDirectory();
                int leftOut = 0;
                const auto json = effectChainToJson (trackPtr->slots, &leftOut);
                const auto file = folder.getChildFile (name + kEffectChainExtension);
                if (! file.replaceWithText (json))
                {
                    safe->statusLabel.setText ("Could not write " + file.getFullPathName(), juce::dontSendNotification);
                    return;
                }
                safe->statusLabel.setText ("Saved the effect chain \"" + name + "\""
                                               + (leftOut > 0 ? " (" + juce::String (leftOut) + " plugin slot(s) left out: presets keep built-in effects only)" : juce::String()),
                                           juce::dontSendNotification);
            });
        }
        else if (result == 2)
        {
            trackPtr->slots = {};
            applied ("Cleared every effect slot");
        }
        else if (result == 3)
        {
            folder.createDirectory();
            folder.revealToUser();
        }
        else if (result >= 100 && result - 100 < files.size())
        {
            const auto& file = files[result - 100];
            const auto chain = effectChainFromJson (file.loadFileAsString());
            if (! chain)
            {
                self.statusLabel.setText (file.getFileName() + " is not an effect chain", juce::dontSendNotification);
                return;
            }
            trackPtr->slots = *chain;
            applied ("Loaded the effect chain \"" + file.getFileNameWithoutExtension() + "\"");
        }
    });
}
} // namespace sls
