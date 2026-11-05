#include "ui/MixerPanel.h"

#include <cmath>

namespace sls
{
namespace
{
const juce::Colour kStripBg (0xff343841);
const juce::Colour kStripSelectedBg (0xff434a58);
const juce::Colour kAccent (0xffe8943a);
const juce::Colour kPanelBg (0xff262a31);

juce::String shortEffectName (EffectType t)
{
    switch (t)
    {
        case EffectType::Equalizer:  return "EQ";
        case EffectType::Reverb:     return "Reverb";
        case EffectType::Delay:      return "Delay";
        case EffectType::Compressor: return "Comp";
        case EffectType::None:       break;
    }
    return juce::String::fromUTF8 ("\xe2\x80\x94");
}

void styleSmallLabel (juce::Label& l, float size, bool bold, juce::Justification j)
{
    l.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    l.setJustificationType (j);
    l.setInterceptsMouseClicks (false, false);
}
} // namespace

// ------------------------------------------------------------------------- SlotButton

void SlotButton::set (const EffectSlot& slot, bool isSelected, bool showFullName)
{
    if (! (slot == slotCopy) || isSelected != selected || showFullName != fullName)
    {
        slotCopy = slot;
        selected = isSelected;
        fullName = showFullName;
        repaint();
    }
}

void SlotButton::paint (juce::Graphics& g)
{
    const bool empty = slotCopy.type == EffectType::None;
    const bool active = ! empty && ! slotCopy.bypass;
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (empty ? juce::Colour (0xff262a31) : (active ? juce::Colour (0xff2f5e4c) : juce::Colour (0xff3b3d43)));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (selected ? kAccent : juce::Colour (0xff4a505c));
    g.drawRoundedRectangle (r, 3.0f, selected ? 1.6f : 1.0f);

    juce::String text = fullName ? (empty ? juce::String ("Empty - click to add an effect") : juce::String (effectName (slotCopy.type)))
                                 : shortEffectName (slotCopy.type);
    if (slotCopy.bypass && ! empty)
        text += fullName ? "  (bypassed)" : "";

    g.setColour (empty ? juce::Colour (0xff7d8491) : (active ? juce::Colours::white : juce::Colour (0xff9aa0ab)));
    g.setFont (juce::FontOptions (fullName ? 12.0f : 11.0f));
    g.drawText (text, getLocalBounds().reduced (6, 0), fullName ? juce::Justification::centredLeft : juce::Justification::centred, true);
}

void SlotButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu();
    }
    else if (onClick)
        onClick();
}

// ------------------------------------------------------------------------- MixerStrip

juce::String MixerStrip::volumeToText (float volume)
{
    if (volume <= 0.0001f)
        return "-inf dB";
    return juce::String (20.0f * std::log10 (volume), 1) + " dB";
}

juce::String MixerStrip::panToText (float pan)
{
    const int percent = juce::roundToInt (std::abs (pan) * 100.0f);
    if (percent == 0)
        return "C";
    return (pan < 0.0f ? "L " : "R ") + juce::String (percent);
}

MixerStrip::MixerStrip (MixerTrack& trackToControl, int trackIndex)
    : track (trackToControl), index (trackIndex)
{
    styleSmallLabel (nameLabel, 12.0f, true, juce::Justification::centred);
    styleSmallLabel (routedLabel, 10.0f, false, juce::Justification::centred);
    routedLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0ab));
    styleSmallLabel (panLabel, 10.0f, false, juce::Justification::centred);
    styleSmallLabel (dbLabel, 11.0f, false, juce::Justification::centred);

    for (size_t s = 0; s < slotButtons.size(); ++s)
    {
        slotButtons[s].onClick = [this, s] { if (onSlotClicked) onSlotClicked (index, static_cast<int> (s)); };
        slotButtons[s].onMenu = [this, s] { if (onSlotMenu) onSlotMenu (index, static_cast<int> (s)); };
        addAndMakeVisible (slotButtons[s]);
    }

    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.setDoubleClickReturnValue (true, 0.0);
    panSlider.setTooltip ("Stereo balance (double-click: centre)");
    panSlider.onValueChange = [this]
    {
        track.pan = static_cast<float> (panSlider.getValue());
        panLabel.setText (panToText (track.pan), juce::dontSendNotification);
        if (onChanged) onChanged (index);
    };

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9534f));
    muteButton.setMouseClickGrabsKeyboardFocus (false);
    muteButton.onClick = [this]
    {
        track.mute = muteButton.getToggleState();
        if (onChanged) onChanged (index);
    };

    soloButton.setClickingTogglesState (true);
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0b030));
    soloButton.setMouseClickGrabsKeyboardFocus (false);
    soloButton.onClick = [this]
    {
        track.solo = soloButton.getToggleState();
        if (onChanged) onChanged (index);
    };

    fader.setRange (0.0, 1.5, 0.01);
    fader.setDoubleClickReturnValue (true, 1.0);
    fader.setTooltip ("Track volume (double-click: 0 dB)");
    fader.onValueChange = [this]
    {
        track.volume = static_cast<float> (fader.getValue());
        dbLabel.setText (volumeToText (track.volume), juce::dontSendNotification);
        if (onChanged) onChanged (index);
    };

    for (auto* c : std::initializer_list<juce::Component*> { &nameLabel, &routedLabel, &panSlider, &panLabel,
                                                              &muteButton, &soloButton, &fader, &dbLabel, &meter })
        addAndMakeVisible (c);

    refreshFromTrack();
}

