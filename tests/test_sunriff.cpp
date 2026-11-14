// SunRiff, the electric guitar (0.68.0): headless tests through its AudioProcessor and its playing modes.
#include <cmath>
#include <iostream>
#include <vector>

#include "sunriff/RiffParams.h"
#include "sunriff/RiffPlayer.h"
#include "sunriff/RiffPresets.h"
#include "sunriff/RiffProcessor.h"

void supportCheck (bool ok, const char* name); // tests/test_core.cpp
void supportCountAllocations (bool on);
long supportAllocations();

namespace
{
using sunriff::RiffProcessor;
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

void setParam (RiffProcessor& p, const char* id, float value)
{
    auto* param = p.state.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

// A clean, dry guitar: no amp colour, no effect, so the strings themselves are measured.
void dry (RiffProcessor& p)
{
    p.initPatch();
    setParam (p, sunriff::id::amp, 0.0f);
    setParam (p, sunriff::id::cabinet, 0.0f);
    setParam (p, sunriff::id::drive, 0.15f);
    setParam (p, sunriff::id::fx, 0.0f);
}

struct Played
{
    std::vector<float> left, right;
};

// Plays `notes` at the start (velocity 0.8), releases them after `onFor` samples; `total` samples in all.
Played play (RiffProcessor& p, std::initializer_list<int> notes, int onFor, int total, const std::vector<std::pair<int, juce::MidiMessage>>& extra = {})
{
    Played out;
    out.left.assign (static_cast<size_t> (total), 0.0f);
    out.right.assign (static_cast<size_t> (total), 0.0f);
    juce::AudioBuffer<float> block (2, kBlock);
    for (int start = 0; start < total; start += kBlock)
    {
        const int n = std::min (kBlock, total - start);
        juce::MidiBuffer midi;
        for (const int note : notes)
        {
            if (start == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
            if (onFor >= start && onFor < start + n)
                midi.addEvent (juce::MidiMessage::noteOff (1, note), onFor - start);
        }
        for (const auto& [when, message] : extra)
            if (when >= start && when < start + n)
                midi.addEvent (message, when - start);
        block.setSize (2, n, false, false, true);
        p.processBlock (block, midi);
        for (int i = 0; i < n; ++i)
        {
            out.left[static_cast<size_t> (start + i)] = block.getSample (0, i);
            out.right[static_cast<size_t> (start + i)] = block.getSample (1, i);
        }
    }
    return out;
}

float rms (const std::vector<float>& x, double from, double to)
{
    const auto a = static_cast<size_t> (from * kRate), b = std::min (x.size(), static_cast<size_t> (to * kRate));
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += static_cast<double> (x[i]) * x[i];
    return b > a ? static_cast<float> (std::sqrt (s / static_cast<double> (b - a))) : 0.0f;
}

// The fundamental from the autocorrelation (with parabolic interpolation) of a stretch of the sound.
double pitchOf (const std::vector<float>& x, double from, double seconds, double lo = 30.0, double hi = 1500.0)
{
    const auto a = static_cast<size_t> (from * kRate);
    const auto len = static_cast<size_t> (seconds * kRate);
    const int minLag = static_cast<int> (kRate / hi), maxLag = static_cast<int> (kRate / lo);
    std::vector<double> r (static_cast<size_t> (maxLag + 2), 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double s = 0.0, e1 = 0.0, e2 = 0.0;
        for (size_t i = 0; i < len; ++i)
        {
            const double u = x[a + i], v = x[a + i + static_cast<size_t> (lag)];
            s += u * v;
            e1 += u * u;
            e2 += v * v;
        }
        r[static_cast<size_t> (lag)] = s / std::sqrt (e1 * e2 + 1e-20);
    }
    // the first lag whose correlation is close to the best one (not an octave below)
    double best = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
        best = std::max (best, r[static_cast<size_t> (lag)]);
    int at = -1;
    for (int lag = minLag + 1; lag < maxLag; ++lag)
        if (r[static_cast<size_t> (lag)] > 0.9 * best && r[static_cast<size_t> (lag)] >= r[static_cast<size_t> (lag - 1)]
            && r[static_cast<size_t> (lag)] >= r[static_cast<size_t> (lag + 1)])
        {
            at = lag;
            break;
        }
    if (at < 0)
        return 0.0;
    const double y0 = r[static_cast<size_t> (at - 1)], y1 = r[static_cast<size_t> (at)], y2 = r[static_cast<size_t> (at + 1)];
    const double d = y0 - 2 * y1 + y2;
    const double shift = std::abs (d) > 1e-12 ? 0.5 * (y0 - y2) / d : 0.0;
    return kRate / (at + shift);
}

double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

void testPitchAndStrings()
{
    RiffProcessor p;
    p.prepareToPlay (kRate, kBlock);
    dry (p);
    bool tuned = true;
    for (const int string : { 1, 2, 3, 5, 6 })
        for (const int note : { 40, 52, 64, 76 })
        {
            setParam (p, sunriff::id::string, static_cast<float> (string));
            const auto o = play (p, { note }, 19200, 24000);
            const double f = pitchOf (o.left, 0.1, 0.25);
            const double c = cents (f, 440.0 * std::pow (2.0, (note - 69) / 12.0));
            if (std::abs (c) > 5.0)
            {
                std::cout << "  " << sunriff::stringNames()[string] << " note " << note << ": " << f << " Hz (" << c << " cents)\n";
                tuned = false;
            }
            play (p, {}, 0, 9600, { { 0, juce::MidiMessage::allSoundOff (1) } }); // silence between
        }
    supportCheck (tuned, "SunRiff: every string type plays in tune (within 5 cents, stiffness and loop filter compensated)");

    // Damping: a damped string dies away much faster.
    setParam (p, sunriff::id::string, 1.0f);
    setParam (p, sunriff::id::damping, 0.1f);
    const auto ringing = play (p, { 52 }, 96000, 96000);
    setParam (p, sunriff::id::damping, 0.9f);
    play (p, {}, 0, 48000);
    const auto damped = play (p, { 52 }, 96000, 96000);
    const float ringRatio = rms (ringing.left, 1.5, 2.0) / std::max (1e-9f, rms (ringing.left, 0.05, 0.3));
    const float dampRatio = rms (damped.left, 1.5, 2.0) / std::max (1e-9f, rms (damped.left, 0.05, 0.3));
    supportCheck (ringRatio > 0.1f && dampRatio < 0.01f, "SunRiff: Damping sets how long the string rings");
}

void testHoldAndRelease()
{
    RiffProcessor p;
    p.prepareToPlay (kRate, kBlock);
    dry (p);
    const auto muted = play (p, { 57 }, 4800, 48000);
    setParam (p, sunriff::id::hold, 1.0f);
    play (p, {}, 0, 48000);
    const auto held = play (p, { 57 }, 4800, 48000);
    supportCheck (rms (muted.left, 0.4, 0.6) < 0.002f && rms (held.left, 0.4, 0.6) > 0.01f,
                  "SunRiff: a released key mutes its string; with Hold it keeps ringing");

    // The sustain pedal holds too, and lifting it mutes.
    setParam (p, sunriff::id::hold, 0.0f);
    play (p, {}, 0, 48000);
    const auto pedal = play (p, { 57 }, 4800, 48000, { { 0, juce::MidiMessage::controllerEvent (1, 64, 127) }, { 24000, juce::MidiMessage::controllerEvent (1, 64, 0) } });
    supportCheck (rms (pedal.left, 0.3, 0.45) > 0.01f && rms (pedal.left, 0.8, 1.0) < 0.002f, "SunRiff: the sustain pedal holds the strings until it is lifted");
}

void testBend()
{
    RiffProcessor p;
    p.prepareToPlay (kRate, kBlock);
    dry (p);
    setParam (p, sunriff::id::hold, 1.0f);
    // PB1 coarse: the wheel at the top bends 12 semitones; Pitch Bend 2 then bends 2.
    const auto up = play (p, { 57 }, 96000, 48000, { { 0, juce::MidiMessage::pitchWheel (1, 16383) } });
    const double fCoarse = pitchOf (up.left, 0.2, 0.2, 100.0, 2000.0);
    play (p, {}, 0, 4800, { { 0, juce::MidiMessage::pitchWheel (1, 8192) }, { 1, juce::MidiMessage::allSoundOff (1) } });
    play (p, {}, 0, 24000);
    setParam (p, sunriff::id::bend2, 1.0f);
    const auto fine = play (p, { 57 }, 96000, 48000);
    const double fFine = pitchOf (fine.left, 0.2, 0.2, 100.0, 2000.0);
    play (p, {}, 0, 4800, { { 1, juce::MidiMessage::allSoundOff (1) } });
    setParam (p, sunriff::id::gliss, 1.0f); // PB2 coarse
    play (p, {}, 0, 24000);
    const auto coarse2 = play (p, { 57 }, 96000, 48000);
    const double fCoarse2 = pitchOf (coarse2.left, 0.2, 0.2, 100.0, 2000.0);
    supportCheck (std::abs (cents (fCoarse, 440.0)) < 15.0 && std::abs (cents (fFine, 246.94)) < 15.0 && std::abs (cents (fCoarse2, 440.0)) < 15.0,
                  "SunRiff: Glissando picks the coarse bend (+-12) and the fine one (+-2): the wheel and Pitch Bend 2");
}

void testPlayingModes()
{
    using sunriff::Player;
    using sunriff::PlayMode;
    using sunriff::StringEvent;
    int out[8];
    const int n1 = Player::powerchord (60, out);
    const bool low = n1 == 3 && out[0] == 60 && out[1] == 65 && out[2] == 72;
    Player::powerchord (74, out);
    supportCheck (low && out[1] == 81 && out[2] == 86, "SunRiff: Powerchords: below C5 root-fourth-octave, above root-fifth-octave (as FL Slayer)");
    int keys[3] { 57, 60, 64 };
    const int n2 = Player::autochord (keys, 3, out);
    int major[1] { 52 };
    int out2[8];
    const int n3 = Player::autochord (major, 1, out2);
    supportCheck (n2 == 6 && out[0] == 57 && out[3] == 57 + 15 && n3 == 6 && out2[3] == 52 + 16 && out2[5] == 52 + 24,
                  "SunRiff: Autochords: a key makes a six-string major chord; keys played with it make it minor");

    Player pl;
    pl.prepare (kRate);
    pl.configure (PlayMode::Strumming, 0.5f, 120.0);
    pl.keyOn (48, 0.8f, 0);
    pl.keyOn (55, 0.8f, 10);
    pl.keyOn (60, 0.8f, 20);
    std::vector<StringEvent> events;
    StringEvent e;
    while (pl.next (48000, e))
        events.push_back (e);
    bool strummed = events.size() == 3;
    for (size_t i = 1; i < events.size(); ++i)
        strummed = strummed && events[i].time > events[i - 1].time && events[i].note > events[i - 1].note;
    pl.keyOn (48, 0.8f, 48000);
    pl.keyOn (55, 0.8f, 48000);
    pl.keyOn (60, 0.8f, 48000);
    events.clear();
    while (pl.next (96000, e))
        events.push_back (e);
    const bool upstroke = events.size() == 3 && events[0].note == 60 && events[2].note == 48;
    supportCheck (strummed && upstroke, "SunRiff: Strumming: keys played together are strummed, a downstroke then an upstroke");

    pl.reset();
    pl.configure (PlayMode::SoloFixed, 0.5f, 120.0);
    pl.keyOn (60, 0.8f, 0);
    pl.keyOn (64, 0.8f, 4800); // legato: slide
    pl.keyOff (64, 9600);      // back to 60, sliding
    pl.keyOff (60, 14400);
    pl.keyOn (67, 0.8f, 20000); // detached: a new pluck
    events.clear();
    while (pl.next (48000, e))
        events.push_back (e);
    const bool solo = events.size() == 5 && events[0].type == StringEvent::On && events[1].type == StringEvent::Glide && events[1].note == 64
                      && events[1].glideSamples > 0 && events[2].type == StringEvent::Glide && events[2].note == 60
                      && events[3].type == StringEvent::Off && events[4].type == StringEvent::On;
    pl.reset();
    pl.configure (PlayMode::SoloDynamic, 0.5f, 120.0);
    pl.keyOn (60, 0.8f, 0);
    pl.keyOn (62, 0.8f, 100);
    pl.keyOn (72, 0.8f, 200);
    events.clear();
    while (pl.next (48000, e))
        events.push_back (e);
    supportCheck (solo && events.size() == 3 && events[2].glideSamples > 4 * events[1].glideSamples,
                  "SunRiff: SoloFixed slides between held keys in a fixed time; SoloDynamic takes longer for longer slides");

    // Through the processor: a power chord sounds three strings, a solo one.
    RiffProcessor p;
    p.prepareToPlay (kRate, kBlock);
    dry (p);
    setParam (p, sunriff::id::mode, 2.0f);
    setParam (p, sunriff::id::speed, 1.0f);
    play (p, { 50 }, 48000, 4800);
    int notes[16];
    const int sounding = p.soundingNotes (notes, 16);
    supportCheck (sounding == 3, "SunRiff: one key in Powerchords sounds three strings");
}

void testAmpEffectsAndPresets()
{
    RiffProcessor p;
    p.prepareToPlay (kRate, kBlock);
    bool stable = true;
    // Every amp, cabinet and effect, loud and with full feedback: finite and bounded.
    for (int a = 0; a < 4; ++a)
        for (int fx = 0; fx < sunriff::FxCount; ++fx)
        {
            p.initPatch();
            setParam (p, sunriff::id::amp, static_cast<float> (a));
            setParam (p, sunriff::id::cabinet, static_cast<float> ((a + fx) % 4));
            setParam (p, sunriff::id::drive, 1.0f);
            setParam (p, sunriff::id::feedback, 1.0f);
            setParam (p, sunriff::id::hold, 1.0f);
            setParam (p, sunriff::id::fx, static_cast<float> (fx));
            setParam (p, sunriff::id::fx1, 0.9f);
            setParam (p, sunriff::id::fx2, 0.9f);
            const auto o = play (p, { 40, 47, 52 }, 48000, 72000);
            float peak = 0.0f;
            bool finite = true;
            for (size_t i = 0; i < o.left.size(); ++i)
            {
                finite = finite && std::isfinite (o.left[i]) && std::isfinite (o.right[i]);
                peak = std::max (peak, std::max (std::abs (o.left[i]), std::abs (o.right[i])));
            }
            if (! finite || peak > 4.0f || peak < 1.0e-3f)
            {
                std::cout << "  amp " << a << " fx " << sunriff::fxNames()[fx] << ": peak " << peak << (finite ? "" : " (not finite)") << "\n";
                stable = false;
            }
            play (p, {}, 0, 256, { { 0, juce::MidiMessage::allSoundOff (1) } });
            play (p, {}, 0, 9600);
        }
    supportCheck (stable, "SunRiff: every amp, cabinet and effect stays finite and bounded, even with full drive and feedback");

    bool presetsOk = true;
    const auto& presets = sunriff::factoryPresets();
    for (int i = 0; i < static_cast<int> (presets.size()); ++i)
    {
        p.loadFactoryPreset (i);
        const auto o = play (p, { 45, 52, 57 }, 24000, 36000);
        float peak = 0.0f;
        bool finite = true;
        for (const float v : o.left)
        {
            finite = finite && std::isfinite (v);
            peak = std::max (peak, std::abs (v));
        }
        if (! finite || peak < 0.01f || peak > 3.0f)
        {
            std::cout << "  preset " << presets[static_cast<size_t> (i)].name << ": peak " << peak << "\n";
            presetsOk = false;
        }
        play (p, {}, 0, 256, { { 0, juce::MidiMessage::allSoundOff (1) } });
    }
    supportCheck (presetsOk && presets.size() >= 30, "SunRiff: the factory presets (30 and more) all play, none too quiet or too loud");
}

void testRealTime()
{
    RiffProcessor p;
    p.prepareToPlay (kRate, kBlock);
    p.loadFactoryPreset (0);
    setParam (p, sunriff::id::mode, 3.0f);
    setParam (p, sunriff::id::fx, static_cast<float> (sunriff::FxMultitap));
    play (p, { 50 }, 256, 512);
    juce::AudioBuffer<float> block (2, kBlock);
    juce::MidiBuffer midi;
    for (int k = 0; k < 6; ++k)
        midi.addEvent (juce::MidiMessage::noteOn (1, 40 + k * 5, 0.9f), k);
    supportCountAllocations (true);
    for (int b = 0; b < 200; ++b)
    {
        p.processBlock (block, midi);
        midi.clear();
        if (b % 20 == 0)
            for (int k = 0; k < 6; ++k)
                midi.addEvent (juce::MidiMessage::noteOn (1, 40 + (b + k * 5) % 30, 0.9f), k * 3);
        if (b % 20 == 10)
            for (int k = 0; k < 30; ++k)
                midi.addEvent (juce::MidiMessage::noteOff (1, 40 + k), 0);
    }
    const long allocations = supportAllocations();
    supportCountAllocations (false);
    supportCheck (allocations == 0, "SunRiff: the audio thread never allocates (strums, slides, feedback, effects)");
}
} // namespace

void testSunRiff()
{
    std::cout << "SunRiff, the electric guitar (0.68.0)\n";
    testPitchAndStrings();
    testHoldAndRelease();
    testBend();
    testPlayingModes();
    testAmpEffectsAndPresets();
    testRealTime();
}

