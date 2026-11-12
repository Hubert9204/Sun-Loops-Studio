#include "ui/PianoRollToolbar.h"

#include "audio/TimeSignature.h"

#include <random>

namespace sls
{
namespace
{
struct SnapChoice
{
    const char* name;
    int ticks;
};
// Note values (0.32.0: 1/2 and 1/64 added): 1/4 is a beat.
constexpr int kMainSnap = -2; // "Main" (0.60.0): the toolbar's global snap, as in FL Studio
constexpr SnapChoice kSnaps[] = { { "Main", kMainSnap },
                                  { "Bar", 0 }, // 0 = a bar of the time signature (0.46.0)
                                           { "1/2", 2 * kTicksPerBeat },       { "1/4 (beat)", kTicksPerBeat },
                                  { "1/8", kTicksPerBeat / 2 },         { "1/16", kTicksPerBeat / 4 },      { "1/32", kTicksPerBeat / 8 },
                                  { "1/64", kTicksPerBeat / 16 },       { "1/8 triplet", kTicksPerBeat / 3 }, { "1/16 triplet", kTicksPerBeat / 6 },
                                  { "None", 1 } };
constexpr int kDefaultSnap = 5; // 1/16

std::uint32_t freshSeed()
{
    static std::mt19937 engine { std::random_device {}() };
    return static_cast<std::uint32_t> (engine()) | 1u;
}
} // namespace

PianoRollToolbar::PianoRollToolbar (PianoRoll& rollToEdit) : roll (rollToEdit)
{
    for (auto* l : { &snapLabel, &scaleLabel, &chordLabel })
    {
        l->setFont (juce::FontOptions (12.0f));
        l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (*l);
    }

    for (size_t i = 0; i < std::size (kSnaps); ++i)
        snapBox.addItem (kSnaps[i].name, static_cast<int> (i) + 1);
    snapBox.setSelectedId (kDefaultSnap + 1, juce::dontSendNotification);
    snapBox.setTooltip ("Where notes snap when they are drawn, moved and resized (hold Alt while dragging for no snap). "
                        "\"Main\" follows the toolbar's global snap.");
    snapBox.onChange = [this] { applySnap(); };

    // 0.60.0: the target channel and the ghost notes, as in FL Studio.
    channelBox.setTooltip ("The channel this piano roll edits (FL Studio's target channel)");
    channelBox.setWantsKeyboardFocus (false);
    channelBox.onChange = [this]
    {
        if (channelBox.getSelectedId() > 0 && onChannelChosen)
            onChannelChosen (channelBox.getSelectedId() - 1);
    };
    // 0.62.0: the tools, as FL Studio's
    toolBox.addItem ("Draw", 1);
    toolBox.addItem ("Paint", 2);
    toolBox.addItem ("Mute", 3);
    toolBox.addItem ("Slice", 4);
    toolBox.setSelectedId (1, juce::dontSendNotification);
    toolBox.setTooltip ("Tool. Draw: click draws a note, drag moves it. Paint: drag over empty grid to draw a row of notes. "
                        "Mute: click a note to mute or unmute it. Slice: click a note to cut it in two at the grid.");
    toolBox.setWantsKeyboardFocus (false);
    toolBox.onChange = [this] { roll.setTool (static_cast<PianoRoll::Tool> (std::max (0, toolBox.getSelectedId() - 1))); };
    ghostToggle.setTooltip ("Ghost notes: show the other channels' notes of this pattern, faintly, behind this channel's");
    ghostToggle.onClick = [this] { refreshGhostNotes(); };

    for (int pc = 0; pc < 12; ++pc)
        rootBox.addItem (theory::pitchClassName (pc), pc + 1);
    rootBox.setSelectedId (1, juce::dontSendNotification);
    rootBox.setTooltip ("The key: the root of the scale");
    const auto& scales = theory::scales();
    for (size_t i = 0; i < scales.size(); ++i)
        scaleBox.addItem (i == 0 ? "None (chromatic)" : scales[i].name, static_cast<int> (i) + 1);
    scaleBox.setSelectedId (1, juce::dontSendNotification);
    scaleBox.setTooltip ("A scale darkens the rows of the notes outside it; it is also the key of the generators");
    rootBox.onChange = [this] { applyScale(); };
    scaleBox.onChange = [this] { applyScale(); };
    scaleSnapToggle.setTooltip ("New and moved notes go to the nearest note of the scale");
    scaleSnapToggle.onClick = [this] { applyScale(); };

    chordBox.addItem ("Single note", 1);
    const auto& chords = theory::chords();
    for (size_t i = 0; i < chords.size(); ++i)
        chordBox.addItem (chords[i].name, static_cast<int> (i) + 2);
    chordBox.setSelectedId (1, juce::dontSendNotification);
    chordBox.setTooltip ("A click on the grid places this chord (its root on the clicked key) instead of one note");
    inversionBox.addItem ("Root", 1);
    inversionBox.addItem ("1st inv.", 2);
    inversionBox.addItem ("2nd inv.", 3);
    inversionBox.addItem ("3rd inv.", 4);
    inversionBox.setSelectedId (1, juce::dontSendNotification);
    inversionBox.setTooltip ("The inversion of the chord: its lowest notes moved up an octave");
    const auto applyChord = [this] { roll.setChordStamp (chordBox.getSelectedId() - 2, inversionBox.getSelectedId() - 1); };
    chordBox.onChange = applyChord;
    inversionBox.onChange = applyChord;

    velocityToggle.setToggleState (roll.isVelocityLaneVisible(), juce::dontSendNotification);
    velocityToggle.setTooltip ("Show the velocity lane under the notes (drag across it to draw velocities)");
    velocityToggle.onClick = [this] { roll.setVelocityLaneVisible (velocityToggle.getToggleState()); };

    toolsButton.setTooltip ("Edit the selected notes (or all of them when none is selected): quantize, humanize, legato, strum, arpeggio...");
    toolsButton.onClick = [this] { showToolsMenu(); };
    generateButton.setTooltip ("Write a chord progression or a bassline into this channel's pattern (Ctrl+Z undoes it)");
    generateButton.onClick = [this] { showGenerateMenu(); };

    for (juce::Component* c : std::initializer_list<juce::Component*> { &channelBox, &toolBox, &snapBox, &rootBox, &scaleBox, &scaleSnapToggle, &chordBox, &inversionBox,
                                                                       &velocityToggle, &ghostToggle, &toolsButton, &generateButton })
        addAndMakeVisible (c);
}

void PianoRollToolbar::setChannels (const std::vector<juce::String>& names, int selectedIndex)
{
    channelBox.clear (juce::dontSendNotification);
    for (size_t i = 0; i < names.size(); ++i)
        channelBox.addItem (names[i].isEmpty() ? "Channel " + juce::String (i + 1) : names[i], static_cast<int> (i) + 1);
    channelBox.setSelectedId (selectedIndex + 1, juce::dontSendNotification);
}

void PianoRollToolbar::refreshGhostNotes()
{
    if (! ghostToggle.getToggleState() || ! getOtherChannels)
    {
        roll.setGhostNotes ({});
        return;
    }
    std::vector<Note> ghosts;
    for (const auto& other : getOtherChannels())
        ghosts.insert (ghosts.end(), other.notes.begin(), other.notes.end());
    roll.setGhostNotes (std::move (ghosts));
}

void PianoRollToolbar::applySnap()
{
    const int ticks = kSnaps[static_cast<size_t> (std::max (0, snapBox.getSelectedId() - 1))].ticks;
    roll.setGridTicks (ticks == kMainSnap ? mainSnapTicks : ticks);
}

void PianoRollToolbar::setMainSnapTicks (int ticks)
{
    mainSnapTicks = ticks;
    applySnap();
}

theory::Scale PianoRollToolbar::currentScale() const
{
    return { std::max (0, rootBox.getSelectedId() - 1), std::max (0, scaleBox.getSelectedId() - 1) };
}

void PianoRollToolbar::applyScale()
{
    roll.setScale (currentScale(), scaleSnapToggle.getToggleState());
}

void PianoRollToolbar::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1d22));
}

