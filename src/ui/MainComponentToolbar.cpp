// The toolbar laid out as FL Studio's (0.60.0): the menu bar and its menus, the time, global snap and CPU panels, the hint
// bar, the Plugin picker's lists and the Channel Rack's options menu. The layout itself is in MainComponent::resized().
#include "ui/MainComponent.h"

#include "audio/TempoMap.h"
#include "audio/TimeFormat.h"

namespace sls
{
namespace
{
juce::PopupMenu::Item menuItem (int id, const juce::String& text, const juce::String& shortcut = {}, bool enabled = true, bool ticked = false)
{
    juce::PopupMenu::Item item (text);
    item.itemID = id;
    item.isEnabled = enabled;
    item.isTicked = ticked;
    if (shortcut.isNotEmpty())
        item.shortcutKeyDescription = shortcut;
    return item;
}
} // namespace

// ------------------------------------------------------------------------- the time panel

void MainComponent::TimePanel::set (const juce::String& bars, const juce::String& clock)
{
    if (bars == barsText && clock == clockText)
        return;
    barsText = bars;
    clockText = clock;
    repaint();
}

void MainComponent::TimePanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff15171b));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
    g.setColour (juce::Colour (0xffe8943a));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (barsText, getLocalBounds().removeFromLeft (getWidth() * 11 / 20).reduced (6, 0), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffb9bfc9));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (clockText, getLocalBounds().removeFromRight (getWidth() * 9 / 20).reduced (6, 0), juce::Justification::centredRight);
}

// ------------------------------------------------------------------------- the readouts (from the timer)

void MainComponent::updateToolbarReadouts()
{
    // The time panel: where the playhead is while playing, where it will start otherwise.
    const int tick = engine.isPlaying() ? engine.getPlayheadTick() : (songMode ? songStartTick : patternStartTick);
    const double seconds = tickToSeconds (arrangement.tempoMarkers, bpmSlider.getValue(), tick);
    const int ms = juce::roundToInt (seconds * 1000.0);
    const int minutes = ms / 60000, secs = (ms / 1000) % 60, millis = ms % 1000;
    timePanel.set (juce::String (formatBarsBeatsTicks (tick)),
                   juce::String (minutes) + ":" + juce::String (secs).paddedLeft ('0', 2) + "." + juce::String (millis).paddedLeft ('0', 3));

    // The CPU panel: the share of each audio buffer's time the audio takes, a few times a second.
    if (++cpuTicks % 5 == 0)
    {
        const double load = engine.getDeviceManager().getCpuUsage();
        const int percent = std::clamp (juce::roundToInt (load * 100.0), 0, 100);
        // 0.62.0: the memory the program holds (as FL Studio's memory read-out), where the system says (Linux: /proc)
        static juce::String memoryText;
        if (cpuTicks % 25 == 0 || memoryText.isEmpty())
        {
            const auto statm = juce::File ("/proc/self/statm").loadFileAsString();
            const auto pages = juce::StringArray::fromTokens (statm, " ", "")[1].getLargeIntValue();
            const auto megabytes = pages * 4096 / (1024 * 1024);
            memoryText = pages <= 0 ? juce::String ("\n")
                       : megabytes < 1000 ? "\nRAM " + juce::String (megabytes) + "M" : "\nRAM " + juce::String (static_cast<double> (megabytes) / 1024.0, 1) + "G";
        }
        cpuLabel.setText ("CPU " + juce::String (percent) + " %" + (memoryText.length() > 1 ? memoryText : juce::String()), juce::dontSendNotification);
        cpuLabel.setColour (juce::Label::textColourId, percent >= 85 ? juce::Colour (0xffd9534f) : percent >= 60 ? juce::Colour (0xffe8b030)
                                                                                                                 : juce::Colour (0xffb9bfc9));
    }

    // The hint bar: what the control under the mouse does (its tooltip), as FL Studio's hint panel.
    hintLabel.setText (hintUnderMouse(), juce::dontSendNotification);
}

