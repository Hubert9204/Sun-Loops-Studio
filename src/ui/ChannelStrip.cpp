#include "ui/ChannelStrip.h"

#include <algorithm>

namespace sls
{
namespace
{
const juce::Colour kStripBackground { 0xff30343c };
const juce::Colour kStripSelected { 0xff3b4150 };
const juce::Colour kStepOff { 0xff22252b };
const juce::Colour kStepOffAlt { 0xff2a2e36 };
const juce::Colour kStepOn { 0xffe8795a };
const juce::Colour kPreview { 0xff4cc38a };
const juce::Colour kPlayhead { 0xffffb347 };
} // namespace

// ----------------------------------------------------------------------------- StepGrid

void StepGrid::setPlayheadTick (int tick)
{
    if (tick != playheadTick)
    {
        playheadTick = tick;
        repaint();
    }
}

int StepGrid::stepAt (int x) const noexcept
{
    return std::clamp (static_cast<int> (static_cast<float> (x) / stepWidth()), 0, stepCount() - 1);
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const int stepTotal = stepCount();
    const float w = bounds.getWidth() / static_cast<float> (stepTotal);
    const int playingStep = playheadTick >= 0 ? playheadTick / kStepTicks : -1;

    if (showsSteps())
    {
        // Each square is drawn from the notes on every paint (nothing is cached), so what is shown is always what plays. Since
        // 0.42.0 every step of the pattern is a real step: 16 for a bar of 4/4, 4 more for each quarter the rack's + adds, in groups
        // of four (one quarter note each).
        const int pitch = steps::stepPitch (channel);
        for (int s = 0; s < stepTotal; ++s)
        {
            const bool on = steps::stepOn (channel.notes, s, pitch);
            const auto r = juce::Rectangle<float> (bounds.getX() + static_cast<float> (s) * w, bounds.getY(), w, bounds.getHeight()).reduced (std::min (1.5f, w * 0.12f));
            auto colour = on ? kStepOn : ((s / 4) % 2 == 0 ? kStepOff : kStepOffAlt);
            if (s == playingStep)
                colour = colour.brighter (on ? 0.5f : 0.35f);
            g.setColour (colour);
            g.fillRoundedRectangle (r, 2.0f);
        }
    }
    else
    {
        g.setColour (kStepOff);
        g.fillRoundedRectangle (bounds, 3.0f);

        if (! channel.notes.empty())
        {
            int lo = 127, hi = 0;
            for (const auto& n : channel.notes)
            {
                lo = std::min (lo, n.pitch);
                hi = std::max (hi, n.pitch);
            }
            const float span = static_cast<float> (std::max (12, hi - lo + 1));
            const float rowH = std::min (6.0f, (bounds.getHeight() - 4.0f) / span);

            g.setColour (kPreview);
            for (const auto& n : channel.notes)
            {
                const float x = bounds.getX() + static_cast<float> (n.startTick) / static_cast<float> (loopLengthTicks) * bounds.getWidth();
                const float nw = std::max (2.0f, static_cast<float> (n.lengthTicks) / static_cast<float> (loopLengthTicks) * bounds.getWidth() - 1.0f);
                const float y = bounds.getBottom() - 2.0f - (static_cast<float> (n.pitch - lo) + 1.0f) * rowH;
                g.fillRect (x, y, nw, std::max (2.0f, rowH - 1.0f));
            }
        }
    }

    if (playheadTick >= 0)
    {
        const float x = bounds.getX() + static_cast<float> (playheadTick) / static_cast<float> (loopLengthTicks) * bounds.getWidth();
        g.setColour (kPlayhead);
        g.drawVerticalLine (static_cast<int> (x), bounds.getY(), bounds.getBottom());
    }
}

void StepGrid::paintStep (int step, bool on)
{
    if (step == lastStep)
        return;
    lastStep = step;
    if (steps::setStep (channel.notes, channel.steps, step, on, loopLengthTicks, steps::stepPitch (channel)))
    {
        repaint();
        if (onStepsEdited)
            onStepsEdited();
    }
}

bool StepGrid::fillEvery (int every)
{
    every = std::max (1, every);
    bool changed = false;
    const int pitch = steps::stepPitch (channel);
    for (int step = 0; step < stepCount(); ++step)
        changed = steps::setStep (channel.notes, channel.steps, step, step % every == 0, loopLengthTicks, pitch) || changed;
    if (changed)
    {
        repaint();
        if (onStepsEdited)
            onStepsEdited();
    }
    return changed;
}

bool StepGrid::clearSteps()
{
    bool changed = false;
    const int pitch = steps::stepPitch (channel);
    for (int step = 0; step < stepCount(); ++step)
        changed = steps::setStep (channel.notes, channel.steps, step, false, loopLengthTicks, pitch) || changed;
    if (changed)
    {
        repaint();
        if (onStepsEdited)
            onStepsEdited();
    }
    return changed;
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::None;
    lastStep = -1;

    if (! showsSteps())
    {
        // The preview of the piano roll: right click is the channel's menu, left click opens (or brings forward) the editor.
        if (e.mods.isPopupMenu())
        {
            if (onContextMenu)
                onContextMenu();
        }
        else if (onOpenEditor)
            onOpenEditor();
        return;
    }

    // Squares: the left button writes steps, the right button erases them (both also while dragging across).
    drag = e.mods.isPopupMenu() ? Drag::Erase : Drag::Paint;
    paintStep (stepAt (e.x), drag == Drag::Paint);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::Paint || drag == Drag::Erase)
    {
        if (e.x >= 0 && e.x < getWidth())
            paintStep (stepAt (e.x), drag == Drag::Paint);
        return;
    }
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::None;
    lastStep = -1;
    repaint();
}

