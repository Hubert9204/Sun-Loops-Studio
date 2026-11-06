#include "library/ProjectFile.h"

#include "library/JsonRead.h"

#include <algorithm>
#include <map>
#include <cmath>

namespace sls
{
namespace
{
constexpr size_t kMaxNotesPerChannelPattern = 20000;

const char* automationKindName (AutomationKind k)
{
    switch (k)
    {
        case AutomationKind::ChannelVolume: return "channelVolume";
        case AutomationKind::ChannelPan:    return "channelPan";
        case AutomationKind::TrackVolume:   return "trackVolume";
        case AutomationKind::TrackPan:      return "trackPan";
        case AutomationKind::TrackEffect:   return "trackEffect";
        case AutomationKind::MasterEffect:  return "masterEffect";
    }
    return "trackVolume";
}

AutomationKind automationKindFromName (const std::string& name, bool& known)
{
    known = true;
    for (int i = 0; i < 6; ++i)
        if (name == automationKindName (static_cast<AutomationKind> (i)))
            return static_cast<AutomationKind> (i);
    known = false;
    return AutomationKind::TrackVolume;
}

const char* effectKey (EffectType t)
{
    switch (t)
    {
        case EffectType::Equalizer:  return "equalizer";
        case EffectType::Reverb:     return "reverb";
        case EffectType::Delay:      return "delay";
        case EffectType::Compressor: return "compressor";
        case EffectType::None:       break;
    }
    return "none";
}

EffectType effectFromKey (const juce::String& key)
{
    for (int t = 0; t < kEffectTypeCount; ++t)
        if (key == effectKey (static_cast<EffectType> (t)))
            return static_cast<EffectType> (t);
    return EffectType::None; // unknown effects (from a future version) become an empty slot
}

// ------------------------------------------------------------------------- writing

juce::var noteToVar (const Note& n)
{
    juce::Array<juce::var> a;
    a.add (n.pitch);
    a.add (n.startTick);
    a.add (n.lengthTicks);
    a.add (number (static_cast<double> (n.velocity)));
    if (n.link != NoteLink::None)
        a.add (static_cast<int> (n.link)); // 1 = legato, 2 = slide; older programs ignore this fifth value
    return a;
}

juce::var slotToVar (const EffectSlot& slot)
{
    const auto s = slot.sanitised();
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", effectKey (s.type));
    o->setProperty ("bypass", s.bypass);
    juce::Array<juce::var> params;
    for (int i = 0; i < effectParamCount (s.type); ++i)
        params.add (number (static_cast<double> (s.params[static_cast<size_t> (i)])));
    o->setProperty ("params", params);
    return juce::var (o);
}

// ------------------------------------------------------------------------- reading

struct Reader
{
    std::string error;
    std::vector<std::string> warnings;
    bool failed() const { return ! error.empty(); }
    void fail (const std::string& message) { if (error.empty()) error = message; }
};


EffectSlot readSlot (const juce::var& v)
{
    auto* o = v.getDynamicObject();
    if (o == nullptr)
        return {};

    EffectSlot slot = EffectSlot::make (effectFromKey (o->getProperty ("type").toString()));
    slot.bypass = readBool (o->getProperty ("bypass"), false);

    if (auto* params = o->getProperty ("params").getArray())
        for (int i = 0; i < std::min (params->size(), effectParamCount (slot.type)); ++i)
        {
            const auto info = effectParamInfo (slot.type, i);
            slot.params[static_cast<size_t> (i)] = static_cast<float> (readNumber ((*params)[i], info.def, info.min, info.max));
        }
    return slot.sanitised();
}

bool readNote (const juce::var& v, int patternTicks, Note& out)
{
    auto* a = v.getArray();
    if (a == nullptr || a->size() < 3)
        return false;

    const double pitch = readNumber ((*a)[0], -1, -1, 1000);
    const double start = readNumber ((*a)[1], -1, -1, 1.0e9);
    const double length = readNumber ((*a)[2], 0, 0, 1.0e9);
    if (pitch < 0 || pitch > 127 || start < 0 || start >= patternTicks || length < 1)
        return false;

    out.pitch = static_cast<int> (std::lround (pitch));
    out.startTick = static_cast<int> (std::lround (start));
    out.lengthTicks = std::min (static_cast<int> (std::lround (length)), patternTicks - out.startTick);
    out.velocity = static_cast<float> (readNumber (a->size() > 3 ? (*a)[3] : juce::var (0.8), 0.8, 0.0, 1.0));
    const int link = static_cast<int> (std::lround (readNumber (a->size() > 4 ? (*a)[4] : juce::var (0), 0, 0, 2)));
    out.link = link == 1 ? NoteLink::Legato : link == 2 ? NoteLink::Slide : NoteLink::None;
    return true;
}
} // namespace

// ------------------------------------------------------------------------- ProjectDocument

ProjectDocument ProjectDocument::blank()
{
    ProjectDocument d;

    Channel c;
    c.name = "Synth 1";
    c.volume = 0.8f;
    d.channels.push_back (c);
    d.sampleSources.emplace_back();
    d.kitSpecs.push_back (KitSpec::empty());

    d.tracks = Project::defaultTracks();

    d.arrangement.patterns.resize (1);
    d.arrangement.patterns[0].name = "Pattern 1";
    d.arrangement.ensureChannelCount (d.channels.size());
    return d;
}

juce::String ProjectDocument::toJson() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", kProjectFormatName);
    root->setProperty ("version", kProjectFormatVersion);
    root->setProperty ("bpm", number (bpm));
    root->setProperty ("masterVolume", number (static_cast<double> (masterVolume)));
    root->setProperty ("songMode", songMode);
    root->setProperty ("currentPattern", currentPattern);