juce::String MainComponent::hintUnderMouse() const
{
    const auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumMouseSources(); ++i)
    {
        const auto* source = desktop.getMouseSource (i);
        if (source == nullptr || ! source->canHover())
            continue;
        for (auto* c = source->getComponentUnderMouse(); c != nullptr; c = c->getParentComponent())
        {
            if (auto* tip = dynamic_cast<juce::TooltipClient*> (c))
            {
                const auto text = tip->getTooltip();
                if (text.isNotEmpty())
                    return text.upToFirstOccurrenceOf ("\n", false, false);
            }
            if (c == this)
                break;
        }
    }
    return {};
}

// ------------------------------------------------------------------------- the global snap

void MainComponent::applyMainSnap()
{
    const int id = std::clamp (snapBox.getSelectedId(), 1, 8);
    if (settings != nullptr)
        settings->setValue ("snap.main", id);
    // The piano roll's grid: ticks, 0 = a bar.
    static constexpr int rollTicks[] = { 0, 2 * kTicksPerBeat, kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 4, kTicksPerBeat / 8, kTicksPerBeat / 16, 1 };
    pianoToolbar.setMainSnapTicks (rollTicks[id - 1]);
    // The playlist's code: 0 = a bar, -1 = half a bar, else ticks.
    static constexpr int playlistCodes[] = { 0, -1, kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 4, kTicksPerBeat / 8, kTicksPerBeat / 16, 1 };
    playlistView.setMainSnap (playlistCodes[id - 1]);
}

// ------------------------------------------------------------------------- the piano roll's channel box and ghost notes

void MainComponent::refreshChannelNames()
{
    std::vector<juce::String> names;
    names.reserve (channels.size());
    for (const auto& c : channels)
        names.emplace_back (c.name);
    pianoToolbar.setChannels (names, selected);
    pianoToolbar.refreshGhostNotes();
}

// ------------------------------------------------------------------------- the menus

void MainComponent::showEditMenu()
{
    juce::PopupMenu menu;
    menu.addItem (menuItem (1, "Undo", "Ctrl+Z", ! undoHistory.empty()));
    menu.addItem (menuItem (2, "Redo", "Ctrl+Y", ! redoHistory.empty()));
    menu.addSeparator();
    const bool haveChannel = selected >= 0 && selected < static_cast<int> (channels.size());
    menu.addItem (menuItem (3, "Clear the selected channel's notes", "", haveChannel));
    menu.addItem (menuItem (4, "Rename the selected channel...", "", haveChannel));
    menu.addItem (menuItem (5, "Clone the selected channel", "", haveChannel));
    menu.addItem (menuItem (6, "Delete the selected channel", "", haveChannel && channels.size() > 1));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&editButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            switch (result)
                            {
                                case 1: safe->undoEdit(); break;
                                case 2: safe->redoEdit(); break;
                                case 3: safe->clearButton.triggerClick(); break;
                                case 4: safe->renameChannel (safe->selected); break;
                                case 5: safe->cloneChannel (safe->selected); break;
                                case 6: safe->deleteChannel (safe->selected); break;
                                default: break;
                            }
                        });
}

