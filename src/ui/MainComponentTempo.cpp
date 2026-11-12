// Part of MainComponent (0.46.0): the time signature and the song's tempo changes, from the button next to the BPM.
#include "ui/MainComponent.h"

#include "audio/TimeFormat.h"
#include "ui/TextPrompt.h"

namespace sls
{
// Everything that draws bars follows the project's time signature.
void MainComponent::showMeter()
{
    interfaceMeter() = arrangement.meter;
    meterButton.setButtonText (juce::String (arrangement.meter.text()) + (arrangement.tempoMarkers.empty() ? "" : " *"));
    meterButton.setTooltip ("Time signature " + juce::String (arrangement.meter.text())
                            + (arrangement.tempoMarkers.empty() ? juce::String (", one tempo")
                                                                : ", " + juce::String (static_cast<int> (arrangement.tempoMarkers.size())) + " tempo change(s) in the song")
                            + ". Click to change the time signature or add tempo changes.");
    playlistView.meterChanged();
    pianoRoll.repaint();
}

void MainComponent::tempoChanged (const juce::String& message)
{
    arrangement.tempoMarkers = tidyTempoMarkers (arrangement.tempoMarkers);
    showMeter();
    markDirty();
    recordHistory();
    publishProject();
    statusLabel.setText (message, juce::dontSendNotification);
}

void MainComponent::showTempoMenu()
{
    juce::PopupMenu meters;
    for (size_t i = 0; i < TimeSignature::choices().size(); ++i)
    {
        const auto& m = TimeSignature::choices()[i];
        meters.addItem (100 + static_cast<int> (i), juce::String (m.text()), true, m == arrangement.meter);
    }

    // At the song position (on its beat); at the start of the song, where the song's own tempo is, at bar 2 instead.
    const int songBeat = songStartTick / interfaceMeter().beatTicks() * interfaceMeter().beatTicks();
    const int at = songBeat > 0 ? songBeat : interfaceMeter().barTicks();
    juce::PopupMenu menu;
    menu.addSubMenu ("Time signature (" + juce::String (arrangement.meter.text()) + ")", meters);
    menu.addSeparator();
    menu.addSectionHeader ("Tempo changes (Song mode; the song starts at " + juce::String (bpmSlider.getValue(), 0) + " BPM)");
    for (size_t i = 0; i < arrangement.tempoMarkers.size(); ++i)
    {
        const auto& m = arrangement.tempoMarkers[i];
        juce::PopupMenu one;
        one.addItem (1000 + static_cast<int> (i), "Change the tempo...");
        one.addItem (2000 + static_cast<int> (i), "Move to the song position (" + juce::String (formatBarsBeatsTicks (songBeat)) + ")", songBeat > 0);
        one.addItem (3000 + static_cast<int> (i), "Delete");
        menu.addSubMenu ("At " + juce::String (formatBarsBeatsTicks (m.tick)) + ": " + juce::String (m.bpm, 1) + " BPM", one);
    }
    menu.addItem (1, songBeat > 0 ? "Add a tempo change at the song position (" + juce::String (formatBarsBeatsTicks (at)) + ")..."
                                  : "Add a tempo change at " + juce::String (formatBarsBeatsTicks (at)) + "...");
    menu.addItem (2, "Delete every tempo change", ! arrangement.tempoMarkers.empty());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&meterButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this), at, songBeat] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& self = *safe;
        auto& markers = self.arrangement.tempoMarkers;
        const auto askTempo = [safe] (const juce::String& title, double current, std::function<void (double)> use)
        {
            promptForText (*safe, title, "Beats per minute (20 - 400)", juce::String (current, 1), "OK",
                           [use = std::move (use)] (const juce::String& typed)
                           {
                               const double v = typed.trim().getDoubleValue();
                               if (v >= kMinBpm && v <= kMaxBpm)
                                   use (v);
                           });
        };
        if (result >= 100 && result < 100 + static_cast<int> (TimeSignature::choices().size()))
        {
            const auto chosen = TimeSignature::choices()[static_cast<size_t> (result - 100)];
            self.arrangement.meter = chosen;
            self.commitCurrentPattern();
            self.arrangement.lengthenPatternsTo (self.arrangement.minPatternBeats() * kTicksPerBeat);
            self.showPatternLength();
            self.tempoChanged ("Time signature " + juce::String (chosen.text()));
        }
        else if (result == 1)
            askTempo ("Tempo change at " + juce::String (formatBarsBeatsTicks (at)), self.bpmSlider.getValue(), [safe, at] (double bpm)
            {
                if (safe == nullptr)
                    return;
                safe->arrangement.tempoMarkers.push_back ({ at, bpm });
                safe->tempoChanged ("Tempo " + juce::String (bpm, 1) + " BPM from " + juce::String (formatBarsBeatsTicks (at)));
            });
        else if (result == 2)
        {
            markers.clear();
            self.tempoChanged ("Every tempo change deleted: the song plays at one tempo");
        }
        else if (result >= 1000 && result < 1000 + static_cast<int> (markers.size()))
        {
            const auto index = static_cast<size_t> (result - 1000);
            askTempo ("Tempo at " + juce::String (formatBarsBeatsTicks (markers[index].tick)), markers[index].bpm, [safe, index] (double bpm)
            {
                if (safe == nullptr || index >= safe->arrangement.tempoMarkers.size())
                    return;
                safe->arrangement.tempoMarkers[index].bpm = bpm;
                safe->tempoChanged ("Tempo changed to " + juce::String (bpm, 1) + " BPM");
            });
        }
        else if (result >= 2000 && result < 2000 + static_cast<int> (markers.size()) && songBeat > 0)
        {
            markers[static_cast<size_t> (result - 2000)].tick = songBeat;
            self.tempoChanged ("Tempo change moved to " + juce::String (formatBarsBeatsTicks (songBeat)));
        }
        else if (result >= 3000 && result < 3000 + static_cast<int> (markers.size()))
        {
            markers.erase (markers.begin() + (result - 3000));
            self.tempoChanged ("Tempo change deleted");
        }
    });
}
} // namespace sls
