#include "hexforge/HexPresets.h"

#include <algorithm>
#include <cstdint>
#include <map>

#include "hexforge/HexParams.h"

namespace hexforge
{
namespace
{
// Indices of the choice lists (see HexParams.cpp and HexWaves.cpp).
enum W : std::uint8_t
{
    Sine = 0, Tri, Saw, Ramp, Sqr, P25, P12, P5, HalfSine, FullSine, QuarterSine, Camel, SineOct, SineFifth, Organ, Clarinet,
    BrightSaw, SoftSaw, SoftSquare, Trapezoid, Staircase, SawSquare, DoubleSaw, Expo, Parabola, FoldSine, Sync2, Sync35, Vowel, Bell,
    Metal, Digital, SpecA, SpecB
};
// Ratios: index 0 = 1/4, 1 = 1/2, then n -> n + 1.
constexpr int R (int ratio) { return ratio + 1; }
constexpr int kHalf = 1;
enum M : std::uint8_t { LP24 = 0, LP12, HP24, HP12, BP };
enum L : std::uint8_t { LSine = 0, LTri, LSawUp, LSawDown, LSquare, LHold, LSmooth };
enum Div : std::uint8_t { Bars4 = 0, Bars2, Bar1, Half, HalfT, QuarterD, Quarter, QuarterT, EighthD, Eighth, EighthT, SixteenthD, Sixteenth, SixteenthT, ThirtySecond };
enum S : std::uint8_t { None = 0, FLfo, Lfo2, FEnv, Vel, Wheel, Touch, Key, Rnd, Mac1, Mac2, Mac3, Mac4 };
enum D : std::uint8_t { DNone = 0, Op1L, Op2L, Op3L, Op4L, Op5L, Op6L, FmDepth, Pitch, Cutoff, Reso, Drive, UniDetune, Pan, Amp, DistDrive, Lfo2Rate };
enum Dist : std::uint8_t { Soft = 0, Hard, Tube, Fold, Crush };
enum Mode : std::uint8_t { Poly = 0, Mono, Legato };
enum Spec : std::uint8_t { SSaw = 0, SSquare, SOdd, SBright, SFormant, SMetal, SClear };

float levelFor (const juce::String& name);

class B
{
public:
    B (const char* n, const char* c) : preset { n, c, {} } {}
    B& set (const juce::String& id, float v) { preset.values.push_back ({ id, v }); return *this; }
    // Operator `i` (1..6): its wave, ratio index (R (n) or kHalf), how much of it is heard (out).
    B& op (int i, int wave, int ratio, float out, float level = 1.0f)
    {
        const int o = i - 1;
        return set (opId (o, "wave"), wave).set (opId (o, "ratio"), static_cast<float> (ratio)).set (opId (o, "out"), out).set (opId (o, "level"), level);
    }
    B& fine (int i, float cents) { return set (opId (i - 1, "fine"), cents); }
    B& pan (int i, float p) { return set (opId (i - 1, "pan"), p); }
    B& opEnv (int i, float a, float d, float s, float r)
    {
        const auto e = opId (i - 1, "env");
        return set (e + "_a", a).set (e + "_d", d).set (e + "_s", s).set (e + "_r", r);
    }
    B& fm (int target, int source, float amount) { return set (fmId (target - 1, source - 1), amount); }
    B& depth (float d) { return set ("fm_depth", d); }
    B& uni (int voices, float detune, float spread = 0.8f) { return set ("uni_voices", static_cast<float> (voices)).set ("uni_detune", detune).set ("uni_spread", spread); }
    B& filter (int mode, float cutoff, float res, float drive = 0.2f, float env = 0.0f, float key = 0.5f)
    {
        return set ("flt_mode", mode).set ("flt_cut", cutoff).set ("flt_res", res).set ("flt_drive", drive).set ("flt_env", env).set ("flt_key", key);
    }
    B& noFilter() { return set ("flt_on", 0); }
    B& fenv (float a, float d, float s, float r) { return set ("fenv_a", a).set ("fenv_d", d).set ("fenv_s", s).set ("fenv_r", r); }
    B& aenv (float a, float d, float s, float r) { return set ("aenv_a", a).set ("aenv_d", d).set ("aenv_s", s).set ("aenv_r", r); }
    B& flfoSync (float amount, int shape, int division) { return set ("flfo_amt", amount).set ("flfo_shape", shape).set ("flfo_sync", 1).set ("flfo_div", division); }
    B& flfoFree (float amount, int shape, float hz) { return set ("flfo_amt", amount).set ("flfo_shape", shape).set ("flfo_sync", 0).set ("flfo_rate", hz); }
    B& lfo2Sync (int shape, int division) { return set ("lfo2_shape", shape).set ("lfo2_sync", 1).set ("lfo2_div", division); }
    B& lfo2Free (int shape, float hz) { return set ("lfo2_shape", shape).set ("lfo2_sync", 0).set ("lfo2_rate", hz); }
    B& mx (int source, int dest, float amount)
    {
        const int s = next++;
        return set (slotId (s, "src"), source).set (slotId (s, "dst"), dest).set (slotId (s, "amt"), amount);
    }
    B& mode (int m, float glide = 0.0f) { return set ("mode", m).set ("glide", glide); }
    B& drop (float semitones, float seconds) { return set ("drop_amt", semitones).set ("drop_time", seconds); }
    B& dist (int type, float drive, float mix = 1.0f) { return set ("dist_on", 1).set ("dist_type", type).set ("dist_drive", drive).set ("dist_mix", mix); }
    B& noDist() { return set ("dist_on", 0); }
    B& eq (float low, float mid, float midFreq, float high) { return set ("eq_on", 1).set ("eq_low", low).set ("eq_mid", mid).set ("eq_midf", midFreq).set ("eq_high", high); }
    B& chorus (float mix) { return set ("cho_on", 1).set ("cho_mix", mix); }
    B& delay (int division, float feedback, float mix) { return set ("dly_on", 1).set ("dly_div", division).set ("dly_fb", feedback).set ("dly_mix", mix); }
    B& reverb (float size, float mix) { return set ("rev_on", 1).set ("rev_size", size).set ("rev_mix", mix); }
    B& spectral (int slot, int which) { return set (slot == 0 ? "spectralA" : "spectralB", which); }