    juce::Array<juce::var> channelList;
    for (size_t i = 0; i < channels.size(); ++i)
    {
        const auto& c = channels[i];
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", juce::String (c.name));
        o->setProperty ("instrument", c.instrument == Instrument::Sampler ? "sampler" : c.instrument == Instrument::Drums ? "drums" : "synth");
        if (c.instrument == Instrument::Drums)
            o->setProperty ("kit", kitSpecToVar (i < kitSpecs.size() ? kitSpecs[i] : KitSpec::empty()));
        o->setProperty ("sample", juce::String (i < sampleSources.size() ? sampleSources[i] : std::string()));
        o->setProperty ("rootPitch", c.rootPitch);
        o->setProperty ("volume", number (static_cast<double> (c.volume)));
        o->setProperty ("pan", number (static_cast<double> (c.pan)));
        o->setProperty ("mute", c.mute);
        o->setProperty ("solo", c.solo);
        o->setProperty ("pitch", number (static_cast<double> (c.pitch)));
        o->setProperty ("swing", number (static_cast<double> (c.swing)));
        o->setProperty ("mixerTrack", c.mixerTrack);
        channelList.add (juce::var (o));
    }
    root->setProperty ("channels", channelList);

    juce::Array<juce::var> trackList;
    for (const auto& t : tracks)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", juce::String (t.name));
        o->setProperty ("volume", number (static_cast<double> (t.volume)));
        o->setProperty ("pan", number (static_cast<double> (t.pan)));
        o->setProperty ("mute", t.mute);
        o->setProperty ("solo", t.solo);
        juce::Array<juce::var> slots;
        for (const auto& s : t.slots)
            slots.add (slotToVar (s));
        o->setProperty ("effects", slots);
        juce::Array<juce::var> sendList;
        for (const auto send : t.sends)
            sendList.add (number (static_cast<double> (send)));
        o->setProperty ("sends", sendList);
        o->setProperty ("sendsPreFader", t.sendsPreFader);
        o->setProperty ("vca", static_cast<int> (t.vcaMask)); // bit n: member of VCA group n
        trackList.add (juce::var (o));
    }
    root->setProperty ("tracks", trackList);

    {
        auto* consoleObject = new juce::DynamicObject();
        juce::Array<juce::var> auxList;
        for (const auto& a : console.aux)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", juce::String (a.name));
            o->setProperty ("volume", number (static_cast<double> (a.volume)));
            o->setProperty ("pan", number (static_cast<double> (a.pan)));
            o->setProperty ("mute", a.mute);
            juce::Array<juce::var> slots;
            for (const auto& s : a.slots)
                slots.add (slotToVar (s));
            o->setProperty ("effects", slots);
            auxList.add (juce::var (o));
        }
        consoleObject->setProperty ("aux", auxList);
        juce::Array<juce::var> vcaList;
        for (size_t g = 0; g < console.vcaLevel.size(); ++g)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("level", number (static_cast<double> (console.vcaLevel[g])));
            o->setProperty ("mute", static_cast<bool> (console.vcaMute[g]));
            vcaList.add (juce::var (o));
        }
        consoleObject->setProperty ("vca", vcaList);
        root->setProperty ("console", juce::var (consoleObject));
    }

    {
        auto* master = new juce::DynamicObject();
        juce::Array<juce::var> slots;
        for (const auto& s : masterSlots)
            slots.add (slotToVar (s));
        master->setProperty ("effects", slots);
        root->setProperty ("master", juce::var (master));
    }

    juce::Array<juce::var> patternList;
    for (const auto& p : arrangement.patterns)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", juce::String (p.name));
        juce::Array<juce::var> perChannel;
        for (size_t c = 0; c < channels.size(); ++c)
        {
            juce::Array<juce::var> notes;
            if (c < p.notes.size())
                for (const auto& n : p.notes[c])
                    notes.add (noteToVar (n));
            perChannel.add (notes);
        }
        o->setProperty ("notes", perChannel);
        patternList.add (juce::var (o));
    }
    root->setProperty ("patterns", patternList);
    root->setProperty ("patternBars", arrangement.patternTicks / (4 * kTicksPerBeat));

    juce::Array<juce::var> clipList;
    for (const auto& c : arrangement.clips)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("pattern", c.pattern);
        o->setProperty ("track", c.track);
        o->setProperty ("start", c.startUnit);
        o->setProperty ("length", c.lengthUnits);
        clipList.add (juce::var (o));
    }
    root->setProperty ("clips", clipList);

    // Recordings and imported audio: only the path of the file is stored, never the audio itself.
    juce::Array<juce::var> audioList;
    for (const auto& a : arrangement.audioClips)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("source", juce::String (a.data != nullptr ? a.data->source : std::string()));
        o->setProperty ("name", juce::String (a.name));
        o->setProperty ("track", a.track);
        o->setProperty ("startTick", a.startTick);
        o->setProperty ("lengthTicks", a.lengthTicks);
        o->setProperty ("gain", static_cast<double> (a.gain));
        o->setProperty ("offsetSeconds", a.offsetSeconds);
        if (! a.stretch.isNeutral())
        {
            // Only the settings: the audio is stretched again when the project is opened.
            auto* st = new juce::DynamicObject();
            st->setProperty ("lengthRatio", a.stretch.lengthRatio);
            st->setProperty ("semitones", a.stretch.semitones);
            st->setProperty ("keepTimbre", a.stretch.keepTimbre);
            o->setProperty ("stretch", juce::var (st));
        }
        audioList.add (juce::var (o));
    }
    root->setProperty ("audioClips", audioList);

    // Automation clips: the parameter (as a name and numbers), where it sits and its points.
    juce::Array<juce::var> automationList;
    for (const auto& a : arrangement.automationClips)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", juce::String (automationKindName (a.target.kind)));
        o->setProperty ("index", a.target.index);
        o->setProperty ("slot", a.target.slot);
        o->setProperty ("param", a.target.param);
        o->setProperty ("lane", a.lane);
        o->setProperty ("startTick", a.startTick);
        o->setProperty ("lengthTicks", a.lengthTicks);
        juce::Array<juce::var> pointList;
        for (const auto& p : a.points)
        {
            juce::Array<juce::var> one;
            one.add (p.tick);
            one.add (static_cast<double> (p.value));
            one.add (static_cast<int> (p.shape));
            pointList.add (juce::var (one));
        }
        o->setProperty ("points", pointList);
        automationList.add (juce::var (o));
    }
    root->setProperty ("automation", automationList);

    if (arrangement.hasLoop())
    {
        auto* loop = new juce::DynamicObject();
        loop->setProperty ("start", arrangement.loopStartTick);
        loop->setProperty ("end", arrangement.loopEndTick);
        root->setProperty ("loopRegion", juce::var (loop));
    }

    return juce::JSON::toString (juce::var (root), false);
}