void MixerStrip::refreshFromTrack()
{
    nameLabel.setText ("Track " + juce::String (index + 1), juce::dontSendNotification);
    panSlider.setValue (track.pan, juce::dontSendNotification);
    panLabel.setText (panToText (track.pan), juce::dontSendNotification);
    muteButton.setToggleState (track.mute, juce::dontSendNotification);
    soloButton.setToggleState (track.solo, juce::dontSendNotification);
    fader.setValue (track.volume, juce::dontSendNotification);
    dbLabel.setText (volumeToText (track.volume), juce::dontSendNotification);
    refreshSlots();
}

void MixerStrip::refreshSlots()
{
    for (size_t s = 0; s < slotButtons.size(); ++s)
        slotButtons[s].set (track.slots[s], selected && selectedSlot == static_cast<int> (s), false);
}

void MixerStrip::setRoutedChannels (const juce::String& text)
{
    routedLabel.setText (text.isEmpty() ? juce::String ("no channel") : text, juce::dontSendNotification);
    routedLabel.setTooltip (text);
}

void MixerStrip::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        refreshSlots();
        repaint();
    }
}

void MixerStrip::paint (juce::Graphics& g)
{
    g.setColour (selected ? kStripSelectedBg : kStripBg);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);

    if (selected)
    {
        g.setColour (kAccent);
        g.fillRoundedRectangle (4.0f, 1.0f, static_cast<float> (getWidth()) - 8.0f, 3.0f, 1.5f);
    }
}

void MixerStrip::resized()
{
    auto area = getLocalBounds().reduced (5, 6);

    nameLabel.setBounds (area.removeFromTop (18));
    routedLabel.setBounds (area.removeFromTop (14));
    area.removeFromTop (4);

    for (auto& b : slotButtons)
    {
        b.setBounds (area.removeFromTop (18));
        area.removeFromTop (2);
    }
    area.removeFromTop (4);

    panSlider.setBounds (area.removeFromTop (16));
    panLabel.setBounds (area.removeFromTop (12));
    area.removeFromTop (4);

    auto buttons = area.removeFromTop (22);
    muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
    soloButton.setBounds (buttons.reduced (2, 0));
    area.removeFromTop (4);

    dbLabel.setBounds (area.removeFromBottom (16));
    area.removeFromBottom (2);

    auto faderArea = area.withSizeKeepingCentre (std::min (area.getWidth(), 56), area.getHeight());
    meter.setBounds (faderArea.removeFromRight (18));
    faderArea.removeFromRight (4);
    fader.setBounds (faderArea);
}

void MixerStrip::mouseDown (const juce::MouseEvent&)
{
    if (onSelect)
        onSelect (index);
}

// ------------------------------------------------------------------------- MasterStrip

MasterStrip::MasterStrip()
{
    styleSmallLabel (nameLabel, 12.0f, true, juce::Justification::centred);
    nameLabel.setText ("Master", juce::dontSendNotification);
    styleSmallLabel (dbLabel, 11.0f, false, juce::Justification::centred);

    fader.setRange (0.0, 1.0, 0.01);
    fader.setDoubleClickReturnValue (true, 0.8);
    fader.setTooltip ("Master volume (same control as in the toolbar)");
    fader.onValueChange = [this]
    {
        dbLabel.setText (MixerStrip::volumeToText (static_cast<float> (fader.getValue())), juce::dontSendNotification);
        if (onVolumeChanged)
            onVolumeChanged (fader.getValue());
    };

    for (auto* c : std::initializer_list<juce::Component*> { &nameLabel, &fader, &dbLabel, &meter })
        addAndMakeVisible (c);

    setVolume (0.8);
}

