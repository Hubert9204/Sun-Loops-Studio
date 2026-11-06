#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "audio/SamplerProgram.h"

namespace sls
{
// SoundFont 2 (.sf2) files for the sampler (0.16.0). The file is parsed with TinySoundFont; every region of the chosen preset becomes a
// SampleZone (key and velocity range, root key, tuning, level, pan, loop points), so the sampler's own voices play it.
// Not taken over from the file: modulators, LFOs, the filter and modulation envelope, chorus / reverb sends, exclusive classes.

constexpr juce::int64 kMaxSoundFontBytes = 512LL * 1024 * 1024;

struct SoundFontPreset
{
    int index = 0;     // position in the file's preset list (what loadSoundFont takes)
    int bank = 0, number = 0;
    std::string name;
};

// The presets of a file, sorted by bank and number. Empty with a message in `error` when the file cannot be read.
std::vector<SoundFontPreset> listSoundFontPresets (const juce::File& file, juce::String& error);

// One preset as a sampler program (sanitised; `source` = the file's full path, `preset` = `presetIndex`). `suggested`, when given, receives
// sampler settings that fit it: the envelope of its first zone (SoundFont instruments usually sustain while a key is held).
// nullptr and a message when the file or the preset cannot be read, or the preset holds no sound.
std::shared_ptr<const SamplerProgram> loadSoundFont (const juce::File& file, int presetIndex, juce::String& error, SamplerSettings* suggested = nullptr);

// Where a sampler channel's program comes from, as the project file stores it: a SoundFont preset, or slices of an audio file.
struct ProgramSpec
{
    enum class Kind { None, SoundFont, Slices };
    Kind kind = Kind::None;
    std::string file;        // the .sf2, or the audio file that was sliced
    int preset = -1;         // SoundFont: index of the preset
    std::vector<int> starts; // Slices: first frame of every slice
    int firstKey = 60;       // Slices: the key of the first slice

    bool empty() const noexcept { return kind == Kind::None || file.empty(); }
};

// The spec of a program in memory (`starts` and `firstKey` come from the slice program's zones).
ProgramSpec specFromProgram (const SamplerProgram& program);

juce::var programSpecToVar (const ProgramSpec& spec);
ProgramSpec programSpecFromVar (const juce::var& v); // lenient: a bad spec is Kind::None

// A program with no zones that remembers `spec` (its file could not be read): saving the project keeps the reference.
std::shared_ptr<const SamplerProgram> missingProgram (const ProgramSpec& spec);

// Loads a program again from its spec. `loadAudio` reads an audio file (for slices). nullptr and a message when it cannot be read.
std::shared_ptr<const SamplerProgram> loadProgram (const ProgramSpec& spec, const std::function<std::shared_ptr<const SampleData> (const juce::File&, juce::String&)>& loadAudio,
                                                   juce::String& error);

juce::var samplerSettingsToVar (const SamplerSettings& s);
SamplerSettings samplerSettingsFromVar (const juce::var& v); // missing values keep their defaults, everything clamped
} // namespace sls