ProjectParseResult ProjectDocument::fromJson (const juce::String& text)
{
    ProjectParseResult result;
    Reader r;

    const auto parsed = juce::JSON::parse (text);
    auto* root = parsed.getDynamicObject();
    if (root == nullptr || root->getProperty ("format").toString() != kProjectFormatName)
    {
        result.error = "This is not a Sun Loops Studio project file.";
        return result;
    }

    const auto version = root->getProperty ("version");
    if (! (version.isInt() || version.isInt64() || version.isDouble()) || static_cast<int> (version) < 1)
    {
        result.error = "The project file has no valid version number.";
        return result;
    }
    if (static_cast<int> (version) > kProjectFormatVersion)
    {
        result.error = "This project was saved by a newer version of Sun Loops Studio (file format "
                     + std::to_string (static_cast<int> (version)) + "). Please update the program.";
        return result;
    }

    ProjectDocument d;
    d.bpm = readNumber (root->getProperty ("bpm"), 120.0, 40.0, 240.0);
    d.masterVolume = static_cast<float> (readNumber (root->getProperty ("masterVolume"), 0.8, 0.0, 1.0));
    d.songMode = readBool (root->getProperty ("songMode"), false);

    // --- channels
    auto* channelList = root->getProperty ("channels").getArray();
    if (channelList == nullptr || channelList->isEmpty() || channelList->size() > kMaxChannels)
    {
        result.error = "The project must have between 1 and " + std::to_string (kMaxChannels) + " channels.";
        return result;
    }

    for (int i = 0; i < channelList->size(); ++i)
    {
        auto* o = (*channelList)[i].getDynamicObject();
        if (o == nullptr)
        {
            result.error = "Channel " + std::to_string (i + 1) + " is damaged.";
            return result;
        }

        Channel c;
        c.name = readName (o->getProperty ("name"), "Channel " + std::to_string (i + 1));
        const auto instrument = o->getProperty ("instrument").toString();
        if (instrument == "sampler")
            c.instrument = Instrument::Sampler;
        else if (instrument == "drums")
            c.instrument = Instrument::Drums; // files from before 0.14.0 never say this
        else if (instrument == "synth")
            c.instrument = Instrument::Synth;
        else
        {
            result.error = "Channel " + std::to_string (i + 1) + " has an unknown instrument.";
            return result;
        }

        c.rootPitch = readInt (o->getProperty ("rootPitch"), 60, 0, 127);
        c.volume = static_cast<float> (readNumber (o->getProperty ("volume"), 0.8, 0.0, 1.5));
        c.pan = static_cast<float> (readNumber (o->getProperty ("pan"), 0.0, -1.0, 1.0));
        c.mute = readBool (o->getProperty ("mute"), false);
        c.solo = readBool (o->getProperty ("solo"), false);
        c.pitch = static_cast<float> (readNumber (o->getProperty ("pitch"), 0.0, -24.0, 24.0));
        c.swing = static_cast<float> (readNumber (o->getProperty ("swing"), 0.0, 0.0, 1.0));
        c.mixerTrack = readInt (o->getProperty ("mixerTrack"), std::min (i, kMaxTracks - 1), 0, kMaxTracks - 1);

        d.kitSpecs.push_back (c.instrument == Instrument::Drums ? kitSpecFromVar (o->getProperty ("kit")) : KitSpec::empty());
        d.channels.push_back (std::move (c));
        d.sampleSources.push_back (o->getProperty ("sample").toString().toStdString());
    }

    // --- mixer tracks (missing ones keep their defaults)
    d.tracks = Project::defaultTracks();
    if (auto* trackList = root->getProperty ("tracks").getArray())
        for (int i = 0; i < std::min (trackList->size(), kMaxTracks); ++i)
        {
            auto* o = (*trackList)[i].getDynamicObject();
            if (o == nullptr)
                continue;

            auto& t = d.tracks[static_cast<size_t> (i)];
            t.name = readName (o->getProperty ("name"), t.name);
            t.volume = static_cast<float> (readNumber (o->getProperty ("volume"), 1.0, 0.0, 1.5));
            t.pan = static_cast<float> (readNumber (o->getProperty ("pan"), 0.0, -1.0, 1.0));
            t.mute = readBool (o->getProperty ("mute"), false);
            t.solo = readBool (o->getProperty ("solo"), false);

            if (auto* slots = o->getProperty ("effects").getArray())
                for (int s = 0; s < std::min (slots->size(), kSlotsPerTrack); ++s)
                    t.slots[static_cast<size_t> (s)] = readSlot ((*slots)[s]);

            // Sends and VCA membership (files from older versions have none)
            if (auto* sendList = o->getProperty ("sends").getArray())
                for (int a = 0; a < std::min (sendList->size(), kAuxBuses); ++a)
                    t.sends[static_cast<size_t> (a)] = static_cast<float> (readNumber ((*sendList)[a], 0.0, 0.0, 1.5));
            t.sendsPreFader = readBool (o->getProperty ("sendsPreFader"), false);
            t.vcaMask = static_cast<std::uint8_t> (readInt (o->getProperty ("vca"), 0, 0, (1 << kVcaGroups) - 1));
        }

    // --- aux buses and VCA groups (older files have none)
    d.console = Console::defaults();
    if (auto* consoleObject = root->getProperty ("console").getDynamicObject())
    {
        if (auto* auxList = consoleObject->getProperty ("aux").getArray())
            for (int a = 0; a < std::min (auxList->size(), kAuxBuses); ++a)
            {
                auto* o = (*auxList)[a].getDynamicObject();
                if (o == nullptr)
                    continue;
                auto& bus = d.console.aux[static_cast<size_t> (a)];
                bus.name = readName (o->getProperty ("name"), bus.name);
                bus.volume = static_cast<float> (readNumber (o->getProperty ("volume"), 1.0, 0.0, 1.5));
                bus.pan = static_cast<float> (readNumber (o->getProperty ("pan"), 0.0, -1.0, 1.0));
                bus.mute = readBool (o->getProperty ("mute"), false);
                if (auto* slots = o->getProperty ("effects").getArray())
                    for (int s = 0; s < std::min (slots->size(), kSlotsPerTrack); ++s)
                        bus.slots[static_cast<size_t> (s)] = readSlot ((*slots)[s]);
            }
        if (auto* vcaList = consoleObject->getProperty ("vca").getArray())
            for (int g = 0; g < std::min (vcaList->size(), kVcaGroups); ++g)
                if (auto* o = (*vcaList)[g].getDynamicObject())
                {
                    d.console.vcaLevel[static_cast<size_t> (g)] = static_cast<float> (readNumber (o->getProperty ("level"), 1.0, 0.0, 1.5));
                    d.console.vcaMute[static_cast<size_t> (g)] = readBool (o->getProperty ("mute"), false);
                }
    }

    // --- master bus effects (older files have none)
    if (auto* master = root->getProperty ("master").getDynamicObject())
        if (auto* slots = master->getProperty ("effects").getArray())
            for (int s = 0; s < std::min (slots->size(), kSlotsPerTrack); ++s)
                d.masterSlots[static_cast<size_t> (s)] = readSlot ((*slots)[s]);

    // --- pattern length (0.15.0; older files: two bars)
    {
        const int bars = readInt (root->getProperty ("patternBars"), 2, 1, 8);
        const bool offered = std::find (std::begin (kPatternBarChoices), std::end (kPatternBarChoices), bars) != std::end (kPatternBarChoices);
        if (! offered)
            r.warnings.push_back ("The pattern length " + std::to_string (bars) + " bars is not offered; two bars are used.");
        d.arrangement.patternTicks = (offered ? bars : 2) * 4 * kTicksPerBeat;
    }

    // --- patterns
    auto* patternList = root->getProperty ("patterns").getArray();
    if (patternList == nullptr || patternList->isEmpty() || patternList->size() > kMaxPatterns)
    {
        result.error = "The project must have between 1 and " + std::to_string (kMaxPatterns) + " patterns.";
        return result;
    }

    const size_t numChannels = d.channels.size();
    int droppedNotes = 0;
    for (int p = 0; p < patternList->size(); ++p)
    {
        auto* o = (*patternList)[p].getDynamicObject();
        if (o == nullptr)
        {
            result.error = "Pattern " + std::to_string (p + 1) + " is damaged.";
            return result;
        }

        Pattern pattern;
        pattern.name = readName (o->getProperty ("name"), "Pattern " + std::to_string (p + 1));
        pattern.notes.resize (numChannels);

        if (auto* perChannel = o->getProperty ("notes").getArray())
            for (int c = 0; c < std::min (perChannel->size(), static_cast<int> (numChannels)); ++c)
                if (auto* notes = (*perChannel)[c].getArray())
                {
                    if (static_cast<size_t> (notes->size()) > kMaxNotesPerChannelPattern)
                    {
                        result.error = "Pattern " + std::to_string (p + 1) + " has too many notes.";
                        return result;
                    }
                    for (const auto& nv : *notes)
                    {
                        Note n;
                        if (readNote (nv, d.arrangement.patternTicks, n))
                            pattern.notes[static_cast<size_t> (c)].push_back (n);
                        else
                            ++droppedNotes;
                    }
                    std::stable_sort (pattern.notes[static_cast<size_t> (c)].begin(), pattern.notes[static_cast<size_t> (c)].end(),
                                      [] (const Note& a, const Note& b)
                                      { return a.startTick != b.startTick ? a.startTick < b.startTick : a.pitch < b.pitch; });
                }

        d.arrangement.patterns.push_back (std::move (pattern));
    }
    if (droppedNotes > 0)
        r.warnings.push_back (std::to_string (droppedNotes) + " note(s) were outside their pattern and were left out.");

    d.currentPattern = readInt (root->getProperty ("currentPattern"), 0, 0, static_cast<int> (d.arrangement.patterns.size()) - 1);

    // --- clips (each must fit, in order; the rest are dropped)
    int droppedClips = 0;
    if (auto* clipList = root->getProperty ("clips").getArray())
        for (const auto& cv : *clipList)
        {
            auto* o = cv.getDynamicObject();
            Clip clip;
            if (o != nullptr)
            {
                clip.pattern = readInt (o->getProperty ("pattern"), -1, -1, 1000);
                clip.track = readInt (o->getProperty ("track"), -1, -1, 1000);
                clip.startUnit = readInt (o->getProperty ("start"), -1, -1, 100000);
                clip.lengthUnits = readInt (o->getProperty ("length"), 0, 0, 100000);
            }
            if (o != nullptr && d.arrangement.canPlace (clip))
                d.arrangement.clips.push_back (clip);
            else
                ++droppedClips;
        }
    if (droppedClips > 0)
        r.warnings.push_back (std::to_string (droppedClips) + " playlist clip(s) could not be placed and were left out.");

    // --- audio clips (older files have none). The audio itself is loaded by the caller from `data->source`.
    int droppedAudio = 0;
    if (auto* audioList = root->getProperty ("audioClips").getArray())
        for (const auto& av : *audioList)
        {
            auto* o = av.getDynamicObject();
            AudioClip clip;
            std::string source;
            if (o != nullptr)
            {
                source = o->getProperty ("source").toString().toStdString();
                clip.name = readName (o->getProperty ("name"), juce::File (juce::String (source)).getFileNameWithoutExtension().toStdString());
                clip.track = readInt (o->getProperty ("track"), -1, -1, 1000);
                clip.startTick = readInt (o->getProperty ("startTick"), -1, -1, 100000000);
                clip.lengthTicks = readInt (o->getProperty ("lengthTicks"), 0, 0, 100000000);
                clip.gain = static_cast<float> (readNumber (o->getProperty ("gain"), 1.0, 0.0, 2.0));
                clip.offsetSeconds = readNumber (o->getProperty ("offsetSeconds"), 0.0, 0.0, 1.0e6);
                if (auto* st = o->getProperty ("stretch").getDynamicObject())
                {
                    clip.stretch.lengthRatio = readNumber (st->getProperty ("lengthRatio"), 1.0, kMinStretchRatio, kMaxStretchRatio);
                    clip.stretch.semitones = readNumber (st->getProperty ("semitones"), 0.0, -kMaxStretchSemitones, kMaxStretchSemitones);
                    clip.stretch.keepTimbre = readBool (st->getProperty ("keepTimbre"), false);
                }
                clip.data = makeMissingSample (source);
            }
            if (o != nullptr && ! source.empty() && d.arrangement.audioClips.size() < static_cast<size_t> (kMaxAudioClips)
                && d.arrangement.canPlaceAudio (clip))
                d.arrangement.audioClips.push_back (std::move (clip));
            else
                ++droppedAudio;
        }
    if (droppedAudio > 0)
        r.warnings.push_back (std::to_string (droppedAudio) + " audio clip(s) could not be placed and were left out.");

    // --- automation clips (older files have none)
    int droppedAutomation = 0;
    if (auto* automationList = root->getProperty ("automation").getArray())
        for (const auto& av : *automationList)
        {
            auto* o = av.getDynamicObject();
            AutomationClip clip;
            bool kindKnown = false;
            if (o != nullptr)
            {
                clip.target.kind = automationKindFromName (o->getProperty ("kind").toString().toStdString(), kindKnown);
                clip.target.index = readInt (o->getProperty ("index"), 0, 0, 1000);
                clip.target.slot = readInt (o->getProperty ("slot"), 0, 0, 1000);
                clip.target.param = readInt (o->getProperty ("param"), 0, 0, 1000);
                clip.lane = readInt (o->getProperty ("lane"), -1, -1, 1000);
                clip.startTick = readInt (o->getProperty ("startTick"), -1, -1, 100000000);
                clip.lengthTicks = readInt (o->getProperty ("lengthTicks"), 0, 0, 100000000);
                if (auto* pointList = o->getProperty ("points").getArray())
                    for (const auto& pv : *pointList)
                    {
                        auto* one = pv.getArray();
                        if (one == nullptr || one->size() < 2 || static_cast<int> (clip.points.size()) >= kMaxAutomationPoints)
                            continue;
                        AutomationPoint pt;
                        pt.tick = readNumber ((*one)[0], 0.0, 0.0, 1.0e8);
                        pt.value = static_cast<float> (readNumber ((*one)[1], 0.0, -1.0e6, 1.0e6));
                        const int shape = one->size() > 2 ? readInt ((*one)[2], 0, 0, 2) : 0;
                        pt.shape = static_cast<AutomationShape> (shape);
                        clip.points.push_back (pt);
                    }
                std::stable_sort (clip.points.begin(), clip.points.end(),
                                  [] (const AutomationPoint& x, const AutomationPoint& y) { return x.tick < y.tick; });
            }
            if (o != nullptr && kindKnown && d.arrangement.automationClips.size() < static_cast<size_t> (kMaxAutomationClips)
                && d.arrangement.canPlaceAutomation (clip))
                d.arrangement.automationClips.push_back (std::move (clip));
            else
                ++droppedAutomation;
        }
    if (droppedAutomation > 0)
        r.warnings.push_back (std::to_string (droppedAutomation) + " automation clip(s) could not be placed and were left out.");

    // --- the playlist's loop region (0.15.0; older files have none)
    if (auto* loop = root->getProperty ("loopRegion").getDynamicObject())
        if (! d.arrangement.setLoop (readInt (loop->getProperty ("start"), -1, -1, 100000000),
                                     readInt (loop->getProperty ("end"), -1, -1, 100000000)))
            r.warnings.push_back ("The loop region did not fit in the playlist and was left out.");

    d.arrangement.ensureChannelCount (numChannels);

    result.document = std::move (d);
    result.warnings = std::move (r.warnings);
    return result;
}