void MasterStrip::setVolume (double volume)
{
    fader.setValue (volume, juce::dontSendNotification);
    dbLabel.setText (MixerStrip::volumeToText (static_cast<float> (volume)), juce::dontSendNotification);
}

void MasterStrip::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff3a3f4a));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
    g.setColour (kAccent);
    g.fillRoundedRectangle (4.0f, 1.0f, static_cast<float> (getWidth()) - 8.0f, 3.0f, 1.5f);
}

void MasterStrip::resized()
{
    auto area = getLocalBounds().reduced (5, 6);
    nameLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (4);
    dbLabel.setBounds (area.removeFromBottom (16));
    area.removeFromBottom (2);

    auto faderArea = area.withSizeKeepingCentre (std::min (area.getWidth(), 56), area.getHeight());
    meter.setBounds (faderArea.removeFromRight (18));
    faderArea.removeFromRight (4);
    fader.setBounds (faderArea);
}

// ------------------------------------------------------------------------- FxPanel

FxPanel::FxPanel()
{
    styleSmallLabel (titleLabel, 13.0f, true, juce::Justification::centredLeft);
    styleSmallLabel (effectLabel, 12.0f, true, juce::Justification::centredLeft);
    effectLabel.setColour (juce::Label::textColourId, kAccent);
    styleSmallLabel (notePlaceholder, 11.0f, false, juce::Justification::topLeft);
    notePlaceholder.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0ab));
    notePlaceholder.setText ("Click a slot to add an effect to this track's chain "
                             "(Equalizer, Reverb, Delay, Compressor). Effects run in order, before the fader.\n\n"
                             "VST3 plugin hosting is not implemented yet: the slots hold Sun Loops Studio's built-in effects.",
                             juce::dontSendNotification);

    addAndMakeVisible (titleLabel);
    addAndMakeVisible (effectLabel);
    addAndMakeVisible (notePlaceholder);

    for (size_t s = 0; s < slotButtons.size(); ++s)
    {
        slotButtons[s].onClick = [this, s]
        {
            setSelectedSlot (static_cast<int> (s));
            if (onSelectedSlotChanged) onSelectedSlotChanged (static_cast<int> (s));
            if (track != nullptr && track->slots[s].type == EffectType::None)
                showSlotMenu (static_cast<int> (s));
        };
        slotButtons[s].onMenu = [this, s]
        {
            setSelectedSlot (static_cast<int> (s));
            if (onSelectedSlotChanged) onSelectedSlotChanged (static_cast<int> (s));
            showSlotMenu (static_cast<int> (s));
        };
        addAndMakeVisible (slotButtons[s]);

        powerButtons[s].setButtonText ("ON");
        powerButtons[s].setClickingTogglesState (true);
        powerButtons[s].setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f9e6e));
        powerButtons[s].setMouseClickGrabsKeyboardFocus (false);
        powerButtons[s].setTooltip ("Switch this effect on or off (bypass)");
        powerButtons[s].onClick = [this, s]
        {
            if (track == nullptr)
                return;
            track->slots[s].bypass = ! powerButtons[s].getToggleState();
            refresh();
            if (onChanged) onChanged (trackIndex);
            if (onSlotsChanged) onSlotsChanged (trackIndex);
        };
        addAndMakeVisible (powerButtons[s]);
    }

    for (auto& row : params)
    {
        row.name.setFont (juce::FontOptions (12.0f));
        row.name.setInterceptsMouseClicks (false, false);
        row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        addChildComponent (row.name);
        addChildComponent (row.slider);
    }

    refresh();
}

void FxPanel::setTrack (MixerTrack* newTrack, int newIndex)
{
    track = newTrack;
    trackIndex = newIndex;
    refresh();
}

void FxPanel::setSelectedSlot (int slot)
{
    selectedSlot = std::clamp (slot, 0, kSlotsPerTrack - 1);
    refresh();
}

void FxPanel::refresh()
{
    titleLabel.setText ("Effects - Track " + juce::String (trackIndex + 1), juce::dontSendNotification);

    for (size_t s = 0; s < slotButtons.size(); ++s)
    {
        const EffectSlot slot = track != nullptr ? track->slots[s] : EffectSlot {};
        slotButtons[s].set (slot, static_cast<int> (s) == selectedSlot, true);
        powerButtons[s].setEnabled (slot.type != EffectType::None);
        powerButtons[s].setToggleState (slot.type != EffectType::None && ! slot.bypass, juce::dontSendNotification);
    }
    rebuildParameters();
}

