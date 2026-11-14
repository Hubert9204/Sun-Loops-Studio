#pragma once

#include <vector>

#include "sunriff/RiffDsp.h"

// SunRiff's amplifier and speaker cabinet (0.68.0), after FL Slayer's AMP section:
//   Dry      - EQ only (Drive is a plain gain; no presence, no feedback): unplugged sounds;
//   Tube     - presence, three tube stages with soft saturation, EQ, then the cabinet;
//   EQ       - the EQ first, then the three distortion stages (for talkbox-like effects);
//   Bandpass - three stages of band-pass filter and soft saturation (Presence sets the band), then the EQ.
// Cabinets: Dry (none), British (aggressive 4x12), Combo (softer open 1x12), StaX (heavy, with a big high boost).
// Every saturation is anti-aliased (ADAA), so it runs sample by sample, as the feedback into the strings needs.
namespace sunriff
{
struct AmpSettings
{
    int type = 1, cabinet = 1;
    float drive = 0.4f;     // 0..1
    float presence = 0.0f;  // -1..1 (+-12 dB)
    float low = 0.0f, mid = 0.0f, high = 0.0f; // -1..1 (+-12 dB) at 200 Hz, 1 kHz, 2.5 kHz

    bool operator== (const AmpSettings& o) const noexcept
    {
        return type == o.type && cabinet == o.cabinet && same (drive, o.drive) && same (presence, o.presence) && same (low, o.low) && same (mid, o.mid)
               && same (high, o.high);
    }
};

class Amp
{
public:
    void prepare (double sampleRate) noexcept;
    void set (const AmpSettings& s) noexcept; // cheap when nothing changed
    float process (float x) noexcept;
    void reset() noexcept;

private:
    float stage (int i, float x) noexcept;
    float eq (float x) noexcept;
    float cab (float x) noexcept;

    double rate = 48000.0;
    AmpSettings settings;
    bool configured = false;
    Biquad presenceShelf, lowShelf, midPeak, highShelf;
    std::array<Biquad, 3> stageHp, stageLp, stageBand;
    std::array<AdaaTanh, 3> sat;
    std::array<DcBlocker, 3> dc;
    std::array<float, 3> gains {};
    std::array<Biquad, 6> cabinet;
    int cabinetCount = 0;
    float makeup = 1.0f, dryGain = 1.0f;
};
} // namespace sunriff
