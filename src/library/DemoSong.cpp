#include "library/DemoSong.h"

#include <algorithm>
#include <cmath>

#include "audio/BuiltinSamples.h"
#include "audio/SoundLibrary.h"

namespace sls
{
namespace
{
constexpr int Q = kTicksPerBeat;  // a quarter
constexpr int E = Q / 2;          // an eighth
constexpr int S = Q / 4;          // a sixteenth
constexpr int BAR = 4 * Q;
constexpr int PATTERN_BARS = 4;

// The channels, in the order of the Channel Rack; each plays through its own mixer track (channel + 1; track 1 is for audio).
enum Ch { Drums, Bass, Piano, Marimba, Lead, Pad, Growl, Bells, Choir, Strings, Arp, kChannels };
// The patterns, in the order of the pattern list.
enum Pat { PPiano, PBeat, PBuild, PBass, PMarimba, PLead, PPad, PGrowl, PBells, PChoir, PArp, POutro, kPatterns };

constexpr int kAudioTrack = 0; // mixer track 1 ("Audio FX"): the playlist's first track plays through it
int trackOf (int channel) { return channel + 1; }

const SynthPatch& factory (const char* name)
{
    for (const auto& p : synthFactoryPatches())
        if (p.name == name)
            return p.patch;
    return synthFactoryPatches().front().patch;
}

// The four chords of every pattern, one per bar: Am, F, C, G.
constexpr int kRoots[4] = { 33, 29, 36, 31 }; // A1, F1, C2, G1 (the bass plays at its real pitch)
const std::vector<std::vector<int>> kPianoChords { { 57, 60, 64, 67, 71 }, { 53, 57, 60, 64 }, { 55, 60, 64, 67 }, { 55, 59, 62, 67 } };
const std::vector<std::vector<int>> kPadChords { { 57, 60, 64, 69 }, { 53, 57, 60, 65 }, { 55, 60, 64, 67 }, { 55, 59, 62, 67 } };
const std::vector<std::vector<int>> kHighTones { { 69, 72, 76, 81 }, { 65, 69, 72, 77 }, { 67, 72, 76, 79 }, { 67, 71, 74, 79 } };

struct Writer
{
    ProjectDocument& d;
    void add (int pattern, int channel, int pitch, int start, int length, float velocity)
    {
        d.arrangement.patterns[static_cast<size_t> (pattern)].notes[static_cast<size_t> (channel)].push_back (
            { pitch, start, std::max (1, length), std::clamp (velocity, 0.05f, 1.0f) });
    }
};

void writeDrums (Writer& w)
{
    constexpr int kick = 36, rim = 37, snare = 38, clap = 39, lowTom = 41, closedHat = 42, midTom = 45, openHat = 46, highTom = 48,
                  crash = 49;
    // The groove: four on the floor, claps on 2 and 4, sixteenth hats with the offbeats on top, a ghost snare and an open hat.
    for (int b = 0; b < PATTERN_BARS; ++b)
    {
        const int at = b * BAR;
        for (int beat = 0; beat < 4; ++beat)
            w.add (PBeat, Drums, kick, at + beat * Q, S, beat == 0 ? 1.0f : 0.92f);
        w.add (PBeat, Drums, clap, at + Q, S, 0.88f);
        w.add (PBeat, Drums, clap, at + 3 * Q, S, 0.92f);
        for (int s = 0; s < 16; ++s)
        {
            if (s == 14 && b % 2 == 1)
                w.add (PBeat, Drums, openHat, at + s * S, E, 0.7f);
            else if (s != 15 || b % 2 == 0)
                w.add (PBeat, Drums, closedHat, at + s * S, S, s % 4 == 2 ? 0.8f : s % 2 == 0 ? 0.55f : 0.35f);
        }
        w.add (PBeat, Drums, snare, at + 2 * Q + 3 * S, S, 0.28f);
        w.add (PBeat, Drums, rim, at + 3 * Q + 3 * S, S, 0.45f);
    }
    w.add (PBeat, Drums, crash, 0, 2 * Q, 0.85f);
    // The last beat of the groove: a short fill on the toms.
    w.add (PBeat, Drums, highTom, 3 * BAR + 3 * Q, S, 0.7f);
    w.add (PBeat, Drums, midTom, 3 * BAR + 3 * Q + S, S, 0.7f);
    w.add (PBeat, Drums, lowTom, 3 * BAR + 3 * Q + 2 * S, E, 0.8f);

    // The build: kick in quarters, then eighths, a snare roll growing to sixteenths and getting louder.
    for (int b = 0; b < PATTERN_BARS; ++b)
    {
        const int at = b * BAR;
        const int kickStep = b < 2 ? Q : E;
        for (int t = 0; t < BAR; t += kickStep)
            w.add (PBuild, Drums, kick, at + t, S, 0.85f + 0.04f * static_cast<float> (b));
        const int snareStep = b == 0 ? Q : b == 1 ? E : S;
        for (int t = (b == 0 ? Q : 0); t < BAR; t += snareStep)
        {
            const float rise = static_cast<float> (at + t) / static_cast<float> (PATTERN_BARS * BAR);
            w.add (PBuild, Drums, snare, at + t, S, 0.25f + 0.7f * rise);
        }
        for (int e = 0; e < 8; ++e)
            w.add (PBuild, Drums, closedHat, at + e * E, S, e % 2 == 1 ? 0.6f : 0.35f);
    }
}

void writeHarmony (Writer& w)
{
    for (int b = 0; b < PATTERN_BARS; ++b)
    {
        const int at = b * BAR;
        const auto bi = static_cast<size_t> (b);
        // Piano: a short-long syncopated rhythm.
        for (const int p : kPianoChords[bi])
        {
            w.add (PPiano, Piano, p, at, 3 * E - 20, 0.7f);
            w.add (PPiano, Piano, p, at + 3 * E, E - 20, 0.52f);
            w.add (PPiano, Piano, p, at + 5 * E, 3 * E - 20, 0.6f);
        }
        // SunBurst pad: whole bars.
        for (const int p : kPadChords[bi])
            w.add (PPad, Pad, p, at, BAR - 10, 0.7f);
        // Bass: root, octave, fifth, a walk into the next bar.
        const int r = kRoots[bi];
        const int bassLine[][4] = { { r, 0, 420, 100 }, { r, 3 * E, 200, 72 }, { r + 12, 4 * E, 200, 82 }, { r + 7, 5 * E, 200, 78 },
                                    { r, 6 * E, 200, 88 }, { b == 3 ? r + 2 : r + 12, 7 * E, 200, 80 } };
        for (const auto& n : bassLine)
            w.add (PBass, Bass, n[0], at + n[1], n[2], static_cast<float> (n[3]) / 100.0f);
        // Marimba (the physical model): eighths over the chord, high.
        constexpr int order[8] = { 0, 2, 1, 3, 2, 1, 3, 2 };
        for (int e = 0; e < 8; ++e)
            w.add (PMarimba, Marimba, kHighTones[bi][static_cast<size_t> (order[e])], at + e * E, E - 30, e % 2 == 0 ? 0.72f : 0.5f);
        // HexForge growl: driving eighths, the octave on every fourth.
        const int g = r + 12;
        for (int e = 0; e < 8; ++e)
            w.add (PGrowl, Growl, e % 4 == 3 ? g + 12 : g, at + e * E, E - 30, e % 2 == 0 ? 0.95f : 0.75f);
        // FM bells: the top of the chord on the first beat, an octave lower on the third.
        const int top = kHighTones[bi][2];
        w.add (PBells, Bells, top, at, 2 * Q, 0.5f);
        w.add (PBells, Bells, top - 12, at + 2 * Q + E, Q, 0.35f);
        // Breakdown: the additive choir holds two notes, the subtractive strings the chord below.
        w.add (PChoir, Choir, kPadChords[bi][0], at, BAR - 10, 0.6f);
        w.add (PChoir, Choir, kPadChords[bi][2], at, BAR - 10, 0.55f);
        for (const int p : kPianoChords[bi])
            if (p < 66)
                w.add (PChoir, Strings, p - 12, at, BAR - 10, 0.55f);
        // HexForge arp: sixteenths up and down the chord.
        constexpr int arp[6] = { 0, 1, 2, 3, 2, 1 };
        for (int s = 0; s < 16; ++s)
            w.add (PArp, Arp, kHighTones[bi][static_cast<size_t> (arp[s % 6])], at + s * S, S - 15,
                   s % 4 == 0 ? 0.8f : 0.6f);
    }

    // SunBurst lead: the tune of the chorus (eighths: start, length).
    const int tune[][3] = { { 76, 0, 3 },  { 74, 3, 1 },  { 72, 4, 2 },  { 76, 6, 2 },  { 81, 8, 3 },  { 79, 11, 1 }, { 77, 12, 2 },
                            { 76, 14, 2 }, { 79, 16, 3 }, { 76, 19, 1 }, { 72, 20, 2 }, { 74, 22, 2 }, { 71, 24, 2 }, { 74, 26, 2 },
                            { 76, 28, 4 } };
    for (const auto& n : tune)
        w.add (PLead, Lead, n[0], n[1] * E, n[2] * E - 25, n[1] % 4 == 0 ? 0.9f : 0.75f);

    // Outro: Am - F - G - Am, held, slowing down (see the tempo markers).
    const std::vector<std::vector<int>> outro { kPianoChords[0], kPianoChords[1], kPianoChords[3], { 45, 57, 60, 64, 69, 71 } };
    for (int b = 0; b < PATTERN_BARS; ++b)
    {
        const auto bi = static_cast<size_t> (b);
        for (const int p : outro[bi])
            w.add (POutro, Piano, p, b * BAR, b == 3 ? BAR - 10 : BAR - 30, b == 3 ? 0.62f : 0.55f);
        for (const int p : kPadChords[b == 2 ? 3 : b == 3 ? 0 : bi])
            w.add (POutro, Pad, p, b * BAR, BAR - 10, 0.6f);
    }
    w.add (POutro, Bass, 33, 3 * BAR, BAR - 10, 0.85f);
    w.add (POutro, Bells, 76, 3 * BAR, 2 * Q, 0.45f);
}

AutomationPoint pt (double tick, float value, AutomationShape shape = AutomationShape::Smooth) { return { tick, value, shape }; }

AutomationClip automation (AutomationKind kind, int index, int slot, int param, int lane, int startBar, int bars,
                           std::vector<AutomationPoint> points)
{
    AutomationClip c;
    c.target = { kind, index, slot, param };
    c.lane = lane;
    c.startTick = startBar * BAR;
    c.lengthTicks = bars * BAR;
    c.points = std::move (points);
    return c;
}

EffectSlot effect (EffectType type, std::initializer_list<std::pair<int, float>> values)
{
    auto slot = EffectSlot::make (type);
    for (const auto& [param, value] : values)
        slot.params[static_cast<size_t> (param)] = value;
    return slot;
}

// The length of a built-in sound in ticks at the song's tempo (one bar if it cannot be read).
int ticksOf (const std::string& id)
{
    const auto data = makeBuiltinSample (id);
    if (data == nullptr || data->sampleRate <= 0.0)
        return BAR;
    const double seconds = data->numFrames() / data->sampleRate;
    return std::max (1, static_cast<int> (seconds * kDemoSongBpm / 60.0 * kTicksPerBeat));
}
} // namespace

DemoSong makeDemoSong()
{
    DemoSong demo;
    auto& d = demo.document;

    // --- Channels -------------------------------------------------------------------------------------------------------------
    d.channels.resize (kChannels);
    d.sampleSources.assign (kChannels, std::string());
    d.kitSpecs.assign (kChannels, KitSpec::empty());
    d.programSpecs.assign (kChannels, ProgramSpec {});
    const auto setUp = [&d] (int ch, const char* name, Instrument instrument, float volume, float pan = 0.0f)
    {
        auto& c = d.channels[static_cast<size_t> (ch)];
        c.name = name;
        c.instrument = instrument;
        c.volume = volume;
        c.pan = pan;
        c.mixerTrack = trackOf (ch);
        return &c;
    };
    setUp (Drums, "Drums", Instrument::Drums, 0.82f);
    d.kitSpecs[static_cast<size_t> (Drums)] = builtinKitSpec ("gen:punch");
    for (const auto& [ch, id] : { std::pair<int, const char*> { Bass, "electric-bass" }, { Piano, "grand-piano" } })
    {
        auto* c = setUp (ch, ch == Bass ? "Bass" : "Piano", Instrument::Sampler, ch == Bass ? 1.25f : 1.1f);
        c->sampler = libraryProgramSettings (id);
        d.programSpecs[static_cast<size_t> (ch)].kind = ProgramSpec::Kind::Builtin;
        d.programSpecs[static_cast<size_t> (ch)].file = std::string (kProgramSourcePrefix) + id;
    }
    setUp (Marimba, "Marimba", Instrument::Synth, 1.4f)->synth = factory ("Marimba");                  // physical model
    setUp (Bells, "FM Bells", Instrument::Synth, 0.32f, 0.15f)->synth = factory ("FM Bell");           // FM
    setUp (Choir, "Choir", Instrument::Synth, 0.5f, -0.1f)->synth = factory ("Vowel Choir");          // additive
    setUp (Strings, "Strings", Instrument::Synth, 0.38f, 0.1f)->synth = factory ("PWM Strings");       // subtractive
    setUp (Lead, "SunBurst Lead", Instrument::Plugin, 0.7f);
    setUp (Pad, "SunBurst Pad", Instrument::Plugin, 0.23f);
    setUp (Growl, "HexForge Bass", Instrument::Plugin, 0.55f);
    setUp (Arp, "HexForge Arp", Instrument::Plugin, 0.84f, -0.15f);
    demo.pluginChannels = { { Lead, "SunBurst", "Sun Lead", factory ("Saw Lead") },
                            { Pad, "SunBurst", "Warm Pad", factory ("Soft Pad") },
                            { Growl, "HexForge", "Chrome Bass", factory ("FM Growl") },
                            { Arp, "HexForge", "Neon Pluck", factory ("Pluck") } };

    // --- Patterns ---------------------------------------------------------------------------------------------------------------
    auto& a = d.arrangement;
    a.patterns.resize (kPatterns);
    const char* patternNames[kPatterns] = { "Piano", "Beat", "Build", "Bass", "Marimba", "Lead", "Pad", "Growl", "Bells", "Choir", "Arp", "Outro" };
    for (int p = 0; p < kPatterns; ++p)
    {
        a.patterns[static_cast<size_t> (p)].name = patternNames[p];
        a.patterns[static_cast<size_t> (p)].lengthTicks = PATTERN_BARS * BAR;
    }
    a.ensureChannelCount (kChannels);
    Writer w { d };
    writeDrums (w);
    writeHarmony (w);

    // --- Playlist: 44 bars --------------------------------------------------------------------------------------------------------
    a.resetTracks();
    a.playlistTracks[0].name = "Audio";
    const auto place = [&a] (int pattern, int startBar, int bars)
    {
        a.clips.push_back ({ pattern, a.dedicatedTrack (pattern), startBar * BAR, bars * BAR, 0 });
    };
    // Intro 1-4
    place (PPiano, 0, 4);
    place (PPad, 0, 4);
    // Verse 5-12
    for (const int p : { PBeat, PBass, PPiano, PMarimba })
        place (p, 4, 8);
    place (PBells, 8, 4);
    // Build 13-16
    for (const int p : { PBuild, PBass, PPad, PPiano })
        place (p, 12, 4);
    // Chorus 17-24
    for (const int p : { PBeat, PGrowl, PLead, PPad, PBells })
        place (p, 16, 8);
    // Breakdown 25-28
    for (const int p : { PChoir, PPiano, PPad })
        place (p, 24, 4);
    // Build 29-32
    for (const int p : { PBuild, PMarimba, PBass, PPad })
        place (p, 28, 4);
    // Last chorus 33-40
    for (const int p : { PBeat, PGrowl, PLead, PPad, PArp, PBells })
        place (p, 32, 8);
    place (PMarimba, 36, 4);
    // Outro 41-44
    place (POutro, 40, 4);

    // Audio clips on the first track: the start-up sound, then a cymbal played backwards (a riser) before each chorus, the second
    // one a minor third lower (time-stretched without changing its length).
    {
        AudioClip intro;
        intro.name = "Intro sound";
        intro.data = makeMissingSample (std::string ("builtin:") + kStartupSampleId); // read when the song is opened
        intro.track = kAudioTrack;
        intro.startTick = 0;
        intro.lengthTicks = std::min (ticksOf (kStartupSampleId), 4 * BAR);
        intro.gain = 0.55f;
        a.audioClips.push_back (intro);

        const std::string crash = "lib:kits/studio/crashl-4";
        const int crashTicks = std::min (ticksOf (crash), 2 * BAR);
        for (const int bar : { 16, 32 })
        {
            AudioClip riser;
            riser.name = bar == 16 ? "Reverse cymbal" : "Reverse cymbal (pitched)";
            riser.data = makeMissingSample ("builtin:" + crash);
            riser.track = kAudioTrack;
            riser.lengthTicks = crashTicks;
            riser.startTick = bar * BAR - crashTicks;
            riser.reverse = true;
            riser.gain = 0.7f;
            riser.fadeInSeconds = 0.8;
            riser.pan = bar == 16 ? -0.2f : 0.2f;
            if (bar == 32)
                riser.stretch.semitones = -3.0;
            a.audioClips.push_back (riser);
        }
    }

    // Tempo: 110 BPM, slowing down over the last two bars.
    a.tempoMarkers = { { 42 * BAR, 104.0 }, { 42 * BAR + 2 * Q, 98.0 }, { 43 * BAR, 90.0 }, { 43 * BAR + 2 * Q, 80.0 } };

    // --- Mixer -----------------------------------------------------------------------------------------------------------------------
    d.tracks = Project::defaultTracks();
    d.tracks.resize (16);
    for (size_t t = 0; t < d.tracks.size(); ++t)
        d.tracks[t].name = "Track " + std::to_string (t + 1);
    auto& tr = d.tracks;
    tr[kAudioTrack].name = "Audio FX";
    tr[kAudioTrack].slots[0] = effect (EffectType::ParametricEq, { { 0, 200.0f }, { 1, -6.0f } });
    tr[kAudioTrack].sends = { 0.45f, 0.0f };
    const char* trackNames[kChannels] = { "Drums", "Bass", "Piano", "Marimba", "Lead", "Pad", "Growl", "Bells", "Choir", "Strings", "Arp" };
    for (int ch = 0; ch < kChannels; ++ch)
        tr[static_cast<size_t> (trackOf (ch))].name = trackNames[ch];

    auto& drums = tr[static_cast<size_t> (trackOf (Drums))];
    drums.slots[0] = effect (EffectType::Transient, { { 0, 4.0f }, { 1, -2.0f } });
    drums.slots[1] = effect (EffectType::Compressor, { { 0, -16.0f }, { 1, 3.0f }, { 2, 8.0f }, { 3, 120.0f }, { 4, 2.0f } });
    // Off (mix 0) until its automation opens it for a tape stop at the end of each build.
    drums.slots[2] = effect (EffectType::TimeShaper, { { 0, 3.0f }, { 1, 0.0f }, { 2, 1.0f }, { 5, 0.0f } });
    drums.sends = { 0.08f, 0.0f };
    drums.vcaMask = 0b10;

    auto& bass = tr[static_cast<size_t> (trackOf (Bass))];
    bass.slots[0] = effect (EffectType::ParametricEq, { { 0, 80.0f }, { 1, 3.0f }, { 2, 400.0f }, { 3, -3.0f } });
    // Ducks under the drums (sidechain from track 2).
    bass.slots[1] = effect (EffectType::Compressor, { { 0, -12.0f }, { 1, 3.0f }, { 2, 2.0f }, { 3, 140.0f }, { 5, static_cast<float> (trackOf (Drums) + 1) } });
    bass.vcaMask = 0b10;

    auto& piano = tr[static_cast<size_t> (trackOf (Piano))];
    piano.slots[0] = effect (EffectType::ParametricEq, { { 0, 120.0f }, { 1, -4.0f }, { 5, 9000.0f }, { 6, 2.0f } });
    piano.slots[1] = effect (EffectType::Chorus, { { 0, 0.4f }, { 1, 2.0f }, { 6, 0.18f } });
    piano.sends = { 0.25f, 0.0f };

    auto& marimba = tr[static_cast<size_t> (trackOf (Marimba))];
    marimba.slots[0] = effect (EffectType::Phaser, { { 0, 0.3f }, { 6, 0.3f } });
    marimba.sends = { 0.2f, 0.25f };

    auto& lead = tr[static_cast<size_t> (trackOf (Lead))];
    lead.slots[0] = effect (EffectType::Delay, { { 0, 409.0f }, { 1, 0.35f }, { 3, 0.22f }, { 4, 1.0f } }); // dotted eighth, ping-pong
    lead.sends = { 0.3f, 0.0f };
    lead.vcaMask = 0b01;

    auto& pad = tr[static_cast<size_t> (trackOf (Pad))];
    pad.slots[0] = effect (EffectType::Imager, { { 0, 130.0f }, { 3, 150.0f } });
    // Pumps with the drums (sidechain from track 2), the classic synthwave pad.
    pad.slots[1] = effect (EffectType::Compressor, { { 0, -30.0f }, { 1, 6.0f }, { 2, 1.0f }, { 3, 180.0f }, { 5, static_cast<float> (trackOf (Drums) + 1) } });
    pad.sends = { 0.35f, 0.0f };
    pad.vcaMask = 0b01;

    auto& growl = tr[static_cast<size_t> (trackOf (Growl))];
    growl.slots[0] = effect (EffectType::Overdrive, { { 0, 0.3f }, { 5, 0.35f }, { 6, -4.0f } });
    growl.slots[1] = effect (EffectType::ParametricEq, { { 0, 60.0f }, { 1, 2.0f }, { 5, 7000.0f }, { 6, -3.0f } });
    growl.vcaMask = 0b01;

    auto& bells = tr[static_cast<size_t> (trackOf (Bells))];
    bells.sends = { 0.35f, 0.4f };

    tr[static_cast<size_t> (trackOf (Choir))].sends = { 0.5f, 0.0f };
    tr[static_cast<size_t> (trackOf (Strings))].sends = { 0.45f, 0.0f };
    tr[static_cast<size_t> (trackOf (Strings))].slots[0] = effect (EffectType::Filter, { { 0, 0.0f }, { 1, 5000.0f } });

    auto& arpTrack = tr[static_cast<size_t> (trackOf (Arp))];
    arpTrack.slots[0] = effect (EffectType::Delay, { { 0, 273.0f }, { 1, 0.3f }, { 3, 0.2f }, { 4, 1.0f } });
    arpTrack.sends = { 0.15f, 0.3f };
    arpTrack.vcaMask = 0b01;

    // Aux A: a hall; aux B: a ping-pong delay. Both fully wet.
    d.console = Console::defaults();
    d.console.aux[0].name = "Reverb";
    d.console.aux[0].slots[0] = effect (EffectType::Reverb, { { 0, 0.82f }, { 1, 0.45f }, { 3, 1.0f }, { 4, 0.0f }, { 5, 30.0f } });
    d.console.aux[0].slots[1] = effect (EffectType::ParametricEq, { { 0, 250.0f }, { 1, -6.0f } });
    d.console.aux[0].volume = 0.8f;
    d.console.aux[1].name = "Delay";
    d.console.aux[1].slots[0] = effect (EffectType::Delay, { { 0, 545.0f }, { 1, 0.45f }, { 2, 0.5f }, { 3, 1.0f }, { 4, 1.0f } });
    d.console.aux[1].volume = 0.7f;

    // Master: a low-pass (automated in the breakdown), a gentle EQ and the maximizer.
    d.masterSlots[0] = effect (EffectType::Filter, { { 0, 0.0f }, { 1, 20000.0f }, { 2, 0.15f } });
    d.masterSlots[1] = effect (EffectType::ParametricEq, { { 0, 70.0f }, { 1, 1.5f }, { 5, 11000.0f }, { 6, 1.5f } });
    d.masterSlots[2] = effect (EffectType::Maximizer, { { 0, 3.0f }, { 1, -1.0f }, { 3, 0.3f } });

    // --- Automation ---------------------------------------------------------------------------------------------------------------------
    auto& clips = a.automationClips;
    const auto pluginClip = [&] (int channel, const char* parameter, int lane, int startBar, int bars, std::vector<AutomationPoint> points)
    {
        demo.pluginAutomation.push_back ({ clips.size(), parameter });
        clips.push_back (automation (AutomationKind::ChannelPlugin, channel, 0, 0, lane, startBar, bars, std::move (points)));
    };
    // Lane 1: SunBurst pad's filter opens in the intro and again in the breakdown.
    pluginClip (Pad, "Cutoff", 0, 0, 4, { pt (0, 0.28f), pt (4 * BAR, 0.72f) });
    pluginClip (Pad, "Cutoff", 0, 24, 4, { pt (0, 0.32f), pt (4 * BAR, 0.72f) });
    // Lane 2: the marimba moves left and right in the verse; HexForge's bass filter wobbles in the choruses.
    {
        std::vector<AutomationPoint> pan;
        for (int half = 0; half <= 8; ++half)
            pan.push_back (pt (half * BAR, half % 2 == 0 ? -0.45f : 0.45f));
        clips.push_back (automation (AutomationKind::ChannelPan, Marimba, 0, 0, 1, 4, 8, pan));
        std::vector<AutomationPoint> wobble;
        for (int h = 0; h <= 16; ++h)
            wobble.push_back (pt (h * 2 * Q, h % 2 == 0 ? 0.46f : 0.64f));
        for (const int bar : { 16, 32 })
            pluginClip (Growl, "Cutoff", 1, bar, 8, wobble);
    }
    // Lane 3: the master low-pass sweeps open through the breakdown (Hz).
    clips.push_back (automation (AutomationKind::MasterEffect, 0, 0, 1, 2, 24, 4, { pt (0, 350.0f), pt (3 * BAR, 4000.0f), pt (4 * BAR, 20000.0f) }));
    // ... and the delay bus comes up through the last chorus.
    clips.push_back (automation (AutomationKind::AuxVolume, 0, 1, 0, 2, 32, 8, { pt (0, 0.5f), pt (8 * BAR, 0.95f) }));
    // Lane 4: the piano's reverb send grows in the breakdown; a tape stop on the drums at the end of each build; the synths' VCA
    // fades out in the outro.
    clips.push_back (automation (AutomationKind::TrackSend, trackOf (Piano), 0, 0, 3, 24, 5,
                                 { pt (0, 0.25f), pt (4 * BAR, 0.8f), pt (4 * BAR + Q, 0.25f, AutomationShape::Hold) }));
    for (const int bar : { 15, 31 })
        clips.push_back (automation (AutomationKind::TrackEffect, trackOf (Drums), 2, 5, 3, bar, 1,
                                     { pt (0, 0.0f, AutomationShape::Hold), pt (2 * Q, 100.0f, AutomationShape::Hold), pt (BAR - 1, 0.0f, AutomationShape::Hold) }));
    clips.push_back (automation (AutomationKind::VcaLevel, 0, 0, 0, 3, 40, 4, { pt (0, 1.0f), pt (2 * BAR, 0.9f), pt (4 * BAR, 0.0f) }));

    // --- The rest ----------------------------------------------------------------------------------------------------------------------
    d.bpm = kDemoSongBpm;
    d.masterVolume = 0.8f;
    d.songMode = true;
    d.currentPattern = PBeat;
    return demo;
}

std::vector<std::string> attachDemoPlugins (DemoSong& demo, const std::function<std::shared_ptr<HostedPlugin> (const std::string&)>& make)
{
    std::vector<std::string> problems;
    auto& d = demo.document;
    std::vector<int> missing;
    for (const auto& use : demo.pluginChannels)
    {
        auto& channel = d.channels[static_cast<size_t> (use.channel)];
        auto plugin = make != nullptr ? make (use.plugin) : nullptr;
        if (plugin == nullptr || ! plugin->isLoaded())
        {
            problems.push_back (use.plugin + " is not available: \"" + channel.name + "\" plays a built-in synth");
            channel.instrument = Instrument::Synth;
            channel.synth = use.fallback;
            missing.push_back (use.channel);
            continue;
        }
        if (! plugin->selectProgram (use.preset))
            problems.push_back (use.plugin + " has no preset \"" + use.preset + "\"");
        channel.plugin = std::move (plugin);
    }

    // Plugin automation: find each parameter by its name; without the plugin (or the parameter) the clip goes.
    std::vector<size_t> drop;
    for (const auto& pa : demo.pluginAutomation)
    {
        auto& clip = d.arrangement.automationClips[pa.clip];
        const auto& channel = d.channels[static_cast<size_t> (clip.target.index)];
        int found = -1;
        if (channel.instrument == Instrument::Plugin && channel.plugin != nullptr)
            for (int i = 0; i < channel.plugin->parameterCount() && found < 0; ++i)
                if (channel.plugin->parameterName (i) == pa.parameter)
                    found = i;
        if (found < 0)
        {
            if (std::find (missing.begin(), missing.end(), clip.target.index) == missing.end())
                problems.push_back ("\"" + channel.name + "\" has no parameter \"" + pa.parameter + "\"");
            drop.push_back (pa.clip);
        }
        else
            clip.target.param = found;
    }
    std::sort (drop.rbegin(), drop.rend());
    for (const auto index : drop)
        d.arrangement.automationClips.erase (d.arrangement.automationClips.begin() + static_cast<std::ptrdiff_t> (index));
    demo.pluginAutomation.clear(); // the indices no longer hold after removals
    return problems;
}
} // namespace sls