void StepGrid::mouseMove (const juce::MouseEvent&)
{
    setMouseCursor (showsSteps() ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
}

void StepGrid::mouseExit (const juce::MouseEvent&)
{
}

// ------------------------------------------------------------------------- ChannelStrip

namespace
{
juce::String pitchText (double semitones)
{
    return (semitones > 0.0 ? "+" : "") + juce::String (semitones, 1) + " st";
}

juce::String swingText (double swing)
{
    return juce::String (juce::roundToInt (swing * 100.0)) + " %";
}
} // namespace

ChannelStrip::ChannelStrip (Channel& channelToControl, int channelIndex)
    : channel (channelToControl), index (channelIndex),
      pitchKnob ("PITCH", -12.0, 12.0, 0.1, 0.0, pitchText),
      swingKnob ("SWING", 0.0, 1.0, 0.01, 0.0, swingText),
      grid (channelToControl)
{
    nameLabel.setFont (juce::FontOptions (13.0f));
    nameLabel.setJustificationType (juce::Justification::centred); // 0.60.0: on its button, as in FL Studio
    nameLabel.setMinimumHorizontalScale (0.7f);
    nameLabel.setInterceptsMouseClicks (false, false);

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9534f));
    muteButton.onClick = [this]
    {
        channel.mute = muteButton.getToggleState();
        if (onChanged) onChanged (index);
    };

    soloButton.setClickingTogglesState (true);
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0b030));
    soloButton.onClick = [this]
    {
        channel.solo = soloButton.getToggleState();
        if (onChanged) onChanged (index);
    };

    volumeSlider.setRange (0.0, 1.5, 0.01);
    volumeSlider.setDoubleClickReturnValue (true, 0.8);
    volumeSlider.setTooltip ("Volume (right click: automate)");
    volumeSlider.onAutomate = [this] { if (onAutomate) onAutomate (index, AutomationKind::ChannelVolume); };
    volumeSlider.onValueChange = [this]
    {
        channel.volume = static_cast<float> (volumeSlider.getValue());
        if (onChanged) onChanged (index);
    };

    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.setDoubleClickReturnValue (true, 0.0);
    panSlider.setTooltip ("Pan (right click: automate)");
    panSlider.onAutomate = [this] { if (onAutomate) onAutomate (index, AutomationKind::ChannelPan); };
    panSlider.onValueChange = [this]
    {
        channel.pan = static_cast<float> (panSlider.getValue());
        if (onChanged) onChanged (index);
    };

    trackSelector.onChange = [this] (int track)
    {
        channel.mixerTrack = track;
        if (onChanged) onChanged (index);
    };

    pitchKnob.setTooltipText ("Pitch: tunes every note of this channel, in semitones (double-click: 0)");
    pitchKnob.onValueChange = [this] (double v)
    {
        channel.pitch = static_cast<float> (v);
        if (onChanged) onChanged (index);
    };

    swingKnob.setTooltipText ("Swing: delays the off-beat 16th notes (double-click: 0)");
    swingKnob.onValueChange = [this] (double v)
    {
        channel.swing = static_cast<float> (v);
        if (onChanged) onChanged (index);
    };

    loadButton.onClick = [this] { if (onLoadSample) onLoadSample (index); };

    grid.onStepsEdited = [this] { if (onChanged) onChanged (index); };
    // A click on the preview opens the piano roll for every instrument, drums included (0.53.0; before, drums opened the drum
    // machine, which a double click on the channel and its menu still do).
    grid.onOpenEditor = [this]
    {
        if (onOpenPianoRoll)
            onOpenPianoRoll (index);
    };
    grid.onContextMenu = [this] { showContextMenu(); };

    for (auto* c : std::initializer_list<juce::Component*> { &nameLabel, &trackSelector, &muteButton, &soloButton,
                                                              &volumeSlider, &panSlider, &pitchKnob, &swingKnob,
                                                              &loadButton, &meter, &grid })
        addAndMakeVisible (c);

    refreshFromChannel();
}

