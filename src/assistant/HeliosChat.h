#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sls
{
class HelpIndex;
}

namespace sls::helios
{
// Helios LLM mode (0.64.0): what is said to the language model, and how. Plain C++ (no JUCE), tested.
//
// Helios answers from the program's own manual: for every question the best sections of the user guide (found by HelpIndex, the
// same offline search the plain Helios uses) go to the model together with the question ("retrieval-augmented generation").
// The model only talks: it cannot press buttons or change the project.

// The models Helios can download (Gemma 4 by Google, Apache License 2.0, as GGUF files made by the llama.cpp project).
struct ModelInfo
{
    std::string id;          // "light", "quality": what the settings remember
    std::string name;        // "Gemma 4 E2B"
    std::string label;       // what the menu shows
    std::string file;        // the file name on disk
    std::string url;         // where it is downloaded from
    std::uint64_t bytes = 0; // its size (about), for the menu and the free-space check
};
const std::vector<ModelInfo>& models();
const ModelInfo* findModel (std::string_view id); // nullptr when unknown

// How much the model reads and writes (in tokens, about 3 to 4 characters of text each).
inline constexpr int kContextTokens = 8192;
inline constexpr int kAnswerTokens = 768;

// "Lode al Sole": about one answer in ten may end with it (decided here, by chance, not by the model: language models are bad at
// "one time in ten"); the model then decides whether the moment is right (not when the user is angry or frustrated, not for a
// crash or an emergency). Without the note it is not allowed, and tidyAnswer() removes it if the model says it anyway.
inline constexpr double kCatchphraseChance = 0.1;
inline constexpr const char* kCatchphrase = "Lode al Sole";
bool catchphraseAllowed (double random01) noexcept; // random01 in [0, 1)

// The fixed instructions: who Helios is and how it answers. Always the same text, so the helper reads it only once per session.
std::string systemPrompt();

struct Excerpt
{
    std::string title; // "Mixer > Effetti"
    std::string text;  // the section without Markdown, shortened
};

// The manual's best sections for a question: at most `count`, together at most `maxCharacters`.
std::vector<Excerpt> excerptsFor (const HelpIndex& index, const std::string& question, int count = 3, std::size_t maxCharacters = 6000);

// The text of the user's turn as the model reads it: the excerpts, the question and, when allowed, the note about the
// catchphrase.
std::string userTurn (const std::string& question, const std::vector<Excerpt>& excerpts, bool catchphrase);

struct Turn
{
    bool fromUser = true;
    std::string text; // as the model read it (a user turn with its excerpts) or wrote it
};

// The whole prompt in Gemma 4's chat format, the newest turn last and the model's turn opened (thinking off). The oldest turns
// are left out until it fits in `maxCharacters`; the system text and the new turn always stay.
std::string buildPrompt (const std::vector<Turn>& history, const std::string& newUserTurn,
                         std::size_t maxCharacters = static_cast<std::size_t> (kContextTokens - kAnswerTokens - 256) * 3);

// Text that must not be read as the model's control marks (<|turn>, <bos>...): the user could type them, the manual could hold
// them. They are broken up with a space, so the model reads them as plain text.
std::string sanitize (std::string_view text);

// The answer as shown and kept: trimmed, and without the catchphrase when it was not allowed.
std::string tidyAnswer (std::string answer, bool catchphrase);
} // namespace sls::helios