void PianoRollToolbar::resized()
{
    auto r = getLocalBounds().reduced (4, 4);
    const auto place = [&r] (juce::Component& c, int width)
    {
        c.setVisible (r.getWidth() >= width);
        if (c.isVisible())
        {
            c.setBounds (r.removeFromLeft (width));
            r.removeFromLeft (4);
        }
    };
    // the buttons first (on the right), then as many of the settings as fit
    generateButton.setBounds (r.removeFromRight (80));
    r.removeFromRight (4);
    toolsButton.setBounds (r.removeFromRight (60));
    r.removeFromRight (8);
    place (channelBox, 120); // the target channel first, as FL Studio's title bar (0.60.0)
    place (toolBox, 72);     // 0.62.0
    place (snapLabel, 36);
    place (snapBox, 96);
    place (ghostToggle, 64); // before the scale and chord settings, so it stays in view in a narrow panel (0.60.0)
    place (scaleLabel, 40);
    place (rootBox, 52);
    place (scaleBox, 136);
    place (scaleSnapToggle, 108);
    place (chordLabel, 42);
    place (chordBox, 112);
    place (inversionBox, 78);
    place (velocityToggle, 76);
}

void PianoRollToolbar::showToolsMenu()
{
    const int count = roll.selectedCount();
    const auto scale = currentScale();
    juce::PopupMenu menu;
    menu.addSectionHeader (count > 0 ? juce::String (count) + " selected note(s)" : "All the notes (none selected)");
    menu.addItem (1, "Select all");
    menu.addSeparator();

    juce::PopupMenu quantize;
    quantize.addItem (10, "Starts");
    quantize.addItem (11, "Starts and ends");
    quantize.addItem (12, "Starts, half way (50 %)");
    menu.addSubMenu ("Quantize to the snap", quantize);
    juce::PopupMenu human;
    human.addItem (20, "Light (5 ticks, 5 % velocity)");
    human.addItem (21, "Medium (10 ticks, 10 % velocity)");
    human.addItem (22, "Strong (20 ticks, 18 % velocity)");
    menu.addSubMenu ("Humanize", human);
    menu.addSeparator();
    menu.addItem (30, "Legato (each note lasts until the next)");
    menu.addItem (31, "Staccato (half the length)");
    juce::PopupMenu strum;
    strum.addItem (40, "Up, fast (10 ticks)");
    strum.addItem (41, "Up, slow (30 ticks)");
    strum.addItem (42, "Down, fast (10 ticks)");
    strum.addItem (43, "Down, slow (30 ticks)");
    menu.addSubMenu ("Strum chords", strum);
    juce::PopupMenu arp;
    arp.addItem (50, "Up, 16ths");
    arp.addItem (51, "Down, 16ths");
    arp.addItem (52, "Up and down, 16ths");
    arp.addItem (53, "Up, 8ths");
    arp.addItem (54, "Up and down, 8ths");
    arp.addItem (55, "Random, 16ths");
    menu.addSubMenu ("Arpeggiate chords", arp);
    juce::PopupMenu chop;
    chop.addItem (60, "Into 8ths");
    chop.addItem (61, "Into 16ths");
    chop.addItem (62, "Into 32nds");
    menu.addSubMenu ("Chop", chop);
    menu.addSeparator();
    menu.addItem (70, "Flip (upside down)");
    menu.addItem (74, "Reverse (flip in time)");        // 0.61.0, as FL Studio's tools
    menu.addItem (75, "Glue (join touching notes of one pitch)");
    {
        juce::PopupMenu flamMenu;
        flamMenu.addItem (76, "Light (grace note 1/64 before, 60 % velocity)");
        flamMenu.addItem (77, "Strong (1/32 before, 80 % velocity)");
        menu.addSubMenu ("Flam", flamMenu);
    }
    {
        // 0.62.0: Limit, the LFO on the event lane, the notes' colour groups
        juce::PopupMenu limitMenu;
        limitMenu.addItem (90, "To two octaves (C4 - B5)");
        limitMenu.addItem (91, "To three octaves (C3 - B5)");
        limitMenu.addItem (92, "To one octave (C5 - B5)");
        menu.addSubMenu ("Limit (move notes by octaves into a range)", limitMenu);
        juce::PopupMenu lfoMenu;
        lfoMenu.addItem (93, "One cycle per beat");
        lfoMenu.addItem (94, "One cycle per bar");
        lfoMenu.addItem (95, "One cycle every 2 bars");
        lfoMenu.addItem (96, "One cycle every 4 bars");
        menu.addSubMenu (juce::String ("LFO on the event lane (") + PianoRoll::laneName (roll.getLane()) + ")", lfoMenu);
        juce::PopupMenu colourMenu;
        for (int c = 0; c < 16; ++c)
        {
            juce::PopupMenu::Item item ("Colour " + juce::String (c + 1) + (c == 0 ? " (default)" : ""));
            item.itemID = 300 + c;
            item.colour = PianoRoll::noteColour (c);
            item.isTicked = c == roll.getNoteColour();
            colourMenu.addItem (item);
        }
        menu.addSubMenu ("Note colour", colourMenu);
    }
    menu.addItem (71, "Transpose up an octave");
    menu.addItem (72, "Transpose down an octave");
    menu.addItem (73, "Snap to the scale", scale.type != 0);
    menu.addSeparator();
    juce::PopupMenu velocity;
    velocity.addItem (80, "Crescendo (soft to loud)");
    velocity.addItem (81, "Decrescendo (loud to soft)");
    velocity.addItem (82, "Random (60 - 100 %)");
    velocity.addItem (83, "All 100 %");
    velocity.addItem (84, "All 80 %");
    velocity.addItem (85, "All 50 %");
    menu.addSubMenu ("Velocity", velocity);

    const int grid = roll.getGridTicks();
    const int pattern = roll.getLoopLengthTicks();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&toolsButton),
                        [safe = juce::Component::SafePointer<PianoRollToolbar> (this), grid, pattern, scale] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& editor = safe->roll;
                            using Notes = std::vector<Note>;
                            using Sel = std::vector<bool>;
                            const auto seed = freshSeed();
                            if (result >= 300 && result < 316) // 0.62.0: a colour group for the selected notes and the next ones
                            {
                                editor.setNoteColour (result - 300);
                                return;
                            }
                            switch (result)
                            {
                                case 1: editor.selectAll(); break;
                                case 10: editor.applyTool ([=] (Notes& n, Sel& s) { theory::quantize (n, s, grid, 1.0f, false, pattern); }, "Quantized the starts"); break;
                                case 11: editor.applyTool ([=] (Notes& n, Sel& s) { theory::quantize (n, s, grid, 1.0f, true, pattern); }, "Quantized starts and ends"); break;
                                case 12: editor.applyTool ([=] (Notes& n, Sel& s) { theory::quantize (n, s, grid, 0.5f, false, pattern); }, "Quantized the starts half way"); break;
                                case 20: editor.applyTool ([=] (Notes& n, Sel& s) { theory::humanize (n, s, 5, 0.05f, seed, pattern); }, "Humanized (light)"); break;
                                case 21: editor.applyTool ([=] (Notes& n, Sel& s) { theory::humanize (n, s, 10, 0.10f, seed, pattern); }, "Humanized (medium)"); break;
                                case 22: editor.applyTool ([=] (Notes& n, Sel& s) { theory::humanize (n, s, 20, 0.18f, seed, pattern); }, "Humanized (strong)"); break;
                                case 30: editor.applyTool ([=] (Notes& n, Sel& s) { theory::legato (n, s, pattern); }, "Legato"); break;
                                case 31: editor.applyTool ([=] (Notes& n, Sel& s) { theory::staccato (n, s, 0.5f, 20); }, "Staccato"); break;
                                case 40: case 41: case 42: case 43:
                                    editor.applyTool ([=] (Notes& n, Sel& s) { theory::strum (n, s, result % 2 == 0 ? 10 : 30, result < 42); }, "Strummed the chords");
                                    break;
                                case 50: case 51: case 52: case 53: case 54: case 55:
                                {
                                    const auto pat = result == 51 ? theory::ArpPattern::Down : (result == 52 || result == 54) ? theory::ArpPattern::UpDown
                                                   : result == 55 ? theory::ArpPattern::Random : theory::ArpPattern::Up;
                                    const int rate = result == 53 || result == 54 ? kTicksPerBeat / 2 : kTicksPerBeat / 4;
                                    editor.applyTool ([=] (Notes& n, Sel& s) { theory::arpeggiate (n, s, rate, pat, seed); }, "Arpeggiated the chords");
                                    break;
                                }
                                case 60: case 61: case 62:
                                {
                                    const int piece = result == 60 ? kTicksPerBeat / 2 : result == 61 ? kTicksPerBeat / 4 : kTicksPerBeat / 8;
                                    editor.applyTool ([=] (Notes& n, Sel& s) { theory::chop (n, s, piece); }, "Chopped");
                                    break;
                                }
                                case 70: editor.applyTool ([] (Notes& n, Sel& s) { theory::flip (n, s); }, "Flipped"); break;
                                case 90: editor.applyTool ([] (Notes& n, Sel& s) { theory::limit (n, s, 48, 71); }, "Limited to C4 - B5"); break;
                                case 91: editor.applyTool ([] (Notes& n, Sel& s) { theory::limit (n, s, 36, 71); }, "Limited to C3 - B5"); break;
                                case 92: editor.applyTool ([] (Notes& n, Sel& s) { theory::limit (n, s, 60, 71); }, "Limited to C5 - B5"); break;
                                case 93: editor.applyLaneLfo (kTicksPerBeat, 1.0f); break;
                                case 94: editor.applyLaneLfo (interfaceMeter().barTicks(), 1.0f); break;
                                case 95: editor.applyLaneLfo (2 * interfaceMeter().barTicks(), 1.0f); break;
                                case 96: editor.applyLaneLfo (4 * interfaceMeter().barTicks(), 1.0f); break;
                                case 74: editor.applyTool ([] (Notes& n, Sel& s) { theory::reverse (n, s); }, "Reversed in time"); break;
                                case 75: editor.applyTool ([] (Notes& n, Sel& s) { theory::glue (n, s); }, "Glued"); break;
                                case 76: editor.applyTool ([] (Notes& n, Sel& s) { theory::flam (n, s, kTicksPerBeat / 16, 0.6f); }, "Flammed (light)"); break;
                                case 77: editor.applyTool ([] (Notes& n, Sel& s) { theory::flam (n, s, kTicksPerBeat / 8, 0.8f); }, "Flammed (strong)"); break;
                                case 71: editor.applyTool ([] (Notes& n, Sel& s) { theory::transpose (n, s, 12); }, "Up an octave"); break;
                                case 72: editor.applyTool ([] (Notes& n, Sel& s) { theory::transpose (n, s, -12); }, "Down an octave"); break;
                                case 73: editor.applyTool ([=] (Notes& n, Sel& s) { theory::snapToScale (n, s, scale); }, "Snapped to the scale"); break;
                                case 80: editor.applyTool ([] (Notes& n, Sel& s) { theory::velocityRamp (n, s, 0.35f, 1.0f); }, "Crescendo"); break;
                                case 81: editor.applyTool ([] (Notes& n, Sel& s) { theory::velocityRamp (n, s, 1.0f, 0.35f); }, "Decrescendo"); break;
                                case 82: editor.applyTool ([=] (Notes& n, Sel& s) { theory::randomizeVelocity (n, s, 0.6f, 1.0f, seed); }, "Random velocities"); break;
                                case 83: case 84: case 85:
                                {
                                    const float v = result == 83 ? 1.0f : result == 84 ? 0.8f : 0.5f;
                                    editor.applyTool ([=] (Notes& n, Sel& s) { theory::setVelocity (n, s, v); }, "Velocity " + juce::String (juce::roundToInt (v * 100.0f)) + " %");
                                    break;
                                }
                                default: break;
                            }
                        });
}