void ChannelStrip::showAutomated (AutomationKind kind, float value)
{
    if (kind == AutomationKind::ChannelVolume)
        volumeSlider.setValue (value, juce::dontSendNotification);
    else if (kind == AutomationKind::ChannelPan)
        panSlider.setValue (value, juce::dontSendNotification);
}

void ChannelStrip::refreshFromChannel()
{
    nameLabel.setText (juce::String (channel.name), juce::dontSendNotification);
    muteButton.setToggleState (channel.mute, juce::dontSendNotification);
    soloButton.setToggleState (channel.solo, juce::dontSendNotification);
    volumeSlider.setValue (channel.volume, juce::dontSendNotification);
    panSlider.setValue (channel.pan, juce::dontSendNotification);
    trackSelector.setTrack (channel.mixerTrack);
    pitchKnob.setValue (channel.pitch);
    swingKnob.setValue (channel.swing);
    loadButton.setVisible (channel.instrument == Instrument::Sampler);
    swingKnob.setVisible (channel.instrument != Instrument::Drums); // a drum machine has a swing per pad
    grid.repaint();
}

void ChannelStrip::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void ChannelStrip::paint (juce::Graphics& g)
{
    g.setColour (selected ? kStripSelected : kStripBackground);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);

    if (selected)
    {
        g.setColour (kPreview);
        g.fillRoundedRectangle (1.0f, 4.0f, 3.0f, static_cast<float> (getHeight()) - 8.0f, 1.5f);
    }

    // 0.60.0: the name sits on a button, like FL Studio's channel button (click: select; double click: the channel's window;
    // right click: the menu). The selected channel's button is lit.
    const auto button = nameLabel.getBounds().toFloat().reduced (0.0f, 3.0f);
    // 0.62.0: the channel's own colour, when it has one
    const auto own = juce::Colour (channel.colour);
    g.setColour (channel.colour != 0 ? own.withAlpha (selected ? 0.75f : 0.5f) : (selected ? kPreview.withAlpha (0.28f) : juce::Colour (0xff23272e)));
    g.fillRoundedRectangle (button, 3.0f);
    if (channel.instrument == Instrument::Layer) // a layer: a small mark at the button's right
    {
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        for (int k = 0; k < 3; ++k)
            g.fillRect (button.getRight() - 12.0f, button.getY() + 6.0f + static_cast<float> (k) * 5.0f, 8.0f, 2.0f);
    }
    g.setColour (selected ? kPreview.withAlpha (0.9f) : juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (button, 3.0f, 1.0f);
}