// ADD: a channel of each built-in instrument, the synthesizers that come with the program and the plugin instruments found
// (the Channel Rack's "+" and the picker's instrument list lead here too).
void MainComponent::showAddMenu (juce::Component& target)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Synth channel");
    menu.addItem (2, "Sampler channel (load a sample)");
    menu.addItem (3, "Drum machine (16 pads)");
    menu.addItem (4, "Slicer (slice a loop)...");
    menu.addItem (8, "Layer channel (its notes are played by other channels)"); // 0.62.0
    juce::String sunburst, hexforge; // the synthesizer plugins that come with the program (0.48.0, 0.50.0)
    std::vector<juce::String> others;
    for (const auto& p : foundPlugins)
        if (p.isInstrument)
        {
            if (p.name == "SunBurst" && p.format == "VST3")
                sunburst = p.identifier;
            else if (p.name == "HexForge" && p.format == "VST3")
                hexforge = p.identifier;
            else
                others.push_back (p.identifier);
        }
    if (sunburst.isNotEmpty())
        menu.addItem (5, "SunBurst synthesizer");
    if (hexforge.isNotEmpty())
        menu.addItem (6, "HexForge FM synthesizer");
    if (! others.empty())
    {
        juce::PopupMenu plugins;
        int id = 100;
        for (const auto& p : foundPlugins)
            if (p.isInstrument && p.identifier != sunburst && p.identifier != hexforge)
                plugins.addItem (id++, p.name + "  (" + p.format + ")");
        menu.addSubMenu ("Plugin instruments", plugins);
    }
    menu.addSeparator();
    menu.addItem (7, "More plugins... (Plugin picker)");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                        [safe = juce::Component::SafePointer<MainComponent> (this), sunburst, hexforge, others] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result >= 100 && result - 100 < static_cast<int> (others.size()))
                            {
                                safe->setPluginInstrument (-1, others[static_cast<size_t> (result - 100)]);
                                return;
                            }
                            switch (result)
                            {
                                case 5: safe->setPluginInstrument (-1, sunburst); return;
                                case 6: safe->setPluginInstrument (-1, hexforge); return;
                                case 4: safe->chooseLoopForSlicer(); return;
                                case 7: safe->showPanel (safe->pluginPanel); safe->saveLayout(); safe->updateToggleButtons(); return;
                                default: break;
                            }
                            const int index = safe->addChannel (result == 1 ? Instrument::Synth : result == 2 ? Instrument::Sampler
                                                                : result == 8 ? Instrument::Layer : Instrument::Drums);
                            if (index >= 0 && result == 3)
                                safe->openDrumMachine (index);
                            if (index >= 0 && result == 8)
                                safe->statusLabel.setText ("Layer channel added: right-click it and tick the channels that play its notes under Layer",
                                                           juce::dontSendNotification);
                        });
}

void MainComponent::showPatternsMenu()
{
    juce::PopupMenu menu;
    const bool havePattern = currentPattern >= 0 && currentPattern < static_cast<int> (arrangement.patterns.size());
    menu.addItem (1, "New empty pattern");
    menu.addItem (menuItem (2, "Clone current pattern", "", havePattern));
    menu.addItem (3, "Loop Starter...");
    menu.addSeparator();
    menu.addItem (menuItem (4, "Rename current pattern...", "", havePattern));
    menu.addItem (menuItem (5, "Delete current pattern", "", havePattern && arrangement.patterns.size() > 1));
    {
        juce::PopupMenu lengths;
        const int currentBars = havePattern ? std::max (1, currentPatternTicks() / std::max (1, interfaceMeter().barTicks())) : 0;
        for (int bars : { 1, 2, 4, 8, 16 })
            lengths.addItem (100 + bars, juce::String (bars) + (bars == 1 ? " bar" : " bars"), havePattern, bars == currentBars);
        menu.addSubMenu ("Pattern length", lengths);
    }
    menu.addSeparator();
    menu.addItem (menuItem (6, "Previous pattern", "", currentPattern > 0));
    menu.addItem (menuItem (7, "Next pattern", "", currentPattern + 1 < static_cast<int> (arrangement.patterns.size())));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternsButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result > 100)
                            {
                                safe->setPatternLength (result - 100);
                                return;
                            }
                            switch (result)
                            {
                                case 1: safe->addPattern (false); break;
                                case 2: safe->addPattern (true); break;
                                case 3: safe->openLoopStarter(); break;
                                case 4: safe->renamePattern (safe->currentPattern); break;
                                case 5: safe->deletePattern (safe->currentPattern); break;
                                case 6: safe->selectPattern (safe->currentPattern - 1); break;
                                case 7: safe->selectPattern (safe->currentPattern + 1); break;
                                default: break;
                            }
                        });
}

