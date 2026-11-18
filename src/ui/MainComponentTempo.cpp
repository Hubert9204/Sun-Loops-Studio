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
    interfaceMeterMarkers() = arrangement.meterMarkers; // 0.83.0
    const bool changes = ! arrangement.tempoMarkers.empty() || ! arrangement.meterMarkers.empty();
    meterButton.setButtonText (juce::String (arrangement.meter.text()) + (changes ? " *" : ""));
    meterButton.setTooltip ("Time signature " + juce::String (arrangement.meter.text())
                            + (arrangement.tempoMarkers.empty() ? juce::String (", one tempo")
                                                                : ", " + juce::String (static_cast<int> (arrangement.tempoMarkers.size())) + " tempo change(s) in the song")
                            + (arrangement.meterMarkers.empty() ? juce::String()
                                                                : ", " + juce::String (static_cast<int> (arrangement.meterMarkers.size())) + " time signature change(s)")
                            + ". Click to change the time signature or add tempo and time signature changes.");
    playlistView.meterChanged();
    pianoRoll.repaint();
}

void MainComponent::tempoChanged (const juce::String& message)
{
    arrangement.tempoMarkers = tidyTempoMarkers (arrangement.tempoMarkers);
    arrangement.meterMarkers = tidyMeterMarkers (arrangement.meterMarkers, arrangement.meter); // 0.83.0
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
    const auto here = barPositionAt (arrangement.meterMarkers, arrangement.meter, songStartTick);
    const int songBeat = songStartTick - here.ticks;
    const int at = songBeat > 0 ? songBeat : interfaceMeter().barTicks();
    // 0.83.0: a time signature change goes on a bar line: the one at the song position or the next one (bar 2 at the start)
    const int songBar = static_cast<int> (barStartTick (arrangement.meterMarkers, arrangement.meter, here.bar + (here.beat > 0 || here.ticks > 0 ? 1 : 0)));
    const int meterAt = songBar > 0 ? songBar : interfaceMeter().barTicks();
    juce::PopupMenu menu;
    menu.addSubMenu ("Time signature (" + juce::String (arrangement.meter.text()) + ")", meters);
    menu.addSeparator();
    menu.addSectionHeader ("Tempo changes (Song mode; the song starts at " + juce::String (bpmSlider.getValue(), 0) + " BPM)");
    for (size_t i = 0; i < arrangement.tempoMarkers.size(); ++i)
    {
        const auto& m = arrangement.tempoMarkers[i];
        juce::PopupMenu one;
        one.addItem (1000 + static_cast<int> (i), "Change the tempo...");
        one.addItem (4000 + static_cast<int> (i), "Ramp to it (glide from the tempo before)", true, m.ramp); // 0.83.0
        one.addItem (2000 + static_cast<int> (i), "Move to the song position (" + juce::String (formatBarsBeatsTicks (songBeat)) + ")", songBeat > 0);
        one.addItem (3000 + static_cast<int> (i), "Delete");
        menu.addSubMenu ("At " + juce::String (formatBarsBeatsTicks (m.tick)) + ": " + (m.ramp ? "ramp to " : "") + juce::String (m.bpm, 1) + " BPM", one);
    }
    menu.addItem (1, songBeat > 0 ? "Add a tempo change at the song position (" + juce::String (formatBarsBeatsTicks (at)) + ")..."
                                  : "Add a tempo change at " + juce::String (formatBarsBeatsTicks (at)) + "...");
    menu.addItem (2, "Delete every tempo change", ! arrangement.tempoMarkers.empty());

    // 0.83.0: time signature changes in the song
    menu.addSeparator();
    menu.addSectionHeader ("Time signature changes (Song mode; the song starts in " + juce::String (arrangement.meter.text()) + ")");
    for (size_t i = 0; i < arrangement.meterMarkers.size(); ++i)
    {
        const auto& m = arrangement.meterMarkers[i];
        juce::PopupMenu one, choices;
        for (size_t k = 0; k < TimeSignature::choices().size(); ++k)
            choices.addItem (10000 + static_cast<int> (i) * 100 + static_cast<int> (k), juce::String (TimeSignature::choices()[k].text()), true,
                             TimeSignature::choices()[k] == m.meter);
        one.addSubMenu ("Time signature", choices);
        one.addItem (5000 + static_cast<int> (i), "Delete");
        menu.addSubMenu ("At bar " + juce::String (barPositionAt (arrangement.meterMarkers, arrangement.meter, m.tick).bar + 1) + ": "
                             + juce::String (m.meter.text()), one);
    }
    juce::PopupMenu addMeter;
    for (size_t k = 0; k < TimeSignature::choices().size(); ++k)
        addMeter.addItem (200 + static_cast<int> (k), juce::String (TimeSignature::choices()[k].text()));
    menu.addSubMenu ("Add a time signature change at bar " + juce::String (barPositionAt (arrangement.meterMarkers, arrangement.meter, meterAt).bar + 1),
                     addMeter, arrangement.meterMarkers.size() < static_cast<size_t> (kMaxMeterMarkers));
    menu.addItem (3, "Delete every time signature change", ! arrangement.meterMarkers.empty());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&meterButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this), at, songBeat, meterAt] (int result)
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
        else if (result >= 4000 && result < 4000 + static_cast<int> (markers.size())) // 0.83.0
        {
            auto& m = markers[static_cast<size_t> (result - 4000)];
            m.ramp = ! m.ramp;
            self.tempoChanged (m.ramp ? "The tempo now glides to " + juce::String (m.bpm, 1) + " BPM from the tempo before"
                                      : "The tempo now changes at once to " + juce::String (m.bpm, 1) + " BPM");
        }
        else if (result >= 200 && result < 200 + static_cast<int> (TimeSignature::choices().size()))
        {
            const auto chosen = TimeSignature::choices()[static_cast<size_t> (result - 200)];
            auto& meters = self.arrangement.meterMarkers;
            meters.erase (std::remove_if (meters.begin(), meters.end(), [meterAt] (const MeterMarker& m) { return m.tick == meterAt; }), meters.end());
            meters.push_back ({ meterAt, chosen });
            self.tempoChanged ("Time signature " + juce::String (chosen.text()) + " from bar "
                               + juce::String (barPositionAt (meters, self.arrangement.meter, meterAt).bar + 1));
        }
        else if (result == 3)
        {
            self.arrangement.meterMarkers.clear();
            self.tempoChanged ("Every time signature change deleted: the song is in " + juce::String (self.arrangement.meter.text()));
        }
        else if (result >= 5000 && result < 5000 + static_cast<int> (self.arrangement.meterMarkers.size()))
        {
            self.arrangement.meterMarkers.erase (self.arrangement.meterMarkers.begin() + (result - 5000));
            self.tempoChanged ("Time signature change deleted");
        }
        else if (result >= 10000)
        {
            const auto index = static_cast<size_t> ((result - 10000) / 100), choice = static_cast<size_t> ((result - 10000) % 100);
            if (index < self.arrangement.meterMarkers.size() && choice < TimeSignature::choices().size())
            {
                self.arrangement.meterMarkers[index].meter = TimeSignature::choices()[choice];
                self.tempoChanged ("Time signature change set to " + juce::String (TimeSignature::choices()[choice].text()));
            }
        }
    });
}
} // namespace sls