// ------------------------------------------------------------------------- files

std::shared_ptr<const SampleData> makeMissingSample (const std::string& source)
{
    auto s = std::make_shared<SampleData>();
    s->source = source;
    s->name = juce::File (juce::String (source)).getFileNameWithoutExtension().toStdString();
    return s; // no frames: Player ignores a sampler voice without audio
}

ProjectParseResult readProjectFile (const juce::File& file)
{
    ProjectParseResult result;

    if (! file.existsAsFile())
    {
        result.error = "The file " + file.getFileName().toStdString() + " does not exist.";
        return result;
    }
    if (file.getSize() > kMaxProjectFileBytes)
    {
        result.error = file.getFileName().toStdString() + " is too large to be a project file.";
        return result;
    }

    juce::FileInputStream in (file);
    if (in.failedToOpen())
    {
        result.error = "Cannot read " + file.getFileName().toStdString() + ".";
        return result;
    }
    auto parsed = ProjectDocument::fromJson (in.readEntireStreamAsString());
    if (parsed.document.has_value())
        resolveRelativeSources (*parsed.document, file.getParentDirectory());
    return parsed;
}

namespace
{
bool isRelativeSource (const std::string& source)
{
    return ! source.empty() && source.rfind ("builtin:", 0) != 0 && ! juce::File::isAbsolutePath (juce::String (source));
}

std::string resolved (const std::string& source, const juce::File& folder)
{
    return isRelativeSource (source) ? folder.getChildFile (juce::String (source)).getFullPathName().toStdString() : source;
}

// Every place a project refers to a sound file, as a reference that can be read and replaced.
template <typename Visit>
void forEachSource (ProjectDocument& d, Visit&& visit)
{
    for (auto& s : d.sampleSources)
        visit (s);
    for (auto& spec : d.kitSpecs)
        for (auto& pad : spec.pads)
            for (auto& s : pad.sources)
                visit (s);
    for (auto& clip : d.arrangement.audioClips)
        if (clip.data != nullptr)
        {
            auto source = clip.data->source;
            const auto before = source;
            visit (source);
            if (source != before)
                clip.data = makeMissingSample (source); // only the reference is written to a file; the caller loads the audio
        }
}
} // namespace