void MainComponent::showViewMenu()
{
    juce::PopupMenu menu;
    menu.addItem (menuItem (1, "Playlist", "F5", true, playlistPanel.isOpen()));
    menu.addItem (menuItem (2, "Piano roll", "F7", true, pianoPanel.isOpen()));
    menu.addItem (menuItem (3, "Channel rack", "F6", true, rackPanel.isOpen()));
    menu.addItem (menuItem (4, "Mixer", "F9", true, mixerPanelWindow.isOpen()));
    menu.addItem (menuItem (5, "Browser", "F8", true, browserOpen));
    menu.addItem (menuItem (6, "Plugin picker", "", true, pluginPanel.isOpen()));
    menu.addSeparator();
    menu.addItem (menuItem (7, "Meters", "", true, metersWindow != nullptr));
    menu.addItem (menuItem (9, "Performance mode...", "", true, performanceWindow != nullptr)); // 0.62.0
    menu.addSeparator();
    menu.addItem (8, "Reset the layout");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&viewButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            switch (result)
                            {
                                case 1: safe->togglePanel (safe->playlistPanel); break;
                                case 2: safe->togglePanel (safe->pianoPanel); break;
                                case 3: safe->togglePanel (safe->rackPanel); break;
                                case 4: safe->togglePanel (safe->mixerPanelWindow); break;
                                case 5: safe->setBrowserOpen (! safe->browserOpen); break;
                                case 9: safe->togglePerformanceWindow(); break;
                                case 6: safe->togglePanel (safe->pluginPanel); break;
                                case 7: safe->toggleMeters(); break;
                                case 8: safe->resetLayout(); break;
                                default: break;
                            }
                        });
}

void MainComponent::showOptionsMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Audio settings (device, buffer)...");
    menu.addItem (2, "Metronome: tempo, volume, tap tempo...");
    menu.addItem (3, "Recording options...");
    menu.addItem (4, "Note recording options...");
    menu.addItem (menuItem (5, "Typing keyboard to piano", "", true, typingButton.getToggleState()));
    menu.addSeparator();
    menu.addItem (menuItem (6, "Play a sound at start-up", "", true, settings == nullptr || settings->getBoolValue ("startup.sound", true)));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            switch (result)
                            {
                                case 1: safe->openAudioSettings(); break;
                                case 2: safe->openMetronomePanel(); break;
                                case 3: safe->openRecordMenu(); break;
                                case 4: safe->openNoteRecordMenu(); break;
                                case 5: safe->setTypingOn (! safe->typingButton.getToggleState()); break;
                                case 6:
                                    if (safe->settings != nullptr)
                                        safe->settings->setValue ("startup.sound", ! safe->settings->getBoolValue ("startup.sound", true));
                                    break;
                                default: break;
                            }
                        });
}

void MainComponent::showToolsMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Helios (ask about the program)...");
    menu.addItem (2, "Loop Starter...");
    menu.addItem (3, "Slicer: slice a loop into a channel...");
    menu.addSeparator();
    menu.addItem (4, "Import audio or video into the playlist...");
    menu.addItem (5, "Import a MIDI file into patterns...");
    menu.addItem (6, "Open an FL Studio project (.flp)...");
    menu.addItem (7, "Export as FL Studio project (.flp)...");
    menu.addItem (8, "Export project with samples...");
    menu.addSeparator();
    menu.addItem (9, "Scan for plugins");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&toolsButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            switch (result)
                            {
                                case 1: safe->openHelios(); break;
                                case 2: safe->openLoopStarter(); break;
                                case 3: safe->chooseLoopForSlicer(); break;
                                case 4: safe->chooseMediaToImport(); break;
                                case 5: safe->chooseMidiToImport(); break;
                                case 6: safe->openFlpProject(); break;
                                case 7: safe->exportFlpProject(); break;
                                case 8: safe->exportProjectFolder(); break;
                                case 9: safe->startPluginScan(); break;
                                default: break;
                            }
                        });
}

