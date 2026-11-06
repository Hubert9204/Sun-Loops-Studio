#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/Automation.h"
#include "audio/DrumKit.h"
#include "audio/Effects.h"
#include "audio/NoteSequence.h"
#include "audio/SampleData.h"
#include "audio/SamplerProgram.h"
#include "audio/SynthPatch.h"

namespace sls
{
constexpr int kMaxChannels = 32;
constexpr int kMaxTracks = 8; // mixer tracks; every channel is routed to exactly one of them
constexpr int kAuxBuses = 2;  // aux buses ("returns"): tracks send to them, they have their own effects and fader
constexpr int kVcaGroups = 2; // VCA groups: one fader that scales the faders of its member tracks

enum class Instrument { Synth, Sampler, Drums, Plugin };

struct Channel
{
    std::string name = "Channel";
    Instrument instrument = Instrument::Synth;
    std::vector<Note> notes;
    std::shared_ptr<const SampleData> sample; // Sampler only (when there is no program). Plays one-shot unless `sampler` says otherwise.
    std::shared_ptr<const SamplerProgram> program; // Sampler only: a multi-sample program (a SoundFont preset, slices); used instead of `sample`
    SamplerSettings sampler;                  // Sampler only: envelope, loop, filter (0.16.0)
    SynthPatch synth;                         // Synth only: the engine (Classic = the original sound) and the subtractive synth's settings (0.18.0)
    std::shared_ptr<HostedPlugin> plugin;     // Plugin only: a third-party instrument (0.17.0); it gets the notes as MIDI
    std::shared_ptr<const DrumKit> kit;       // Drums only: the 16 pads (see DrumKit.h). Never null for a Drums channel after Project::make().
    int rootPitch = 60;                       // Sampler: pitch at which the sample plays unchanged
    float volume = 0.8f;                      // 0..1.5
    float pan = 0.0f;                         // -1 (left) .. +1 (right)
    bool mute = false;
    bool solo = false;
    float pitch = 0.0f;                       // channel tuning in semitones (-24..+24), applied to every note
    float swing = 0.0f;                       // 0..1: delays notes that fall on the second half of every 8th
    int mixerTrack = 0;                       // which mixer track this channel plays through (0-based)
};

// One vertical strip of the mixer: its own fader, balance, mute/solo and a chain of insert effects.
// The channels routed to it are summed, run through the effects (pre-fader), then through the fader.
struct MixerTrack
{
    std::string name = "Track";
    float volume = 1.0f; // 0..1.5, 1 = unity
    float pan = 0.0f;    // -1..+1, balance: the louder side stays at unity
    bool mute = false;
    bool solo = false;
    std::array<EffectSlot, kSlotsPerTrack> slots {};

    // Only used by the tracks (an aux bus ignores them).
    std::array<float, kAuxBuses> sends {}; // level sent to each aux bus, 0 (off) .. 1.5
    bool sendsPreFader = false;            // sends take the signal before the fader, balance and VCA (they still follow mute/solo)
    std::uint8_t vcaMask = 0;              // bit n: this track is a member of VCA group n
};

// The part of the mixer that is not a track: the aux buses (a MixerTrack each: name, fader, balance, mute and effect chain),
// and the VCA groups (a level and a mute switch each).
struct Console
{
    std::array<MixerTrack, kAuxBuses> aux;
    std::array<float, kVcaGroups> vcaLevel { 1.0f, 1.0f };
    std::array<bool, kVcaGroups> vcaMute {};

    static Console defaults()
    {
        Console c;
        for (size_t a = 0; a < c.aux.size(); ++a)
            c.aux[a].name = std::string ("Aux ") + static_cast<char> ('A' + static_cast<int> (a));
        return c;
    }
};

// A recording or imported audio file placed on the song timeline. It plays once from `startTick`, through mixer
// track `mixerTrack`, and is cut at `endTick` (so a clip that is shorter than its audio trims it).
struct AudioEvent
{
    std::shared_ptr<const SampleData> data;
    int startTick = 0;
    int endTick = 0;
    int mixerTrack = 0;
    float gain = 1.0f; // 0..2
    double offsetSeconds = 0.0; // where in the audio the clip starts playing (a clip cut from a longer one starts later)
};

// Immutable snapshot of everything the audio thread needs.
// Build it with Project::make() on the message thread and hand it to a SnapshotExchange.
struct Project
{
    std::vector<Channel> channels;
    std::vector<MixerTrack> tracks; // always kMaxTracks entries after make()
    std::array<EffectSlot, kSlotsPerTrack> masterSlots {}; // insert effects on the master bus (after the sum of all tracks)
    Console console = Console::defaults(); // aux buses and VCA groups
    std::vector<AudioEvent> audioEvents; // song mode only; sorted by startTick, validated by make()
    std::vector<AutomationEvent> automation; // song mode only; sorted by parameter and start, validated by make()
    int lengthTicks = 8 * kTicksPerBeat;
    // The playlist's loop region (song mode, live playback only): when the transport reaches loopEndTick it goes back to
    // loopStartTick, and the end of the song goes back to it too. -1 / -1 when there is none (always so in exports).
    int loopStartTick = -1;
    int loopEndTick = -1;
    std::uint64_t revision = 0; // assigned by SnapshotExchange

    static std::vector<MixerTrack> defaultTracks()
    {
        std::vector<MixerTrack> t (static_cast<size_t> (kMaxTracks));
        for (size_t i = 0; i < t.size(); ++i)
            t[i].name = "Track " + std::to_string (i + 1);
        return t;
    }