void resolveRelativeSources (ProjectDocument& d, const juce::File& projectFolder)
{
    forEachSource (d, [&] (std::string& s) { s = resolved (s, projectFolder); });
}

juce::String exportProjectWithSamples (ProjectDocument d, const juce::File& folder, const juce::String& name, juce::StringArray& notes)
{
    if (folder.exists() && ! (folder.isDirectory() && folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0))
        return "The folder " + folder.getFullPathName() + " already exists and is not empty.";
    const auto samples = folder.getChildFile ("Samples");
    if (! samples.createDirectory().wasOk())
        return "Cannot create the folder " + samples.getFullPathName();

    std::map<std::string, std::string> copied; // original path -> "Samples/<name>"
    juce::String error;
    forEachSource (d, [&] (std::string& s)
    {
        if (s.empty() || s.rfind ("builtin:", 0) == 0 || error.isNotEmpty())
            return;
        if (const auto it = copied.find (s); it != copied.end())
        {
            s = it->second;
            return;
        }
        const juce::File original { juce::String (s) };
        if (! original.existsAsFile())
        {
            notes.addIfNotAlreadyThere ("Not found, left as it was: " + original.getFullPathName());
            return;
        }
        auto target = samples.getChildFile (original.getFileName());
        for (int n = 2; target.exists(); ++n)
            target = samples.getChildFile (original.getFileNameWithoutExtension() + "-" + juce::String (n) + original.getFileExtension());
        if (! original.copyFileTo (target))
        {
            error = "Cannot copy " + original.getFullPathName();
            return;
        }
        const auto relative = ("Samples/" + target.getFileName()).toStdString();
        copied[s] = relative;
        s = relative;
    });
    if (error.isNotEmpty())
        return error;
    return writeProjectFile (withProjectExtension (folder.getChildFile (name)), d);
}