void FxPanel::rebuildParameters()
{
    const EffectSlot slot = track != nullptr ? track->slots[static_cast<size_t> (selectedSlot)] : EffectSlot {};
    const int count = effectParamCount (slot.type);

    effectLabel.setText (slot.type == EffectType::None ? juce::String() : "Slot " + juce::String (selectedSlot + 1) + ": " + effectName (slot.type),
                         juce::dontSendNotification);
    notePlaceholder.setVisible (slot.type == EffectType::None);

    for (int i = 0; i < kMaxEffectParams; ++i)
    {
        auto& row = params[static_cast<size_t> (i)];
        row.slider.onValueChange = nullptr; // not while reconfiguring
        const bool used = i < count;
        row.name.setVisible (used);
        row.slider.setVisible (used);
        if (! used)
            continue;

        const auto info = effectParamInfo (slot.type, i);
        const juce::String unit (info.unit);
        const double range = static_cast<double> (info.max) - static_cast<double> (info.min);
        const int decimals = range >= 100.0 ? 0 : (range >= 10.0 ? 1 : 2);

        row.name.setText (info.name, juce::dontSendNotification);
        row.slider.setRange (info.min, info.max, 0.0);
        row.slider.setSkewFactor (1.0);
        if (unit == "Hz" || unit == "ms" || unit == ":1")
            row.slider.setSkewFactorFromMidPoint (std::sqrt (static_cast<double> (info.min) * static_cast<double> (info.max)));
        row.slider.setDoubleClickReturnValue (true, info.def);
        row.slider.textFromValueFunction = [decimals, unit] (double v)
        {
            return juce::String (v, decimals) + (unit.isEmpty() ? juce::String() : " " + unit);
        };
        row.slider.setValue (slot.params[static_cast<size_t> (i)], juce::dontSendNotification);
        row.slider.updateText();
        row.slider.onValueChange = [this, i]
        {
            if (track == nullptr)
                return;
            track->slots[static_cast<size_t> (selectedSlot)].params[static_cast<size_t> (i)]
                = static_cast<float> (params[static_cast<size_t> (i)].slider.getValue());
            if (onChanged)
                onChanged (trackIndex);
        };
    }
    resized();
}

void FxPanel::applySlot (int slot, const EffectSlot& newSlot)
{
    if (track == nullptr)
        return;

    track->slots[static_cast<size_t> (slot)] = newSlot;
    refresh();
    if (onChanged) onChanged (trackIndex);
    if (onSlotsChanged) onSlotsChanged (trackIndex);
}

void FxPanel::showSlotMenu (int slot)
{
    if (track == nullptr)
        return;

    const auto current = track->slots[static_cast<size_t> (slot)];

    juce::PopupMenu menu;
    menu.addSectionHeader ("Slot " + juce::String (slot + 1));
    for (int t = 1; t < kEffectTypeCount; ++t)
        menu.addItem (t, effectName (static_cast<EffectType> (t)), true, current.type == static_cast<EffectType> (t));
    menu.addSeparator();
    menu.addItem (100, "VST3 plugin... (hosting not implemented yet)", false);
    menu.addSeparator();
    menu.addItem (101, "Bypass", current.type != EffectType::None, current.bypass);
    menu.addItem (102, "Remove effect", current.type != EffectType::None);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slotButtons[static_cast<size_t> (slot)]),
                        [safe = juce::Component::SafePointer<FxPanel> (this), slot, current] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            if (result >= 1 && result < kEffectTypeCount)
                            {
                                const auto type = static_cast<EffectType> (result);
                                if (type != current.type)
                                    safe->applySlot (slot, EffectSlot::make (type));
                            }
                            else if (result == 101)
                            {
                                auto s = current;
                                s.bypass = ! s.bypass;
                                safe->applySlot (slot, s);
                            }
                            else if (result == 102)
                            {
                                safe->applySlot (slot, EffectSlot {});
                            }
                        });
}

void FxPanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff2f333b));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
}

void FxPanel::resized()
{
    auto area = getLocalBounds().reduced (8, 8);
    titleLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (4);

    for (size_t s = 0; s < slotButtons.size(); ++s)
    {
        auto row = area.removeFromTop (26);
        powerButtons[s].setBounds (row.removeFromLeft (36));
        row.removeFromLeft (4);
        slotButtons[s].setBounds (row);
        area.removeFromTop (4);
    }

    area.removeFromTop (8);
    effectLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (4);
    notePlaceholder.setBounds (area);

    for (auto& row : params)
    {
        if (! row.slider.isVisible())
            continue;
        auto r = area.removeFromTop (26);
        row.name.setBounds (r.removeFromLeft (60));
        row.slider.setBounds (r);
        area.removeFromTop (2);
    }
}