// 0.60.0: the row follows FL Studio's Channel Rack from left to right - mute (and solo), pan, volume, the mixer track, the
// channel's name button, then the steps - with the program's own pitch and swing, the meter and Load between the name and the steps.
void ChannelStrip::resized()
{
    auto area = getLocalBounds().reduced (8, 6);

    muteButton.setBounds (area.removeFromLeft (26).withSizeKeepingCentre (26, 24));
    area.removeFromLeft (3);
    soloButton.setBounds (area.removeFromLeft (26).withSizeKeepingCentre (26, 24));
    area.removeFromLeft (8);
    panSlider.setBounds (area.removeFromLeft (54));
    area.removeFromLeft (2);
    volumeSlider.setBounds (area.removeFromLeft (72));
    area.removeFromLeft (8);
    trackSelector.setBounds (area.removeFromLeft (62).withSizeKeepingCentre (62, 22));
    area.removeFromLeft (6);
    nameLabel.setBounds (area.removeFromLeft (96));
    area.removeFromLeft (6);
    pitchKnob.setBounds (area.removeFromLeft (38));
    area.removeFromLeft (2);
    swingKnob.setBounds (area.removeFromLeft (38));
    area.removeFromLeft (6);
    meter.setBounds (area.removeFromLeft (10));
    area.removeFromLeft (8);
    loadButton.setBounds (area.removeFromLeft (46).withSizeKeepingCentre (46, 24));
    area.removeFromLeft (8);
    grid.setBounds (area);
}

// ------------------------------------------------------------------ drag and drop from the browser

bool ChannelStrip::isInterestedInDragSource (const SourceDetails& details)
{
    drag::Item item;
    return drag::Item::parse (details.description, item);
}

void ChannelStrip::setDropHighlight (bool on)
{
    if (dropHighlight != on)
    {
        dropHighlight = on;
        repaint();
    }
}

void ChannelStrip::itemDropped (const SourceDetails& details)
{
    setDropHighlight (false);

    drag::Item item;
    if (drag::Item::parse (details.description, item) && onItemDropped)
        onItemDropped (index, item);
}

void ChannelStrip::paintOverChildren (juce::Graphics& g)
{
    if (! dropHighlight)
        return;

    g.setColour (kPreview.withAlpha (0.18f));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
    g.setColour (kPreview);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 4.0f, 2.0f);
}

void ChannelStrip::mouseDown (const juce::MouseEvent& e)
{
    if (onSelect)
        onSelect (index);

    if (e.mods.isPopupMenu())
        showContextMenu();
}

void ChannelStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    if (channel.instrument == Instrument::Plugin && onOpenPlugin)
        onOpenPlugin (index);
    else if (channel.instrument == Instrument::Drums && onOpenDrumMachine)
        onOpenDrumMachine (index);
    else if (onOpenPianoRoll)
        onOpenPianoRoll (index);
}

juce::Colour ChannelStrip::paletteColour (int index)
{
    static constexpr std::uint32_t kPalette[16] = { 0xffd9534f, 0xffe8843a, 0xffe8a93a, 0xffd9c94a, 0xff9ccc4a, 0xff4cc38a, 0xff4ac9b0, 0xff3aa6a0,
                                                    0xff3ab6d9, 0xff4a9be8, 0xff4a72e8, 0xff6a5ae8, 0xff9a5ae8, 0xffc94ad0, 0xffe86aa0, 0xff8a909a };
    return juce::Colour (kPalette[static_cast<size_t> (std::clamp (index, 0, 15))]);
}

