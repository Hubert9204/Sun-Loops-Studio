#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "audio/NoteSequence.h"
#include "audio/SampleData.h"

namespace sls
{
// The drum machine: one channel with 16 pads. Pad n answers to MIDI note kDrumFirstPitch + n (36..51, the General MIDI drum
// range from C2), so the piano roll, MIDI keyboards and pattern files all play pads without any new concept. Each pad has up to
// four sample layers chosen by velocity, its own level, pan, tuning and swing, and an optional choke group (a pad that starts
// silences the pads of the same group that are still sounding: a closed hi-hat cuts an open one).
constexpr int kDrumPads = 16;
constexpr int kPadLayers = 4;
constexpr int kChokeGroups = 8;      // groups 1..8; 0 means "no group"
constexpr int kDrumFirstPitch = 36;

struct PadLayer
{
    std::shared_ptr<const SampleData> sample;
    int minVelocity = 1; // 1..127: the layer plays from this velocity up, until a layer with a higher minimum takes over
};

struct DrumPad
{
    std::string name = "Pad";
    std::array<PadLayer, kPadLayers> layers {};
    float volume = 1.0f; // 0..1.5
    float pan = 0.0f;    // -1..+1
    float pitch = 0.0f;  // semitones, -24..+24
    float swing = 0.0f;  // 0..1, like the channel swing of the other instruments, but for this pad only
    int chokeGroup = 0;  // 0..kChokeGroups
    bool mute = false;
    bool solo = false;

    bool hasSample() const noexcept
    {
        for (const auto& l : layers)
            if (l.sample != nullptr && l.sample->numFrames() >= 2)
                return true;
        return false;
    }

    // The layer that answers to `velocity` (1..127): of the layers that hold a sample, the one with the highest minimum that
    // does not exceed it; when the velocity is below every minimum, the layer with the lowest minimum. nullptr when the pad is empty.
    const PadLayer* layerFor (int velocity) const noexcept
    {
        const PadLayer* best = nullptr;
        const PadLayer* lowest = nullptr;
        for (const auto& l : layers)
        {
            if (l.sample == nullptr || l.sample->numFrames() < 2)
                continue;
            if (lowest == nullptr || l.minVelocity < lowest->minVelocity)
                lowest = &l;
            if (l.minVelocity <= velocity && (best == nullptr || l.minVelocity > best->minVelocity))
                best = &l;
        }
        return best != nullptr ? best : lowest;
    }
};

struct DrumKit
{
    std::string name = "Kit";
    std::array<DrumPad, kDrumPads> pads {};

    static int padForPitch (int pitch) noexcept
    {
        const int pad = pitch - kDrumFirstPitch;
        return pad >= 0 && pad < kDrumPads ? pad : -1;
    }
    static int pitchForPad (int pad) noexcept { return kDrumFirstPitch + pad; }

    bool anySolo() const noexcept
    {
        for (const auto& p : pads)
            if (p.solo)
                return true;
        return false;
    }

    // A pad plays unless it is muted, or another pad is soloed and this one is not.
    bool padAudible (int pad, bool someSolo) const noexcept
    {
        const auto& p = pads[static_cast<size_t> (pad)];
        return ! p.mute && (! someSolo || p.solo);
    }

    // An empty kit: sixteen pads named "Pad 1" .. "Pad 16".
    static DrumKit empty()
    {
        DrumKit k;
        for (int i = 0; i < kDrumPads; ++i)
            k.pads[static_cast<size_t> (i)].name = "Pad " + std::to_string (i + 1);
        return k;
    }

    // The same kit with every number forced into its range (hostile project files, damaged kit files). Names are cut to 24 characters.
    DrumKit sanitised() const
    {
        DrumKit k = *this;
        k.name = k.name.substr (0, 32);
        for (auto& p : k.pads)
        {
            p.name = p.name.substr (0, 24);
            p.volume = std::clamp (finiteOr (p.volume, 1.0f), 0.0f, 1.5f);
            p.pan = std::clamp (finiteOr (p.pan, 0.0f), -1.0f, 1.0f);
            p.pitch = std::clamp (finiteOr (p.pitch, 0.0f), -24.0f, 24.0f);
            p.swing = std::clamp (finiteOr (p.swing, 0.0f), 0.0f, 1.0f);
            p.chokeGroup = std::clamp (p.chokeGroup, 0, kChokeGroups);
            for (auto& l : p.layers)
                l.minVelocity = std::clamp (l.minVelocity, 1, 127);
        }
        return k;
    }

    // True when `s` is one of the samples of this kit (the audio thread uses it to see whether a sounding voice may go on).
    bool owns (const SampleData* s) const noexcept
    {
        for (const auto& p : pads)
            for (const auto& l : p.layers)
                if (l.sample.get() == s)
                    return true;
        return false;
    }
};

// Swing for a drum channel: every pad delays the notes that fall on off-beat 16ths by its own amount (see Project::applySwing).
inline void applyPadSwing (std::vector<Note>& notes, const DrumKit& kit, int loopLengthTicks)
{
    for (auto& n : notes)
    {
        const int pad = DrumKit::padForPitch (n.pitch);
        if (pad < 0)
            continue;
        const int shift = static_cast<int> (std::lround (kit.pads[static_cast<size_t> (pad)].swing * kMaxSwingShiftTicks));
        if (shift > 0 && n.startTick >= 0 && (n.startTick / kSixteenthTicks) % 2 == 1 && n.startTick + shift < loopLengthTicks)
            n.startTick += shift;
    }
}
} // namespace sls