    // Swing: notes on the second half of every 8th (the off-beat 16ths) are delayed. Notes that would be
    // pushed past the end of the loop stay where they are.
    static void applySwing (std::vector<Note>& notes, float swing, int loopLengthTicks)
    {
        const int shift = static_cast<int> (std::lround (swing * kMaxSwingShiftTicks));
        if (shift <= 0)
            return;

        for (auto& n : notes)
            if (n.startTick >= 0 && (n.startTick / kSixteenthTicks) % 2 == 1 && n.startTick + shift < loopLengthTicks)
                n.startTick += shift;
    }

    static Project make (std::vector<Channel> input, int loopLengthTicks, std::vector<MixerTrack> tracks = {},
                         std::array<EffectSlot, kSlotsPerTrack> masterSlots = {},
                         std::vector<AudioEvent> audioEvents = {}, const std::vector<AutomationClip>& automationClips = {},
                         Console console = Console::defaults())
    {
        Project p;
        p.lengthTicks = std::max (1, loopLengthTicks);

        if (input.size() > static_cast<size_t> (kMaxChannels))
            input.resize (kMaxChannels);

        for (auto& c : input)
        {
            c.swing = std::clamp (finiteOr (c.swing, 0.0f), 0.0f, 1.0f);
            if (c.instrument == Instrument::Drums)
            {
                c.kit = std::make_shared<const DrumKit> (c.kit != nullptr ? c.kit->sanitised() : DrumKit::empty());
                applyPadSwing (c.notes, *c.kit, p.lengthTicks); // each pad has its own swing; the channel's is not used
            }
            else
            {
                c.kit = nullptr;
                applySwing (c.notes, c.swing, p.lengthTicks);
            }
            c.notes = Sequence::make (c.notes, p.lengthTicks).notes; // validate + sort
            c.volume = std::clamp (c.volume, 0.0f, 1.5f);
            c.pan = std::clamp (c.pan, -1.0f, 1.0f);
            c.rootPitch = std::clamp (c.rootPitch, 0, 127);
            c.pitch = std::clamp (finiteOr (c.pitch, 0.0f), -24.0f, 24.0f);
            c.mixerTrack = std::clamp (c.mixerTrack, 0, kMaxTracks - 1);
            c.sampler = c.sampler.sanitised();
            c.synth = c.synth.sanitised(); // a program is sanitised when it is made (SoundFont loader, slices)
            if (c.instrument != Instrument::Sampler)
                c.program = nullptr;
            if (c.instrument != Instrument::Plugin)
                c.plugin = nullptr;
        }

        if (tracks.empty())
            tracks = defaultTracks();
        tracks.resize (static_cast<size_t> (kMaxTracks));
        for (auto& t : tracks)
        {
            t.volume = std::clamp (finiteOr (t.volume, 1.0f), 0.0f, 1.5f);
            t.pan = std::clamp (finiteOr (t.pan, 0.0f), -1.0f, 1.0f);
            for (auto& slot : t.slots)
                slot = slot.sanitised();
            for (auto& send : t.sends)
                send = std::clamp (finiteOr (send, 0.0f), 0.0f, 1.5f);
            t.vcaMask = static_cast<std::uint8_t> (t.vcaMask & ((1u << kVcaGroups) - 1u));
        }

        for (auto& a : console.aux)
        {
            a.volume = std::clamp (finiteOr (a.volume, 1.0f), 0.0f, 1.5f);
            a.pan = std::clamp (finiteOr (a.pan, 0.0f), -1.0f, 1.0f);
            a.sends = {};
            a.vcaMask = 0;
            for (auto& slot : a.slots)
                slot = slot.sanitised();
        }
        for (auto& level : console.vcaLevel)
            level = std::clamp (finiteOr (level, 1.0f), 0.0f, 1.5f);
        p.console = std::move (console);

        for (auto& slot : masterSlots)
            slot = slot.sanitised();

        audioEvents.erase (std::remove_if (audioEvents.begin(), audioEvents.end(),
                                           [] (const AudioEvent& e)
                                           {
                                               return e.data == nullptr || e.data->numFrames() < 2 || e.startTick < 0 || e.endTick <= e.startTick
                                                   || finiteOr (static_cast<float> (e.offsetSeconds), 0.0f) * e.data->sampleRate >= e.data->numFrames();
                                           }),
                           audioEvents.end());
        for (auto& e : audioEvents)
        {
            e.offsetSeconds = std::isfinite (e.offsetSeconds) ? std::max (0.0, e.offsetSeconds) : 0.0;
            e.mixerTrack = std::clamp (e.mixerTrack, 0, kMaxTracks - 1);
            e.gain = std::clamp (finiteOr (e.gain, 1.0f), 0.0f, 2.0f);
        }
        std::stable_sort (audioEvents.begin(), audioEvents.end(),
                          [] (const AudioEvent& a, const AudioEvent& b) { return a.startTick < b.startTick; });
        p.audioEvents = std::move (audioEvents);

        p.channels = std::move (input);
        p.masterSlots = masterSlots;
        p.tracks = std::move (tracks);
        p.automation = makeAutomationEvents (automationClips, static_cast<int> (p.channels.size()), kMaxTracks,
                                             [&p] (const AutomationTarget& t)
                                             {
                                                 if (t.kind == AutomationKind::TrackEffect)
                                                     return p.tracks[static_cast<size_t> (t.index)].slots[static_cast<size_t> (t.slot)].type;
                                                 if (t.kind == AutomationKind::MasterEffect)
                                                     return p.masterSlots[static_cast<size_t> (t.slot)].type;
                                                 return EffectType::None;
                                             });
        return p;
    }
};
} // namespace sls