void PianoRollToolbar::showGenerateMenu()
{
    const auto scale = currentScale();
    const auto& progs = theory::progressions();
    const juce::String key = juce::String (theory::pitchClassName (scale.root)) + " " + (scale.type == 0 ? "major" : juce::String (theory::scales()[static_cast<size_t> (scale.type)].name).toLowerCase());

    juce::PopupMenu menu;
    menu.addSectionHeader ("Key: " + key + (scale.type == 0 ? " (choose a scale in the toolbar)" : ""));

    // Chords: a submenu per rhythm, a progression per item (ids 1000 + rhythm * 100 + progression).
    juce::PopupMenu chordsMenu;
    for (int r = 0; r < 4; ++r)
    {
        juce::PopupMenu sub;
        for (size_t p = 0; p < progs.size(); ++p)
            sub.addItem (1000 + r * 100 + static_cast<int> (p), progs[p].name);
        chordsMenu.addSubMenu (theory::chordRhythmName (static_cast<theory::ChordRhythm> (r)), sub);
    }
    menu.addSubMenu ("Chord progression (one chord per bar)", chordsMenu);
    juce::PopupMenu halfChords;
    for (size_t p = 0; p < progs.size(); ++p)
        halfChords.addItem (1500 + static_cast<int> (p), progs[p].name);
    menu.addSubMenu ("Chord progression (two chords per bar, held)", halfChords);

    // Basslines: following another channel (2000 + style * 100 + channel), or on a progression (3000 + style * 100 + progression).
    const auto others = getOtherChannels != nullptr ? getOtherChannels() : std::vector<OtherChannel> {};
    juce::PopupMenu bassMenu;
    for (int st = 0; st < 6; ++st)
    {
        juce::PopupMenu sub;
        bool anyChannel = false;
        for (size_t c = 0; c < others.size() && c < 60; ++c)
            if (! others[c].notes.empty())
            {
                sub.addItem (2000 + st * 100 + static_cast<int> (c), "Follow the chords of " + others[c].name);
                anyChannel = true;
            }
        if (anyChannel)
            sub.addSeparator();
        for (size_t p = 0; p < progs.size(); ++p)
            sub.addItem (3000 + st * 100 + static_cast<int> (p), "On " + juce::String (progs[p].name));
        bassMenu.addSubMenu (theory::bassStyleName (static_cast<theory::BassStyle> (st)), sub);
    }
    menu.addSubMenu ("Bassline", bassMenu);

    const int pattern = roll.getLoopLengthTicks();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&generateButton),
                        [safe = juce::Component::SafePointer<PianoRollToolbar> (this), scale, pattern, others] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            const auto& list = theory::progressions();
                            const auto keyScale = scale.type == 0 ? theory::Scale { scale.root, 1 } : scale; // chromatic: major
                            const int bar = 4 * kTicksPerBeat;
                            if (result >= 1000 && result < 1500)
                            {
                                const int rhythm = (result - 1000) / 100, prog = (result - 1000) % 100;
                                safe->roll.replaceAllNotes (theory::progressionNotes (keyScale, prog, static_cast<theory::ChordRhythm> (rhythm), bar, pattern),
                                                            juce::String ("Chords: ") + list[static_cast<size_t> (prog)].name + " (Ctrl+Z to undo)");
                            }
                            else if (result >= 1500 && result < 2000)
                            {
                                const int prog = result - 1500;
                                safe->roll.replaceAllNotes (theory::progressionNotes (keyScale, prog, theory::ChordRhythm::Held, bar / 2, pattern),
                                                            juce::String ("Chords: ") + list[static_cast<size_t> (prog)].name + " (Ctrl+Z to undo)");
                            }
                            else if (result >= 2000 && result < 3000)
                            {
                                const int style = (result - 2000) / 100, channel = (result - 2000) % 100;
                                if (channel >= static_cast<int> (others.size()))
                                    return;
                                const auto spans = theory::detectChords (others[static_cast<size_t> (channel)].notes, 2 * kTicksPerBeat, pattern);
                                if (spans.empty())
                                    return;
                                juce::String names;
                                for (size_t i = 0; i < spans.size() && i < 8; ++i)
                                    names << (i > 0 ? " " : "") << theory::pitchClassName (spans[i].root);
                                safe->roll.replaceAllNotes (theory::basslineNotes (spans, static_cast<theory::BassStyle> (style), keyScale),
                                                            "Bassline on " + others[static_cast<size_t> (channel)].name + "'s chords (" + names + "; Ctrl+Z to undo)");
                            }
                            else if (result >= 3000 && result < 4000)
                            {
                                const int style = (result - 3000) / 100, prog = (result - 3000) % 100;
                                const auto spans = theory::progressionChords (keyScale, prog, bar, pattern);
                                safe->roll.replaceAllNotes (theory::basslineNotes (spans, static_cast<theory::BassStyle> (style), keyScale),
                                                            juce::String ("Bassline on ") + list[static_cast<size_t> (prog)].name + " (Ctrl+Z to undo)");
                            }
                        });
}

PianoRollEditor::PianoRollEditor (PianoRollToolbar& toolbarToShow, PianoRoll& rollToShow) : toolbar (toolbarToShow), roll (rollToShow)
{
    addAndMakeVisible (toolbar);
    addAndMakeVisible (roll);
}

void PianoRollEditor::resized()
{
    auto r = getLocalBounds();
    toolbar.setBounds (r.removeFromTop (PianoRollToolbar::kHeight));
    roll.setBounds (r);
}
} // namespace sls