    // Shared performance controls: the mod wheel adds FM, Macro 1 = cutoff, 2 = FM depth, 3 = distortion, 4 = unison width.
    FactoryPreset done()
    {
        next = kMatrixSlots - 5;
        mx (Wheel, FmDepth, 0.6f).mx (Mac1, Cutoff, 0.6f).mx (Mac2, FmDepth, 0.8f).mx (Mac3, DistDrive, 0.6f).mx (Mac4, UniDetune, 0.6f);
        set ("master", levelFor (preset.name));
        return preset;
    }

private:
    FactoryPreset preset;
    int next = 0;
};

std::vector<FactoryPreset> build()
{
    std::vector<FactoryPreset> p;
    // --- Neuro ---
    p.push_back (B ("Neuro Growl", "Neuro").op (1, Saw, R (1), 0.8f).op (2, Sine, R (1), 0).op (3, Sine, R (2), 0).fm (1, 2, 0.55f).fm (2, 3, 0.4f).fm (2, 2, 0.3f)
                     .filter (LP24, 900, 0.5f, 0.5f).flfoSync (0.5f, LSine, Eighth).mx (FLfo, FmDepth, 0.5f).mode (Mono).uni (2, 0.1f).dist (Fold, 0.3f).done());
    p.push_back (B ("Neuro Talker", "Neuro").op (1, Vowel, R (1), 0.8f).op (2, Sine, R (2), 0).fm (1, 2, 0.5f).filter (BP, 1200, 0.65f, 0.4f)
                     .flfoSync (0.55f, LSmooth, Sixteenth).mode (Mono).dist (Tube, 0.4f).done());
    p.push_back (B ("Rolling Neuro", "Neuro").op (1, Saw, R (1), 0.7f).op (2, Sqr, R (1), 0).op (3, Sine, kHalf, 0.5f).fm (1, 2, 0.4f)
                     .filter (LP24, 700, 0.45f, 0.5f).flfoSync (0.45f, LTri, EighthT).lfo2Sync (LSine, Sixteenth).mx (Lfo2, FmDepth, 0.4f).mode (Mono).done());
    p.push_back (B ("Neuro Stab", "Neuro").op (1, Saw, R (1), 0.8f).op (2, Sync35, R (1), 0).opEnv (2, 0.0f, 0.15f, 0.0f, 0.1f).fm (1, 2, 0.6f)
                     .aenv (0.001f, 0.3f, 0.0f, 0.15f).filter (LP24, 2500, 0.3f, 0.3f, 0.3f).fenv (0.0f, 0.2f, 0.0f, 0.1f).dist (Hard, 0.35f).done());
    p.push_back (B ("Hollow Neuro", "Neuro").op (1, Sqr, R (1), 0.7f).op (2, Sine, R (3), 0).fm (1, 2, 0.35f).fm (1, 1, 0.3f).filter (LP24, 1500, 0.6f, 0.4f)
                     .flfoSync (0.4f, LSine, Quarter).mode (Mono).done());
    p.push_back (B ("Spectral Growl", "Neuro").spectral (0, SFormant).op (1, SpecA, R (1), 0.8f).op (2, Sine, R (1), 0).fm (1, 2, 0.45f)
                     .filter (LP24, 1500, 0.5f, 0.4f).flfoSync (0.4f, LSine, Eighth).mx (FLfo, FmDepth, 0.5f).mode (Mono).dist (Tube, 0.35f).done());

    // --- Reese ---
    p.push_back (B ("Classic Reese", "Reese").op (1, Saw, R (1), 0.7f).op (2, Saw, R (1), 0.7f).fine (2, 14).op (3, Sine, kHalf, 0.5f).uni (2, 0.2f)
                     .filter (LP24, 700, 0.3f, 0.4f).flfoFree (0.25f, LSine, 0.15f).mode (Mono).dist (Tube, 0.3f).done());
    p.push_back (B ("FM Reese", "Reese").op (1, Saw, R (1), 0.7f).op (2, Saw, R (1), 0.6f).fine (2, 10).op (3, Sine, R (1), 0).fm (1, 3, 0.25f).fm (2, 3, 0.25f)
                     .lfo2Free (LSine, 0.3f).mx (Lfo2, FmDepth, 0.4f).filter (LP24, 900, 0.35f, 0.4f).mode (Mono).done());
    p.push_back (B ("Dark Reese", "Reese").op (1, BrightSaw, R (1), 0.7f).op (2, BrightSaw, R (1), 0.7f).fine (2, -12).op (3, Sine, kHalf, 0.45f).uni (3, 0.25f)
                     .filter (LP24, 450, 0.4f, 0.6f).mode (Mono).dist (Tube, 0.45f).done());
    p.push_back (B ("Wide Reese", "Reese").op (1, Saw, R (1), 0.8f).uni (6, 0.4f, 1.0f).op (2, Sine, kHalf, 0.4f).filter (LP24, 800, 0.3f, 0.4f)
                     .mode (Mono).dist (Soft, 0.3f).done());

    // --- Dubstep ---
    p.push_back (B ("Wobble FM", "Dubstep").op (1, Saw, R (1), 0.8f).op (2, Sine, R (1), 0).op (3, Sine, kHalf, 0.5f).fm (1, 2, 0.5f)
                     .flfoSync (0.7f, LSine, Eighth).mx (FLfo, FmDepth, 0.6f).filter (LP24, 400, 0.5f, 0.4f).mode (Mono, 0.05f).dist (Soft, 0.4f).done());
    p.push_back (B ("Yoi Bass", "Dubstep").op (1, Vowel, R (1), 0.8f).op (2, Saw, R (2), 0).fm (1, 2, 0.4f).filter (LP12, 600, 0.7f, 0.4f)
                     .flfoSync (0.6f, LTri, QuarterT).mode (Mono).dist (Tube, 0.4f).done());
    p.push_back (B ("Screech Bass", "Dubstep").op (1, Sync2, R (1), 0.7f).op (2, Sine, R (4), 0).fm (1, 2, 0.5f).fm (2, 2, 0.2f).filter (HP12, 300, 0.3f)
                     .flfoSync (0.3f, LSquare, Sixteenth).dist (Hard, 0.5f).mode (Mono).done());
    p.push_back (B ("Dub Sub", "Dubstep").op (1, Sine, R (1), 0.9f).op (2, Sine, R (1), 0).opEnv (2, 0.0f, 0.1f, 0.0f, 0.05f).fm (1, 2, 0.2f).noFilter()
                     .mode (Mono).drop (12, 0.05f).noDist().done());
    p.push_back (B ("Laser Wob", "Dubstep").op (1, Sqr, R (1), 0.7f).op (2, Saw, R (1), 0).fm (1, 2, 0.45f).flfoSync (0.6f, LHold, SixteenthT)
                     .filter (LP24, 900, 0.55f, 0.4f).mode (Mono).done());

    // --- Drum & bass ---
    p.push_back (B ("DnB Sub", "DnB").op (1, Sine, R (1), 0.9f).op (2, Tri, R (2), 0.15f).filter (LP12, 400, 0.1f, 0.1f).aenv (0.002f, 0.4f, 0.8f, 0.1f)
                     .mode (Mono).drop (7, 0.04f).noDist().done());
    p.push_back (B ("Jump Up", "DnB").op (1, Sqr, R (1), 0.7f).op (2, Sine, R (1), 0).opEnv (2, 0.0f, 0.2f, 0.1f, 0.1f).fm (1, 2, 0.6f)
                     .filter (LP24, 1200, 0.4f, 0.3f, 0.4f).fenv (0.0f, 0.2f, 0.1f, 0.1f).mode (Mono, 0.03f).dist (Hard, 0.35f).done());
    p.push_back (B ("Liquid Pluck", "DnB").op (1, Sine, R (1), 0.6f).op (2, Sine, R (3), 0).opEnv (2, 0.0f, 0.3f, 0.0f, 0.2f).fm (1, 2, 0.3f).op (3, Tri, R (2), 0.2f)
                     .aenv (0.0f, 0.6f, 0.0f, 0.4f).filter (LP12, 5000, 0.1f).reverb (0.6f, 0.4f).delay (EighthD, 0.35f, 0.25f).noDist().done());
    p.push_back (B ("Amen Stab", "DnB").op (1, Saw, R (1), 0.7f).op (2, Saw, R (2), 0.4f).op (3, Bell, R (4), 0).opEnv (3, 0.0f, 0.1f, 0.0f, 0.1f).fm (1, 3, 0.3f)
                     .aenv (0.001f, 0.25f, 0.0f, 0.15f).filter (LP24, 3000, 0.3f, 0.3f, 0.3f).fenv (0.0f, 0.15f, 0.0f, 0.1f).reverb (0.5f, 0.3f).done());
    p.push_back (B ("Techstep Bass", "DnB").op (1, Saw, R (1), 0.7f).op (2, Digital, kHalf, 0).fm (1, 2, 0.35f).filter (LP24, 600, 0.5f, 0.7f)
                     .flfoFree (0.3f, LSine, 0.5f).dist (Fold, 0.35f).mode (Mono).done());

    // --- Cyberpunk ---
    p.push_back (B ("Night City Lead", "Cyberpunk").op (1, Saw, R (1), 0.7f).uni (5, 0.3f).op (2, Sqr, R (2), 0.3f).op (3, Sine, R (3), 0).fm (1, 3, 0.2f)
                     .filter (LP24, 4000, 0.3f, 0.3f, 0.2f).mode (Legato, 0.06f).lfo2Free (LSine, 5.0f).mx (Lfo2, Pitch, 0.005f)
                     .delay (EighthD, 0.35f, 0.25f).reverb (0.5f, 0.3f).done());
    p.push_back (B ("Neon Pluck", "Cyberpunk").op (1, P25, R (1), 0.7f).op (2, Sine, R (4), 0).opEnv (2, 0.0f, 0.15f, 0.0f, 0.1f).fm (1, 2, 0.3f)
                     .aenv (0.0f, 0.3f, 0.0f, 0.2f).filter (LP24, 2500, 0.3f, 0.2f, 0.4f).fenv (0.0f, 0.2f, 0.0f, 0.1f).delay (EighthD, 0.45f, 0.35f).noDist().done());
    p.push_back (B ("Chrome Bass", "Cyberpunk").op (1, Saw, R (1), 0.8f).op (2, Metal, R (1), 0).fm (1, 2, 0.25f).filter (LP24, 800, 0.45f, 0.4f, 0.35f)
                     .fenv (0.0f, 0.25f, 0.2f, 0.15f).mode (Mono).dist (Tube, 0.35f).done());
    p.push_back (B ("Data Stream", "Cyberpunk").op (1, Digital, R (1), 0.6f).op (2, Sqr, R (5), 0).fm (1, 2, 0.4f).filter (BP, 2000, 0.5f)
                     .flfoSync (0.5f, LHold, Sixteenth).delay (Sixteenth, 0.4f, 0.3f).done());
    p.push_back (B ("Blade Pad", "Cyberpunk").op (1, SoftSaw, R (1), 0.6f).uni (6, 0.3f).op (2, Sine, R (2), 0.3f).op (3, Sine, R (3), 0).fm (2, 3, 0.2f)
                     .aenv (1.2f, 1.0f, 0.9f, 2.5f).filter (LP24, 2000, 0.2f, 0.1f).flfoFree (0.2f, LSine, 0.1f).chorus (0.4f).reverb (0.8f, 0.45f).noDist().done());

    // --- Dark synth ---
    p.push_back (B ("Darksynth Lead", "Dark Synth").op (1, BrightSaw, R (1), 0.7f).uni (3, 0.25f).op (2, Saw, R (2), 0.3f).filter (LP24, 3500, 0.35f, 0.5f)
                     .mode (Mono, 0.05f).dist (Tube, 0.4f).delay (Quarter, 0.3f, 0.2f).reverb (0.55f, 0.35f).done());
    p.push_back (B ("Doom Bass", "Dark Synth").op (1, Saw, kHalf, 0.7f).op (2, Saw, R (1), 0.5f).fine (2, 7).filter (LP24, 300, 0.45f, 0.8f, 0.3f)
                     .fenv (0.0f, 0.4f, 0.2f, 0.2f).mode (Mono).dist (Tube, 0.55f).done());
    p.push_back (B ("Horror Pad", "Dark Synth").op (1, Metal, R (1), 0.5f).uni (4, 0.5f).op (2, Bell, kHalf, 0.4f).aenv (2.0f, 1.0f, 0.9f, 4.0f)
                     .filter (LP24, 1500, 0.3f, 0.2f).flfoFree (0.3f, LSmooth, 0.05f).reverb (0.95f, 0.55f).noDist().done());
    p.push_back (B ("Synthwave Brass", "Dark Synth").op (1, Saw, R (1), 0.7f).uni (3, 0.15f).op (2, Saw, R (1), 0.5f).fine (2, -8)
                     .filter (LP24, 900, 0.2f, 0.3f, 0.45f).fenv (0.05f, 0.5f, 0.4f, 0.4f).aenv (0.03f, 0.4f, 0.9f, 0.4f).chorus (0.4f).reverb (0.6f, 0.3f).done());
    p.push_back (B ("Dark Arp", "Dark Synth").op (1, Sqr, R (1), 0.6f).op (2, Sine, R (2), 0).opEnv (2, 0.0f, 0.12f, 0.0f, 0.1f).fm (1, 2, 0.35f)
                     .aenv (0.0f, 0.25f, 0.0f, 0.15f).filter (LP24, 1800, 0.4f, 0.3f, 0.35f).fenv (0.0f, 0.15f, 0.0f, 0.1f).delay (EighthD, 0.4f, 0.3f).done());

    // --- Industrial ---
    p.push_back (B ("Industrial Hit", "Industrial").op (1, Sqr, R (1), 0.6f).op (2, Metal, R (7), 0).opEnv (2, 0.0f, 0.2f, 0.0f, 0.1f).fm (1, 2, 0.7f).fm (2, 2, 0.4f)
                     .op (3, Digital, kHalf, 0.3f).aenv (0.0f, 0.5f, 0.0f, 0.3f).filter (LP24, 4000, 0.3f, 0.3f, 0.3f).dist (Crush, 0.4f).reverb (0.6f, 0.4f).done());
    p.push_back (B ("Grinder Bass", "Industrial").op (1, Saw, R (1), 0.8f).op (2, Digital, R (1), 0).fm (1, 2, 0.5f).fm (2, 2, 0.5f)
                     .filter (LP24, 1000, 0.3f, 0.9f).dist (Hard, 0.6f).mode (Mono).done());
    p.push_back (B ("Factory Drone", "Industrial").op (1, Saw, kHalf, 0.7f).uni (4, 0.5f).op (2, Metal, R (1), 0).fm (1, 2, 0.3f).lfo2Free (LSine, 0.1f)
                     .mx (Lfo2, FmDepth, 0.5f).filter (LP24, 700, 0.5f, 0.4f).flfoFree (0.4f, LSine, 0.07f).aenv (1.0f, 1.0f, 1.0f, 3.0f).reverb (0.85f, 0.5f).done());
    p.push_back (B ("Steel Clang", "Industrial").op (1, Sine, R (1), 0.7f).op (2, Sine, R (7), 0).opEnv (2, 0.0f, 0.4f, 0.0f, 0.3f).op (3, Sine, R (11), 0)
                     .fm (1, 2, 0.8f).fm (2, 3, 0.5f).aenv (0.0f, 1.0f, 0.0f, 0.8f).noFilter().reverb (0.6f, 0.4f).noDist().done());
    p.push_back (B ("Saw Blade", "Industrial").op (1, Sync35, R (1), 0.7f).uni (2, 0.2f).op (2, Saw, R (2), 0).fm (1, 2, 0.3f).filter (HP12, 400, 0.3f)
                     .dist (Hard, 0.5f).mode (Mono).done());

    // --- FX & metal ---
    p.push_back (B ("Metal Bell", "FX & Metal").op (1, Sine, R (1), 0.7f).op (2, Sine, R (3), 0).fine (2, 30).opEnv (2, 0.0f, 1.2f, 0.0f, 0.8f)
                     .op (3, Sine, R (9), 0).fm (1, 2, 0.5f).fm (1, 3, 0.2f).aenv (0.0f, 2.0f, 0.0f, 1.5f).noFilter().reverb (0.7f, 0.4f).noDist().done());
    p.push_back (B ("Glitch Zap", "FX & Metal").op (1, Sqr, R (1), 0.6f).op (2, Sine, R (2), 0).fm (1, 2, 0.5f).drop (36, 0.1f).aenv (0.0f, 0.15f, 0.0f, 0.1f)
                     .noFilter().delay (Sixteenth, 0.4f, 0.3f).done());
    p.push_back (B ("Alarm Siren", "FX & Metal").op (1, Saw, R (1), 0.6f).op (2, Sine, R (1), 0).fm (1, 2, 0.3f).lfo2Free (LTri, 1.0f).mx (Lfo2, Pitch, 0.25f)
                     .filter (LP24, 3000, 0.2f).reverb (0.6f, 0.3f).done());
    p.push_back (B ("Spectral Metal Pad", "FX & Metal").spectral (1, SMetal).op (1, SpecB, R (1), 0.6f).uni (5, 0.35f).aenv (1.5f, 1.0f, 0.9f, 3.0f)
                     .filter (LP24, 3000, 0.25f, 0.1f).flfoFree (0.25f, LSine, 0.08f).chorus (0.3f).reverb (0.9f, 0.5f).noDist().done());
    p.push_back (B ("Noise Riser", "FX & Metal").op (1, Saw, R (1), 0.5f).uni (6, 0.6f).op (2, Digital, R (1), 0).fm (1, 2, 0.4f).filter (LP24, 200, 0.4f, 0.3f, 0.8f)
                     .fenv (6.0f, 1.0f, 1.0f, 1.0f).aenv (4.0f, 1.0f, 1.0f, 2.0f).reverb (0.8f, 0.45f).done());
    return p;
}

// The master volume of each factory preset, so that they sound about as loud as one another (measured on a four-note chord).
float levelFor (const juce::String& name)
{
    static const std::map<juce::String, float> levels {
        { "Alarm Siren", -8.8f },
        { "Amen Stab", -3.9f },
        { "Blade Pad", -1.9f },
        { "Chrome Bass", -8.8f },
        { "Classic Reese", -10.5f },
        { "Dark Arp", -3.1f },
        { "Dark Reese", -8.8f },
        { "Darksynth Lead", -9.5f },
        { "Data Stream", -12.6f },
        { "DnB Sub", -2.1f },
        { "Doom Bass", -10.0f },
        { "Dub Sub", -4.8f },
        { "FM Reese", -7.2f },
        { "Factory Drone", -8.1f },
        { "Glitch Zap", -3.9f },
        { "Grinder Bass", -2.4f },
        { "Hollow Neuro", -7.2f },
        { "Horror Pad", 2.0f },
        { "Industrial Hit", 1.3f },
        { "Jump Up", -11.1f },
        { "Laser Wob", -7.9f },
        { "Liquid Pluck", 0.5f },
        { "Metal Bell", -2.6f },
        { "Neon Pluck", 1.2f },
        { "Neuro Growl", -9.8f },
        { "Neuro Stab", 2.0f },
        { "Neuro Talker", -13.5f },
        { "Night City Lead", -9.9f },
        { "Noise Riser", 2.0f },
        { "Rolling Neuro", -7.0f },
        { "Saw Blade", -8.5f },
        { "Screech Bass", -9.0f },
        { "Spectral Growl", -6.8f },
        { "Spectral Metal Pad", 2.0f },
        { "Steel Clang", -0.8f },
        { "Synthwave Brass", -13.8f },
        { "Techstep Bass", -10.1f },
        { "Wide Reese", -10.1f },
        { "Wobble FM", -9.7f },
        { "Yoi Bass", -12.8f }
    };
    const auto it = levels.find (name);
    return it != levels.end() ? it->second : -6.0f;
}
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = build();
    return presets;
}

const juce::StringArray& presetCategories()
{
    static const juce::StringArray categories { "Neuro", "Reese", "Dubstep", "DnB", "Cyberpunk", "Dark Synth", "Industrial", "FX & Metal" };
    return categories;
}

juce::String spectralPresetName (int index)
{
    static const juce::StringArray names { "Saw", "Square", "Odd", "Bright", "Formant", "Metal", "Clear" };
    return names[std::clamp (index, 0, names.size() - 1)];
}
} // namespace hexforge
