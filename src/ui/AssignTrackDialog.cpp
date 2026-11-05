#include "ui/AssignTrackDialog.h"

#include "audio/MixerRouting.h"

namespace sls
{
namespace
{
const juce::Colour kBackground { 0xff2a2d34 };
const juce::Colour kText { 0xffe6e8ec };
const juce::Colour kMuted { 0xffa9aeb9 };
const juce::Colour kFree { 0xff6cc9a0 };
} // namespace

AssignTrackComponent::AssignTrackComponent (const std::vector<Channel>& channels, int channelIndex,
                                            std::function<void (int)> assignCallback)
    : onAssign (std::move (assignCallback))
{
    const bool valid = channelIndex >= 0 && channelIndex < static_cast<int> (channels.size());
    const auto name = valid ? juce::String (channels[static_cast<size_t> (channelIndex)].name) : juce::String ("instrument");
    const int current = valid ? std::clamp (channels[static_cast<size_t> (channelIndex)].mixerTrack, 0, kMaxTracks - 1) : 0;

    title.setText ("Mixer track for \"" + name + "\"", juce::dontSendNotification);
    title.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, kText);
    hint.setText ("Choose the mixer track this instrument plays through. A free track has no instrument on it yet; "
                  "a track that is already used can be shared.",
                  juce::dontSendNotification);
    hint.setFont (juce::FontOptions (13.0f));
    hint.setColour (juce::Label::textColourId, kMuted);
    hint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (title);
    addAndMakeVisible (hint);

    // Who is on each track (this instrument included: it is marked as the current one).
    std::array<juce::String, kMaxTracks> others;
    for (size_t i = 0; i < channels.size(); ++i)
    {
        auto& text = others[static_cast<size_t> (std::clamp (channels[i].mixerTrack, 0, kMaxTracks - 1))];
        text += (text.isEmpty() ? "" : ", ") + juce::String (channels[i].name);
    }

    firstFree = firstFreeTrack (channels); // a track with no instrument at all

    for (int t = 0; t < kMaxTracks; ++t)
    {
        auto& row = rows[static_cast<size_t> (t)];
        const auto& who = others[static_cast<size_t> (t)];
        juce::String text = "Track " + juce::String (t + 1) + "   -   ";
        text += who.isEmpty() ? juce::String ("free") : who;
        if (t == current)
            text += "   (current)";
        row.setButtonText (text);
        row.setRadioGroupId (1);
        row.setColour (juce::ToggleButton::textColourId, who.isEmpty() ? kFree : kText);
        row.setColour (juce::ToggleButton::tickColourId, kText);
        row.onClick = [this, t] { selectedTrack = t; };
        row.onStateChange = [this, t] { if (rows[static_cast<size_t> (t)].getToggleState()) selectedTrack = t; };
        addAndMakeVisible (row);
    }

    freeButton.setEnabled (firstFree >= 0);
    freeButton.onClick = [this] { if (firstFree >= 0) selectTrack (firstFree); };
    assignButton.onClick = [this] { assign(); };
    cancelButton.onClick = [this] { close(); };
    for (auto* b : { &freeButton, &assignButton, &cancelButton })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    selectTrack (firstFree >= 0 ? firstFree : current); // nothing free: start from where it is
    setWantsKeyboardFocus (true);
    setSize (kWidth, kHeight);
}

void AssignTrackComponent::selectTrack (int track)
{
    selectedTrack = std::clamp (track, 0, kMaxTracks - 1);
    rows[static_cast<size_t> (selectedTrack)].setToggleState (true, juce::dontSendNotification);
}

void AssignTrackComponent::assign()
{
    const int track = selectedTrack;
    close();
    if (onAssign)
        onAssign (track);
}

void AssignTrackComponent::close()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState (0);
}

bool AssignTrackComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey)
    {
        assign();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        selectTrack (selectedTrack + (key == juce::KeyPress::upKey ? -1 : 1));
        return true;
    }
    return false;
}

void AssignTrackComponent::parentHierarchyChanged()
{
    if (isShowing())
        grabKeyboardFocus();
}

void AssignTrackComponent::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
}

void AssignTrackComponent::resized()
{
    auto area = getLocalBounds().reduced (16);
    title.setBounds (area.removeFromTop (26));
    area.removeFromTop (4);
    hint.setBounds (area.removeFromTop (54));
    area.removeFromTop (6);

    for (auto& row : rows)
    {
        row.setBounds (area.removeFromTop (28));
        area.removeFromTop (2);
    }

    area.removeFromTop (8);
    freeButton.setBounds (area.removeFromTop (28).removeFromLeft (190));
    area.removeFromTop (10);
    auto buttons = area.removeFromTop (32);
    cancelButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    assignButton.setBounds (buttons.removeFromRight (100));
}
} // namespace sls
