// Part of MainComponent: the Loop Starter (0.30.0) - a starting loop in a genre, made into channels and a new pattern.
#include "ui/MainComponent.h"

#include "audio/LoopStarter.h"
#include "audio/SoundLibrary.h"
#include "library/DrumKitFile.h"

namespace sls
{
namespace
{
// A short word for the genre, for channel and pattern names (channel names are at most 16 characters).
const char* genreTag (loopstarter::Genre g)
{
    switch (g)
    {
        case loopstarter::Genre::House: return "House";
        case loopstarter::Genre::Techno: return "Techno";
        case loopstarter::Genre::HipHop: return "Hip-hop";
        case loopstarter::Genre::Trap: return "Trap";
        case loopstarter::Genre::DrumAndBass: return "DnB";
        case loopstarter::Genre::Pop: return "Pop";
        case loopstarter::Genre::LoFi: return "Lo-fi";
        case loopstarter::Genre::Reggaeton: return "Reggaeton";
        case loopstarter::Genre::Rock: return "Rock";
        case loopstarter::Genre::Synthwave: return "Synthwave";
    }
    return "Loop";
}

// Gives the channel the sound a part asks for: a built-in drum kit, an instrument of the sound library, or a factory synth patch.
// Returns false when neither the sound nor its fallback could be used (the channel is then left as it was).
bool applySound (Channel& c, const std::string& id)
{
    if (id.rfind ("kit:", 0) == 0)
    {
        const auto kitId = id.substr (4);
        bool exists = false;
        for (const auto& k : builtinKits())
            exists = exists || k.id == kitId;
        if (! exists)
            return false;
        juce::StringArray problems;
        auto kit = std::make_shared<const DrumKit> (loadKit (builtinKitSpec (kitId), problems));
        if (! problems.isEmpty())
            return false;
        c.instrument = Instrument::Drums;
        c.kit = std::move (kit);
        c.program = nullptr;
        return true;
    }
    if (id.rfind ("program:", 0) == 0)
    {
        std::string error;
        SamplerSettings settings;
        auto program = makeLibraryProgram (id.substr (8), &settings, error);
        if (program == nullptr)
            return false;
        c.instrument = Instrument::Sampler;
        c.sampler = settings;
        c.program = std::move (program);
        c.sample = nullptr;
        return true;
    }
    if (id.rfind ("patch:", 0) == 0)
        for (const auto& p : synthFactoryPatches())
            if ("patch:" + p.name == id)
            {
                c.instrument = Instrument::Synth;
                c.synth = p.patch;
                c.program = nullptr;
                return true;
            }
    return false;
}
} // namespace

void MainComponent::openLoopStarter()
{
    namespace ls = loopstarter;
    auto* window = new juce::AlertWindow ("Loop Starter",
                                          "Makes a starting loop in a genre - drums, bass, chords and a melody that fit together - "
                                          "as a new pattern, with its own channels and the built-in sounds. Each variation is "
                                          "different; the same one always comes out the same.",
                                          juce::MessageBoxIconType::NoIcon);
    juce::StringArray genres;
    for (int g = 0; g < ls::kGenreCount; ++g)
    {
        const auto& info = ls::genreInfo (static_cast<ls::Genre> (g));
        genres.add (juce::String (info.name) + "  (" + juce::String (juce::roundToInt (info.bpm)) + " BPM)");
    }
    window->addComboBox ("genre", genres, "Genre");
    juce::StringArray keys;
    for (int k = 0; k < 12; ++k)
        keys.add (theory::pitchClassName (k));
    window->addComboBox ("key", keys, "Key");
    window->addComboBox ("mode", { "Minor", "Major", "The genre's usual one" }, "Mode");
    window->addComboBox ("parts", { "Drums, bass, chords and melody", "Drums, bass and chords", "Drums and bass", "Drums only",
                                    "Bass, chords and melody (no drums)" },
                         "Parts");
    window->addComboBox ("tempo", { "Use the genre's tempo", "Keep the current tempo" }, "Tempo");
    window->addTextEditor ("variation", juce::String (loopStarterVariation + 1), "Variation (a number)");
    window->getComboBoxComponent ("genre")->setSelectedItemIndex (loopStarterGenre, juce::dontSendNotification);
    window->getComboBoxComponent ("key")->setSelectedItemIndex (loopStarterKey, juce::dontSendNotification);
    window->getComboBoxComponent ("mode")->setSelectedItemIndex (2, juce::dontSendNotification);
    window->getComboBoxComponent ("parts")->setSelectedItemIndex (loopStarterParts, juce::dontSendNotification);
    window->getComboBoxComponent ("tempo")->setSelectedItemIndex (0, juce::dontSendNotification);
    window->addButton ("Make it", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
                                       [safe = juce::Component::SafePointer<MainComponent> (this), window] (int result)
                                       {
                                           if (safe == nullptr || result != 1)
                                               return;
                                           ls::Request q;
                                           const int genre = std::max (0, window->getComboBoxComponent ("genre")->getSelectedItemIndex());
                                           q.genre = static_cast<ls::Genre> (genre);
                                           q.key = std::max (0, window->getComboBoxComponent ("key")->getSelectedItemIndex());
                                           const int mode = window->getComboBoxComponent ("mode")->getSelectedItemIndex();
                                           q.minor = mode == 2 ? ls::genreInfo (q.genre).minor : mode == 0;
                                           const int parts = std::max (0, window->getComboBoxComponent ("parts")->getSelectedItemIndex());
                                           q.drums = parts != 4;
                                           q.bass = parts != 3;
                                           q.chords = parts == 0 || parts == 1 || parts == 4;
                                           q.melody = parts == 0 || parts == 4;
                                           const auto typed = window->getTextEditorContents ("variation").trim();
                                           q.variation = typed.containsOnly ("0123456789") && typed.isNotEmpty()
                                                             ? static_cast<std::uint32_t> (typed.getLargeIntValue() & 0xffffffff)
                                                             : static_cast<std::uint32_t> (typed.hashCode());
                                           q.patternTicks = safe->arrangement.patternTicks;
                                           const bool setTempo = window->getComboBoxComponent ("tempo")->getSelectedItemIndex() == 0;

                                           safe->loopStarterGenre = genre;
                                           safe->loopStarterKey = q.key;
                                           safe->loopStarterParts = parts;
                                           safe->loopStarterVariation = q.variation;
                                           safe->applyLoop (ls::make (q), q.variation, setTempo);
                                       }),
                             true);
}

