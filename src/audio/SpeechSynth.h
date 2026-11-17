#pragma once

#include <memory>
#include <string>

#include "audio/SampleData.h"

namespace sls
{
// The speech synthesizer (0.74.0, as FL Studio's Speech Synthesizer): text becomes a sample for a sampler channel. It is a
// formant synthesizer: letters become phonemes (read the Italian way, every letter as it is written, the stress on the
// second-last vowel of a word; digits are read as Italian numbers), a voice source (glottal pulses, a buzz or breath) goes
// through three moving formant filters and a noise path for the consonants, with an intonation that falls along a sentence and
// rises at a question mark. Generated the same way on every computer; the sample's source is "builtin:speech:..." so a project
// stores only the text and the settings and makes the voice again when it is opened.

enum class SpeechVoice : int { Male, Female, Child, Robot, Whisper };
constexpr int kSpeechVoices = 5;

struct SpeechSettings
{
    SpeechVoice voice = SpeechVoice::Male;
    float pitch = 0.0f;      // semitones, -12..12
    float speed = 1.0f;      // 0.5..2 (2: twice as fast)
    float intonation = 0.5f; // 0..1: a flat voice to a lively one

    SpeechSettings sanitised() const noexcept;
};

const char* speechVoiceName (SpeechVoice voice) noexcept;

// The id of the sound ("speech:<voice>,<pitch>,<speed>,<intonation>:<text, percent-encoded>"), and back.
std::string speechId (const std::string& text, const SpeechSettings& settings);
bool isSpeechId (const std::string& id) noexcept;
bool parseSpeechId (const std::string& id, std::string& text, SpeechSettings& settings);

// The phonemes `text` is read as, written out (for the dialog's preview: "ciao" -> "tS a o").
std::string speechPhonemes (const std::string& text);

// The voice saying `text` (mono in both channels, at `sampleRate`), named after the text, with its source set to the id;
// nullptr if there is nothing to say. makeSpeechSample takes the id.
std::shared_ptr<const SampleData> speak (const std::string& text, const SpeechSettings& settings, double sampleRate = 44100.0);
std::shared_ptr<const SampleData> makeSpeechSample (const std::string& id);
} // namespace sls
