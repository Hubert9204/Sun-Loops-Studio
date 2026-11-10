#pragma once

#include <string>
#include <vector>

namespace sls
{
// Helios (0.40.0): the program's own manual, searched offline. No language model and no network: the user guides (English and
// Italian) are cut into sections, and a question is matched against their words with BM25, the ranking search engines use
// (titles count more, a few FL Studio words are understood as the program's own). Plain C++, tested.
class HelpIndex
{
public:
    struct Section
    {
        std::string language;   // "en" or "it"
        std::string title;      // "Mixer > Effects"
        std::string heading;    // the section's own heading: "Effects"
        std::string markdown;   // the text as written (for reading)
        std::string plain;      // without markup (for snippets)
    };
    struct Hit
    {
        int section = -1;
        double score = 0.0;
        std::string snippet;    // a sentence or two around the best match
    };

    // Adds a manual (Markdown: "#" headings cut it into sections). The first "# " line is the manual's own title.
    void add (const std::string& markdown, const std::string& language);

    // The best `limit` sections for `query` in either language (both are searched; words are matched without accents and
    // case, with simple English and Italian endings removed). Empty when nothing matches.
    std::vector<Hit> search (const std::string& query, int limit = 12) const;

    const std::vector<Section>& sections() const noexcept { return all; }

    // The words a text is searched by (exposed for the tests).
    static std::vector<std::string> words (const std::string& text);

private:
    struct Doc
    {
        std::vector<std::pair<int, int>> terms; // term id, count
        std::vector<int> titleTerms;
        int length = 0;
    };
    int termId (const std::string& word, bool create);
    std::vector<Section> all;
    std::vector<Doc> docs;
    std::vector<std::string> vocabulary;
    std::vector<int> documentFrequency;
    double averageLength = 0.0;
};
} // namespace sls
