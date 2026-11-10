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

AssignTrackComponent::AssignTrackComponent (const std::vector<Channel>& channels, int trackCount, int channelIndex,
                                            std::function<void (int)> assignCallback)
    : onAssign (std::move (assignCallback))
{
    trackCount = std::clamp (trackCount, 1, kMaxTracks);
    const bool valid = channelIndex >= 0 && channelIndex < static_cast<int> (channels.size());
    const auto name = valid ? juce::String (channels[static_cast<size_t> (channelIndex)].name) : juce::String ("instrument");
    const int current = valid ? std::clamp (channels[static_cast<size_t> (channelIndex)].mixerTrack, 0, trackCount - 1) : 0;

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
    std::vector<juce::String> others (static_cast<size_t> (trackCount));
    for (size_t i = 0; i < channels.size(); ++i)
    {
        auto& text = others[static_cast<size_t> (std::clamp (channels[i].mixerTrack, 0, trackCount - 1))];
        text += (text.isEmpty() ? "" : ", ") + juce::String (channels[i].name);
    }

    for (int t = 0; t < trackCount; ++t)
    {
        const auto& who = others[static_cast<size_t> (t)];
        juce::String text = "Track " + juce::String (t + 1) + "   -   ";
        text += who.isEmpty() ? juce::String ("free") : who;
        if (t == current)
            text += "   (current)";
        rowText.push_back (text);
        rowFree.push_back (who.isEmpty());
    }
    if (trackCount < kMaxTracks)
    {
        rowText.push_back ("Track " + juce::String (trackCount + 1) + "   -   a new track");
        rowFree.push_back (true);
    }

    firstFree = firstFreeTrack (channels, trackCount); // a track with no instrument at all
    if (firstFree < 0 && trackCount < kMaxTracks)
        firstFree = trackCount; // none: the new one

    list.setModel (this);
    list.setRowHeight (26);
    list.setColour (juce::ListBox::backgroundColourId, kBackground.darker (0.25f));
    list.setWantsKeyboardFocus (false);
    addAndMakeVisible (list);

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
    selectedTrack = std::clamp (track, 0, std::max (0, static_cast<int> (rowText.size()) - 1)); // (also called from the constructor)
    list.selectRow (selectedTrack, false, true);
    list.scrollToEnsureRowIsOnscreen (selectedTrack);
}

void AssignTrackComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= getNumRows())
        return;
    if (selected)
        g.fillAll (juce::Colour (0xff3d5a80));
    g.setColour (rowFree[static_cast<size_t> (row)] ? kFree : kText);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (rowText[static_cast<size_t> (row)], 8, 0, width - 12, height, juce::Justification::centredLeft, true);
}

void AssignTrackComponent::selectedRowsChanged (int lastRowSelected)
{
    if (lastRowSelected >= 0)
        selectedTrack = lastRowSelected;
}

void AssignTrackComponent::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    selectedTrack = row;
    assign();
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

    list.setBounds (area.removeFromTop (area.getHeight() - 8 - 28 - 10 - 32));
    list.scrollToEnsureRowIsOnscreen (selectedTrack); // (selected before the list had a size)

    area.removeFromTop (8);
    freeButton.setBounds (area.removeFromTop (28).removeFromLeft (190));
    area.removeFromTop (10);
    auto buttons = area.removeFromTop (32);
    cancelButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    assignButton.setBounds (buttons.removeFromRight (100));
}
} // namespace sls