// The Channel Rack's "≡" (0.60.0), as FL Studio's channel options menu: what happens to the selected channel.
void MainComponent::showRackOptionsMenu (juce::Component& target)
{
    juce::PopupMenu menu;
    const bool haveChannel = selected >= 0 && selected < static_cast<int> (channels.size());
    const auto name = haveChannel ? juce::String (channels[static_cast<size_t> (selected)].name) : juce::String ("channel");
    menu.addSectionHeader (name);
    menu.addItem (menuItem (1, "Rename...", "", haveChannel));
    menu.addItem (menuItem (2, "Clone", "", haveChannel));
    menu.addItem (menuItem (3, "Delete", "", haveChannel && channels.size() > 1));
    menu.addSeparator();
    menu.addItem (menuItem (4, "Move up", "Alt+Up", haveChannel && selected > 0));
    menu.addItem (menuItem (5, "Move down", "Alt+Down", haveChannel && selected + 1 < static_cast<int> (channels.size())));
    menu.addSeparator();
    menu.addItem (menuItem (6, "Assign a free mixer track...", "", haveChannel));
    menu.addItem (menuItem (7, "Open the piano roll", "F7", haveChannel));
    menu.addSeparator();
    menu.addItem (8, "Add a channel...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                        [safe = juce::Component::SafePointer<MainComponent> (this), target = juce::Component::SafePointer<juce::Component> (&target)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            switch (result)
                            {
                                case 1: safe->renameChannel (safe->selected); break;
                                case 2: safe->cloneChannel (safe->selected); break;
                                case 3: safe->deleteChannel (safe->selected); break;
                                case 4: safe->moveChannel (safe->selected, -1); break;
                                case 5: safe->moveChannel (safe->selected, 1); break;
                                case 6: safe->openAssignTrack (safe->selected); break;
                                case 7: safe->openPianoRoll (safe->selected); break;
                                case 8: safe->showAddMenu (target != nullptr ? *target : *safe); break;
                                default: break;
                            }
                        });
}

// ------------------------------------------------------------------------- the Plugin picker

void MainComponent::refreshPluginPicker()
{
    std::vector<PluginPicker::Entry> instruments, effects;
    const auto builtin = [] (const juce::String& name, const juce::String& payload)
    {
        PluginPicker::Entry e;
        e.name = name;
        e.detail = "built-in";
        e.item = { "instrument", payload };
        return e;
    };
    instruments.push_back (builtin ("Synth", "synth"));
    instruments.push_back (builtin ("Sampler", "sampler"));
    instruments.push_back (builtin ("Drum machine", "drums"));
    for (const auto& p : foundPlugins)
    {
        PluginPicker::Entry e;
        e.name = p.name;
        e.detail = (p.manufacturer.isNotEmpty() ? p.manufacturer + "  " : juce::String()) + p.format;
        if (p.isInstrument)
        {
            e.item = { "plugin", "instrument|" + p.identifier };
            instruments.push_back (e);
        }
        else
        {
            e.item = { "plugin", "effect|" + p.identifier };
            e.pluginId = p.identifier;
            effects.push_back (e);
        }
    }
    for (const auto& entry : kEffectMenu)
    {
        PluginPicker::Entry e;
        e.name = effectName (entry.type);
        e.detail = "built-in";
        e.effect = entry.type;
        effects.push_back (e);
    }
    pluginPicker.setInstruments (std::move (instruments));
    pluginPicker.setEffects (std::move (effects));
}

void MainComponent::addBuiltinEffect (EffectType type)
{
    if (selected < 0 || selected >= static_cast<int> (channels.size()) || type == EffectType::None || type == EffectType::Plugin)
        return;
    const int trackIndex = std::clamp (channels[static_cast<size_t> (selected)].mixerTrack, 0, static_cast<int> (tracks.size()) - 1);
    auto& slots = tracks[static_cast<size_t> (trackIndex)].slots;
    const auto freeSlot = std::find_if (slots.begin(), slots.end(), [] (const EffectSlot& s) { return s.type == EffectType::None; });
    if (freeSlot == slots.end())
    {
        statusLabel.setText ("Track " + juce::String (trackIndex + 1) + " has no free effect slot", juce::dontSendNotification);
        return;
    }
    *freeSlot = EffectSlot::make (type);
    mixerPanel.refreshTrack (trackIndex);
    publishProject();
    statusLabel.setText (juce::String (effectName (type)) + " added to track " + juce::String (trackIndex + 1) + ", slot "
                             + juce::String (static_cast<int> (freeSlot - slots.begin()) + 1),
                         juce::dontSendNotification);
}
} // namespace sls