// Right click on a channel, like FL Studio: open its piano roll, choose what makes the sound, rename.
void ChannelStrip::showContextMenu()
{
    if (onSelect)
        onSelect (index);

    juce::PopupMenu instrument;
    instrument.addItem (2, "Synth", true, channel.instrument == Instrument::Synth);
    instrument.addItem (3, "Sampler (WAV / FLAC / OGG / AIFF)", true, channel.instrument == Instrument::Sampler);
    instrument.addItem (9, "Drum machine (16 pads)", true, channel.instrument == Instrument::Drums);
    instrument.addItem (22, "Layer (its notes are played by other channels)", true, channel.instrument == Instrument::Layer);
    const auto plugins = listInstrumentPlugins ? listInstrumentPlugins() : std::vector<std::pair<juce::String, juce::String>> {};
    if (plugins.empty())
        instrument.addItem (17, "Plugin instruments (scan the browser's Plugin folder first)", false);
    else
    {
        juce::PopupMenu pluginMenu;
        for (size_t p = 0; p < plugins.size(); ++p)
            pluginMenu.addItem (1000 + static_cast<int> (p), plugins[p].first);
        instrument.addSubMenu ("Plugin instruments", pluginMenu, true, nullptr, channel.instrument == Instrument::Plugin);
    }

    juce::PopupMenu menu;
    menu.addItem (18, "Plugin window...", channel.instrument == Instrument::Plugin && channel.plugin != nullptr);
    menu.addItem (20, "Automate a plugin parameter...", channel.instrument == Instrument::Plugin && channel.plugin != nullptr && channel.plugin->parameterCount() > 0);
    menu.addItem (10, "Drum machine...", channel.instrument == Instrument::Drums);
    menu.addItem (1, "Piano roll");
    {
        // 0.61.0: the steps, as FL Studio's step menu
        juce::PopupMenu stepMenu;
        stepMenu.addItem (30, "Fill each 2 steps");
        stepMenu.addItem (31, "Fill each 4 steps");
        stepMenu.addItem (32, "Fill each 8 steps");
        stepMenu.addItem (33, "Fill every step");
        stepMenu.addSeparator();
        stepMenu.addItem (34, "Clear the steps");
        menu.addSubMenu ("Steps", stepMenu);
    }
    if (channel.instrument != Instrument::Plugin)
    {
        // 0.62.0: polyphony and cut groups, as the settings of an FL Studio channel
        juce::PopupMenu poly;
        poly.addItem (400, "Mono (one note at a time, no new attack)", true, channel.mono);
        poly.addSeparator();
        poly.addSectionHeader ("Most notes at once");
        for (int n : { 0, 1, 2, 4, 8, 16 })
            poly.addItem (410 + n, n == 0 ? juce::String ("No limit") : juce::String (n), ! channel.mono, channel.maxPoly == n);
        poly.addSeparator();
        poly.addSectionHeader ("Portamento (mono: the pitch slides to the new note)");
        const int portaMs[] = { 0, 30, 60, 120, 250, 500 };
        for (int k = 0; k < 6; ++k)
            poly.addItem (430 + k, portaMs[k] == 0 ? juce::String ("Off") : juce::String (portaMs[k]) + " ms", channel.mono,
                          juce::roundToInt (channel.portamento * 1000.0f) == portaMs[k]);
        menu.addSubMenu ("Polyphony", poly);
        juce::PopupMenu cut, cutBy;
        for (int g = 0; g <= 8; ++g)
        {
            cut.addItem (440 + g, g == 0 ? juce::String ("None") : "Group " + juce::String (g), true, channel.cutGroup == g);
            cutBy.addItem (460 + g, g == 0 ? juce::String ("None") : "Group " + juce::String (g), true, channel.cutBy == g);
        }
        cut.addSeparator();
        cut.addItem (480, "Cut itself (each note stops the one before)", true, channel.cutGroup > 0 && channel.cutGroup == channel.cutBy);
        menu.addSubMenu ("Cut group (its notes cut...)", cut);
        menu.addSubMenu ("Cut by group", cutBy);
    }
    // 0.62.0: the Layer's children, the channel's colour and its filter group
    const auto names = listChannelNames ? listChannelNames() : juce::StringArray();
    if (channel.instrument == Instrument::Layer)
    {
        juce::PopupMenu children;
        for (int c = 0; c < names.size() && c < kMaxChannels; ++c)
            if (c != index)
                children.addItem (600 + c, juce::String (c + 1) + ": " + names[c], true, ((channel.layerMask >> c) & 1u) != 0);
        children.addSeparator();
        children.addItem (598, "All the channels");
        children.addItem (599, "None");
        menu.addSubMenu ("Layer: the channels that play its notes", children);
    }
    {
        juce::PopupMenu colours;
        colours.addItem (500, "Default", true, channel.colour == 0);
        static const char* const kColourNames[] = { "Red", "Orange", "Amber", "Yellow", "Lime", "Green", "Mint", "Teal",
                                                    "Cyan", "Sky", "Blue", "Indigo", "Violet", "Magenta", "Pink", "Grey" };
        for (int k = 0; k < 16; ++k)
        {
            juce::PopupMenu::Item item (kColourNames[k]);
            item.itemID = 501 + k;
            item.colour = paletteColour (k);
            item.isTicked = channel.colour == paletteColour (k).getARGB();
            colours.addItem (item);
        }
        menu.addSubMenu ("Colour", colours);
        juce::PopupMenu groups;
        const auto existing = listGroups ? listGroups() : juce::StringArray();
        groups.addItem (519, "None (shown under All only)", true, channel.group.empty());
        for (int k = 0; k < existing.size() && k < 60; ++k)
            groups.addItem (520 + k, existing[k], true, juce::String (channel.group) == existing[k]);
        groups.addSeparator();
        groups.addItem (518, "New group...");
        menu.addSubMenu ("Filter group", groups);
    }
    menu.addSeparator();
    menu.addSubMenu ("Instrument", instrument);
    menu.addItem (4, "Load sample...", channel.instrument == Instrument::Sampler);
    menu.addItem (8, "Edit sample...", channel.instrument == Instrument::Sampler && channel.sample != nullptr && channel.sample->numFrames() > 1);
    menu.addItem (15, "Sampler settings...", channel.instrument == Instrument::Sampler);
    menu.addItem (21, "Slicer...", channel.instrument == Instrument::Sampler
                                       && (channel.program == nullptr ? channel.sample != nullptr && channel.sample->numFrames() > 1
                                                                      : ! channel.program->sliceStarts.empty()));
    menu.addItem (19, "Synth settings...", channel.instrument == Instrument::Synth);
    menu.addItem (16, "Load SoundFont / SFZ (.sf2, .sfz)...", channel.instrument != Instrument::Drums && channel.instrument != Instrument::Layer);
    menu.addItem (5, "Rename...");
    menu.addItem (11, "Clone channel");
    menu.addItem (12, "Move up", index > 0);
    menu.addItem (13, "Move down");
    menu.addItem (14, "Delete channel...");
    menu.addSeparator();
    menu.addItem (7, "Assign free mixer track");
    menu.addItem (6, "Save as preset...", channel.instrument != Instrument::Drums && channel.instrument != Instrument::Plugin);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<ChannelStrip> (this), plugins] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            const int i = safe->index;
                            if (result >= 1000 && result < 1000 + static_cast<int> (plugins.size()))
                            {
                                if (safe->onPluginChosen)
                                    safe->onPluginChosen (i, plugins[static_cast<size_t> (result - 1000)].second);
                                return;
                            }
                            // 0.62.0: colour, filter group, the layer's children
                            if (result >= 500 && result <= 516)
                            {
                                safe->channel.colour = result == 500 ? 0u : paletteColour (result - 501).getARGB();
                                safe->repaint();
                                if (safe->onLooksChanged) safe->onLooksChanged (i);
                                return;
                            }
                            if (result == 518)
                            {
                                if (safe->onNewGroup) safe->onNewGroup (i);
                                return;
                            }
                            if (result >= 519 && result < 580)
                            {
                                const auto existing = safe->listGroups ? safe->listGroups() : juce::StringArray();
                                safe->channel.group = result == 519 ? std::string() : existing[result - 520].toStdString();
                                if (safe->onLooksChanged) safe->onLooksChanged (i);
                                return;
                            }
                            if (result >= 598 && result < 600 + kMaxChannels)
                            {
                                const auto all = safe->listChannelNames ? safe->listChannelNames() : juce::StringArray();
                                if (result == 598)
                                    safe->channel.layerMask = (all.size() >= 32 ? 0xffffffffu : ((1u << all.size()) - 1u)) & ~(1u << i);
                                else if (result == 599)
                                    safe->channel.layerMask = 0;
                                else
                                    safe->channel.layerMask ^= 1u << (result - 600);
                                if (safe->onChanged) safe->onChanged (i);
                                return;
                            }
                            // 0.62.0: polyphony, portamento and cut groups
                            if (result >= 410 && result <= 426)
                            {
                                safe->channel.maxPoly = result - 410;
                                if (safe->onChanged) safe->onChanged (i);
                                return;
                            }
                            if (result >= 430 && result < 436)
                            {
                                static constexpr float portaSeconds[] = { 0.0f, 0.03f, 0.06f, 0.12f, 0.25f, 0.5f };
                                safe->channel.portamento = portaSeconds[result - 430];
                                if (safe->onChanged) safe->onChanged (i);
                                return;
                            }
                            if (result >= 440 && result <= 448)
                            {
                                safe->channel.cutGroup = result - 440;
                                if (safe->onChanged) safe->onChanged (i);
                                return;
                            }
                            if (result >= 460 && result <= 468)
                            {
                                safe->channel.cutBy = result - 460;
                                if (safe->onChanged) safe->onChanged (i);
                                return;
                            }
                            switch (result)
                            {
                                case 1: if (safe->onOpenPianoRoll) safe->onOpenPianoRoll (i); break;
                                case 20: if (safe->onAutomate) safe->onAutomate (i, AutomationKind::ChannelPlugin); break;
                                case 2: if (safe->onInstrumentChosen) safe->onInstrumentChosen (i, Instrument::Synth); break;
                                case 3: if (safe->onInstrumentChosen) safe->onInstrumentChosen (i, Instrument::Sampler); break;
                                case 4: if (safe->onLoadSample) safe->onLoadSample (i); break;
                                case 8: if (safe->onEditSample) safe->onEditSample (i); break;
                                case 9: if (safe->onInstrumentChosen) safe->onInstrumentChosen (i, Instrument::Drums); break;
                                case 22: if (safe->onInstrumentChosen) safe->onInstrumentChosen (i, Instrument::Layer); break;
                                case 10: if (safe->onOpenDrumMachine) safe->onOpenDrumMachine (i); break;
                                case 5: if (safe->onRename) safe->onRename (i); break;
                                case 6: if (safe->onSavePreset) safe->onSavePreset (i); break;
                                case 7: if (safe->onAssignTrack) safe->onAssignTrack (i); break;
                                case 11: if (safe->onClone) safe->onClone (i); break;
                                case 12: if (safe->onMove) safe->onMove (i, -1); break;
                                case 13: if (safe->onMove) safe->onMove (i, 1); break;
                                case 14: if (safe->onDelete) safe->onDelete (i); break;
                                case 15: if (safe->onSamplerSettings) safe->onSamplerSettings (i); break;
                                case 21: if (safe->onOpenSlicer) safe->onOpenSlicer (i); break;
                                case 400: safe->channel.mono = ! safe->channel.mono; if (safe->onChanged) safe->onChanged (i); break;
                                case 480:
                                {
                                    // "Cut itself": a group of its own (the channel's number, so two channels do not share one by accident)
                                    const bool on = safe->channel.cutGroup > 0 && safe->channel.cutGroup == safe->channel.cutBy;
                                    safe->channel.cutGroup = safe->channel.cutBy = on ? 0 : 10 + i;
                                    if (safe->onChanged) safe->onChanged (i);
                                    break;
                                }
                                case 30: safe->grid.fillEvery (2); break;
                                case 31: safe->grid.fillEvery (4); break;
                                case 32: safe->grid.fillEvery (8); break;
                                case 33: safe->grid.fillEvery (1); break;
                                case 34: safe->grid.clearSteps(); break;
                                case 16: if (safe->onLoadSoundFont) safe->onLoadSoundFont (i); break;
                                case 18: if (safe->onOpenPlugin) safe->onOpenPlugin (i); break;
                                case 19: if (safe->onSynthSettings) safe->onSynthSettings (i); break;
                                default: break;
                            }
                        });
}
} // namespace sls