// ------------------------------------------------------------------------- MixerPanel

MixerPanel::MixerPanel()
{
    viewport.setViewedComponent (&inner, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    addAndMakeVisible (masterStrip);
    addAndMakeVisible (fxPanel);

    masterStrip.onVolumeChanged = [this] (double v) { if (onMasterVolumeChanged) onMasterVolumeChanged (v); };
    fxPanel.onChanged = [this] (int track) { if (onTrackChanged) onTrackChanged (track); };
    fxPanel.onSlotsChanged = [this] (int track) { refreshTrack (track); };
    fxPanel.onSelectedSlotChanged = [this] (int slot)
    {
        if (selectedTrack >= 0 && selectedTrack < static_cast<int> (strips.size()))
            strips[static_cast<size_t> (selectedTrack)]->setSelectedSlot (slot);
    };
}

void MixerPanel::setTracks (std::vector<MixerTrack>& tracks)
{
    strips.clear();
    trackData = &tracks;

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        auto strip = std::make_unique<MixerStrip> (tracks[i], static_cast<int> (i));
        strip->onSelect = [this] (int idx) { setSelectedTrack (idx); };
        strip->onChanged = [this] (int idx) { if (onTrackChanged) onTrackChanged (idx); };
        strip->onSlotClicked = [this] (int idx, int slot)
        {
            setSelectedTrack (idx);
            fxPanel.setSelectedSlot (slot);
            strips[static_cast<size_t> (idx)]->setSelectedSlot (slot);
            if ((*trackData)[static_cast<size_t> (idx)].slots[static_cast<size_t> (slot)].type == EffectType::None)
                fxPanel.showSlotMenu (slot);
        };
        strip->onSlotMenu = [this] (int idx, int slot)
        {
            setSelectedTrack (idx);
            fxPanel.setSelectedSlot (slot);
            strips[static_cast<size_t> (idx)]->setSelectedSlot (slot);
            fxPanel.showSlotMenu (slot);
        };
        inner.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }

    selectedTrack = std::clamp (selectedTrack, 0, std::max (0, static_cast<int> (tracks.size()) - 1));
    setSelectedTrack (selectedTrack);
    layoutInner();
}

void MixerPanel::refreshTrack (int index)
{
    if (index >= 0 && index < static_cast<int> (strips.size()))
        strips[static_cast<size_t> (index)]->refreshFromTrack();
    if (index == selectedTrack)
        fxPanel.refresh();
}

void MixerPanel::setSelectedTrack (int index)
{
    if (trackData == nullptr || trackData->empty())
        return;

    selectedTrack = std::clamp (index, 0, static_cast<int> (trackData->size()) - 1);
    for (auto& s : strips)
        s->setSelected (s->getIndex() == selectedTrack);

    fxPanel.setTrack (&(*trackData)[static_cast<size_t> (selectedTrack)], selectedTrack);
    if (selectedTrack < static_cast<int> (strips.size()))
        strips[static_cast<size_t> (selectedTrack)]->setSelectedSlot (fxPanel.getSelectedSlot());
}

void MixerPanel::setTrackChannels (int index, const juce::String& channelNames)
{
    if (index >= 0 && index < static_cast<int> (strips.size()))
        strips[static_cast<size_t> (index)]->setRoutedChannels (channelNames);
}

void MixerPanel::setTrackLevels (int index, float peak, float rms)
{
    if (index >= 0 && index < static_cast<int> (strips.size()))
        strips[static_cast<size_t> (index)]->setLevels (peak, rms);
}

void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (kPanelBg);
}

void MixerPanel::resized()
{
    auto area = getLocalBounds().reduced (4);
    fxPanel.setBounds (area.removeFromRight (kFxPanelWidth));
    area.removeFromRight (4);
    masterStrip.setBounds (area.removeFromRight (MixerStrip::kWidth));
    area.removeFromRight (4);
    viewport.setBounds (area);
    layoutInner();
}

void MixerPanel::layoutInner()
{
    const int needed = static_cast<int> (strips.size()) * MixerStrip::kWidth;
    const int height = std::max (1, viewport.getMaximumVisibleHeight());
    inner.setSize (std::max (needed, viewport.getMaximumVisibleWidth()), height);

    int x = 0;
    for (auto& s : strips)
    {
        s->setBounds (x, 0, MixerStrip::kWidth, height);
        x += MixerStrip::kWidth;
    }
}
} // namespace sls