void MainComponent::applyLoop (const loopstarter::Loop& loop, std::uint32_t variation, bool setTempo)
{
    if (arrangement.patterns.size() >= static_cast<size_t> (kMaxPatterns))
    {
        statusLabel.setText ("Loop Starter: the project already has " + juce::String (kMaxPatterns) + " patterns", juce::dontSendNotification);
        return;
    }

    // Each part goes to a channel named after the genre and the part ("House Drums"); one made by an earlier loop of the same genre
    // is used again (with this loop's sound), so trying variations does not fill the rack.
    const juce::String tag = genreTag (loop.genre);
    std::vector<int> target;
    int missing = 0;
    for (const auto& part : loop.parts)
    {
        const auto name = (tag + " " + juce::String (part.name)).toStdString();
        int found = -1;
        for (size_t i = 0; i < channels.size(); ++i)
            if (channels[i].name == name)
                found = static_cast<int> (i);
        target.push_back (found);
        missing += found < 0 ? 1 : 0;
    }
    if (channels.size() + static_cast<size_t> (missing) > static_cast<size_t> (kMaxChannels))
    {
        statusLabel.setText ("Loop Starter: it needs " + juce::String (missing) + " more channels, and the rack has room for "
                                 + juce::String (kMaxChannels - static_cast<int> (channels.size())),
                             juce::dontSendNotification);
        return;
    }

    juce::MouseCursor::showWaitCursor(); // the piano and the bass are decoded the first time
    commitCurrentPattern();
    int fallbacks = 0;
    for (size_t p = 0; p < loop.parts.size(); ++p)
    {
        const auto& part = loop.parts[p];
        if (target[p] < 0)
        {
            Channel c;
            c.name = (tag + " " + juce::String (part.name)).toStdString();
            c.mixerTrack = std::min (static_cast<int> (channels.size()), static_cast<int> (tracks.size()) - 1);
            jassert (channels.size() < channels.capacity()); // push_back must not reallocate (strips hold references)
            channels.push_back (std::move (c));
            arrangement.ensureChannelCount (channels.size());
            createStrip (channels.size() - 1);
            target[p] = static_cast<int> (channels.size()) - 1;
        }
        auto& c = channels[static_cast<size_t> (target[p])];
        if (! applySound (c, part.sound.id))
        {
            ++fallbacks;
            applySound (c, part.sound.fallback);
        }
        c.volume = part.volume;
        c.swing = part.swing;
    }
    juce::MouseCursor::hideWaitCursor();
    refreshRackStrips();

    // A new pattern with the loop's notes on those channels.
    Pattern pattern;
    pattern.name = (tag + " " + juce::String (variation)).toStdString();
    pattern.notes.resize (channels.size());
    for (size_t p = 0; p < loop.parts.size(); ++p)
        pattern.notes[static_cast<size_t> (target[p])] = loop.parts[p].notes;
    arrangement.patterns.push_back (std::move (pattern));
    arrangement.ensureChannelCount (channels.size());

    if (setTempo)
    {
        bpmSlider.setValue (loop.bpm, juce::sendNotificationSync);
        playlistView.setBpm (loop.bpm);
    }
    refreshPatternBox();
    updateTrackRouting();
    if (songMode)
        setSongMode (false); // so Play plays the new loop
    selectPattern (static_cast<int> (arrangement.patterns.size()) - 1);
    selectChannel (target.front());
    markDirty();
    publishProject();

    const auto& scaleName = theory::scales()[static_cast<size_t> (loop.scale.type)].name;
    juce::String text = "Loop Starter: " + juce::String (loopstarter::genreInfo (loop.genre).name) + " in "
                      + theory::pitchClassName (loop.scale.root) + " " + juce::String (scaleName).toLowerCase() + ", "
                      + juce::String (loop.progression) + " - press Play";
    if (fallbacks > 0)
        text << " (" << fallbacks << " sound(s) replaced by a synth: the sound library is not built in)";
    statusLabel.setText (text, juce::dontSendNotification);
}
} // namespace sls