juce::String writeProjectFile (const juce::File& file, const ProjectDocument& document)
{
    const auto parent = file.getParentDirectory();
    if (! parent.isDirectory() && ! parent.createDirectory().wasOk())
        return "Cannot create the folder " + parent.getFullPathName();

    const auto text = document.toJson();

    juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithText (text))
        return "Cannot write to " + parent.getFullPathName();
    if (! temp.overwriteTargetFileWithTemporary())
        return "Cannot replace " + file.getFileName();
    return {};
}

bool hasProjectExtension (const juce::File& file)
{
    return file.getFileName().endsWithIgnoreCase (kProjectExtension)
        || file.getFileName().endsWithIgnoreCase (kLegacyProjectExtension);
}

juce::File withProjectExtension (const juce::File& file)
{
    // Not withFileExtension(): "My.Song" must become "My.Song.slp", not "My.slp".
    return hasProjectExtension (file)
               ? file
               : file.getParentDirectory().getChildFile (file.getFileName() + kProjectExtension);
}

juce::File projectFileForSaveAs (const juce::File& file)
{
    if (file.getFileName().endsWithIgnoreCase (kLegacyProjectExtension))
        return file.getParentDirectory().getChildFile (file.getFileName().dropLastCharacters (juce::String (kLegacyProjectExtension).length())
                                                       + kProjectExtension);
    return file;
}

juce::File automaticProjectFile (const juce::File& folder, juce::Time now)
{
    const auto stamp = now.formatted ("%Y-%m-%d %H-%M-%S");
    auto file = folder.getChildFile ("Untitled " + stamp + kProjectExtension);
    for (int n = 2; file.exists(); ++n)
        file = folder.getChildFile ("Untitled " + stamp + " (" + juce::String (n) + ")" + kProjectExtension);
    return file;
}
} // namespace sls
