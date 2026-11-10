#include "library/HelpIndex.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace sls
{
namespace
{
// UTF-8 letters with accents (Latin-1 range, what Italian uses) folded to their plain letter; other bytes kept.
std::string fold (const std::string& in)
{
    std::string out;
    out.reserve (in.size());
    for (size_t i = 0; i < in.size(); ++i)
    {
        const auto c = static_cast<unsigned char> (in[i]);
        if (c == 0xC3 && i + 1 < in.size())
        {
            const auto d = static_cast<unsigned char> (in[i + 1]);
            const unsigned char cp = static_cast<unsigned char> (0xC0 + (d & 0x3F));
            const char* map = nullptr;
            static const char* lower = "aaaaaaaceeeeiiiidnooooo/ouuuuyty"; // U+00E0 .. U+00FF
            static const char* upper = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"; // U+00C0 .. U+00DF
            map = cp >= 0xE0 ? lower : upper;
            out.push_back (static_cast<char> (std::tolower (static_cast<unsigned char> (map[cp & 0x1F]))));
            ++i;
            continue;
        }
        out.push_back (static_cast<char> (std::tolower (c)));
    }
    return out;
}

// Common English and Italian endings removed, so "effects" finds "effect" and "registrare" finds "registrazione" only as far as
// their stems agree ("registr"). Short words are kept whole.
std::string stem (std::string w)
{
    static const char* endings[] = { "azioni", "azione", "amento", "imento", "mente", "ation", "ations", "ings", "ing", "are", "ere", "ire",
                                     "ando", "endo", "ati", "ate", "ato", "ita", "ies", "ied", "ers", "er", "ed", "es", "s", "i", "e", "a", "o" };
    for (const char* e : endings)
    {
        const size_t n = std::char_traits<char>::length (e);
        if (w.size() >= n + 4 && w.compare (w.size() - n, n, e) == 0)
        {
            w.resize (w.size() - n);
            break;
        }
    }
    return w;
}

const std::set<std::string>& stopWords()
{
    static const std::set<std::string> words { "the", "a", "an", "and", "or", "of", "to", "in", "on", "for", "is", "it", "its", "be", "by",
                                               "with", "as", "at", "this", "that", "from", "can", "how", "do", "i", "you", "your", "what",
                                               "il", "lo", "la", "i", "gli", "le", "un", "una", "uno", "di", "da", "del", "della", "dei",
                                               "delle", "e", "o", "che", "per", "con", "su", "in", "si", "non", "come", "cosa", "mi", "ma",
                                               "al", "alla", "ai", "nel", "nella", "sul", "sulla", "posso", "voglio", "fare", "faccio",
                                               // verbs that say little about where to look
                                               "add", "use", "make", "get", "put", "want", "need", "aggiungo", "aggiungere",
                                               "aggiungi", "metto", "mettere", "uso", "usare", "devo", "vorrei" };
    return words;
}

// FL Studio's names and common words for what the program calls otherwise.
const std::map<std::string, std::string>& synonyms()
{
    static const std::map<std::string, std::string> map {
        { "insert", "mixer" }, { "inserts", "mixer" }, { "vst", "plugin" }, { "vst3", "plugin" }, { "lv2", "plugin" },
        { "slicex", "slicer" }, { "newtone", "pitch" }, { "pitcher", "pitch" }, { "gross", "time" }, { "sytrus", "fm" },
        { "harmor", "additive" }, { "fpc", "drum" }, { "fruity", "" }, { "sidechain", "send" }, { "bus", "aux" },
        { "rec", "record" }, { "registra", "registrazione" }, { "registrare", "registrazione" }, { "batteria", "drum" },
        { "tempo", "tempo" }, { "bpm", "tempo" }, { "metronomo", "metronome" }, { "esporta", "export" }, { "esportare", "export" },
        { "salva", "save" }, { "salvare", "save" }, { "annulla", "undo" }, { "campione", "sample" }, { "campioni", "sample" },
        { "traccia", "track" }, { "tracce", "track" }, { "nota", "note" }, { "note", "note" }, { "automazione", "automation" },
        { "effetto", "effect" }, { "effetti", "effect" }, { "reverb", "effect" }, { "riverbero", "effect" }, { "delay", "effect" },
        { "compressor", "effect" }, { "compressore", "effect" }, { "chorus", "effect" }, { "eq", "effect" }, { "equalizzatore", "effect" }, { "canale", "channel" }, { "canali", "channel" }, { "tastiera", "keyboard" },
    };
    return map;
}
} // namespace

std::vector<std::string> HelpIndex::words (const std::string& text)
{
    std::vector<std::string> out;
    const auto folded = fold (text);
    std::string current;
    const auto flush = [&]
    {
        if (current.empty())
            return;
        if (stopWords().count (current) == 0)
        {
            if (const auto it = synonyms().find (current); it != synonyms().end())
            {
                if (! it->second.empty())
                    out.push_back (stem (it->second));
                out.push_back (stem (current));
            }
            else
                out.push_back (stem (current));
        }
        current.clear();
    };
    for (const char c : folded)
    {
        if (std::isalnum (static_cast<unsigned char> (c)) || (c & 0x80) != 0)
            current.push_back (c);
        else
            flush();
    }
    flush();
    return out;
}

int HelpIndex::termId (const std::string& word, bool create)
{
    const auto it = std::lower_bound (vocabulary.begin(), vocabulary.end(), word);
    if (it != vocabulary.end() && *it == word)
        return static_cast<int> (it - vocabulary.begin());
    if (! create)
        return -1;
    // Keeping the vocabulary sorted renumbers terms: done before any document refers to them (see add).
    const auto at = vocabulary.insert (it, word);
    return static_cast<int> (at - vocabulary.begin());
}

void HelpIndex::add (const std::string& markdown, const std::string& language)
{
    // Cut into sections at "## " and "### " headings; the title keeps the "##" parent ("Mixer > Effects").
    std::vector<Section> found;
    std::string parent, current, body;
    const auto finish = [&]
    {
        if (current.empty())
            return;
        Section s;
        s.language = language;
        s.heading = current;
        s.title = parent.empty() || parent == current ? current : parent + " > " + current;
        s.markdown = body;
        std::string plain;
        bool lineStart = true;
        for (size_t i = 0; i < body.size(); ++i)
        {
            const char c = body[i];
            if (c == '*' || c == '`' || c == '#' || (c == '|' && lineStart) || c == '_')
                continue;
            if (c == '|')
            {
                plain += " - ";
                continue;
            }
            plain.push_back (c == '\n' ? ' ' : c);
            lineStart = c == '\n';
        }
        s.plain = plain;
        found.push_back (std::move (s));
    };
    size_t pos = 0;
    while (pos <= markdown.size())
    {
        const auto end = std::min (markdown.find ('\n', pos), markdown.size());
        const auto line = markdown.substr (pos, end - pos);
        pos = end + 1;
        if (line.rfind ("## ", 0) == 0 || line.rfind ("### ", 0) == 0)
        {
            finish();
            const bool top = line.rfind ("## ", 0) == 0;
            current = line.substr (top ? 3 : 4);
            if (top)
                parent = current;
            body.clear();
            continue;
        }
        if (line.rfind ("# ", 0) == 0)
            continue;
        body += line + "\n";
        if (end == markdown.size())
            break;
    }
    finish();

    // Index: rebuild the vocabulary from every section (simple and fast enough for two manuals).
    for (auto& s : found)
        all.push_back (std::move (s));
    vocabulary.clear();
    std::vector<std::vector<std::string>> textWords, titleWords;
    for (const auto& s : all)
    {
        textWords.push_back (words (s.title + " " + s.plain));
        titleWords.push_back (words (s.title));
        for (const auto& w : textWords.back())
            vocabulary.push_back (w);
    }
    std::sort (vocabulary.begin(), vocabulary.end());
    vocabulary.erase (std::unique (vocabulary.begin(), vocabulary.end()), vocabulary.end());
    documentFrequency.assign (vocabulary.size(), 0);
    docs.clear();
    double totalLength = 0.0;
    for (size_t d = 0; d < all.size(); ++d)
    {
        Doc doc;
        std::map<int, int> counts;
        for (const auto& w : textWords[d])
            ++counts[termId (w, false)];
        for (const auto& [id, n] : counts)
        {
            doc.terms.emplace_back (id, n);
            ++documentFrequency[static_cast<size_t> (id)];
        }
        for (const auto& w : titleWords[d])
            doc.titleTerms.push_back (termId (w, false));
        doc.length = static_cast<int> (textWords[d].size());
        totalLength += doc.length;
        docs.push_back (std::move (doc));
    }
    averageLength = docs.empty() ? 0.0 : totalLength / static_cast<double> (docs.size());
}

std::vector<HelpIndex::Hit> HelpIndex::search (const std::string& query, int limit) const
{
    std::vector<Hit> hits;
    std::vector<int> ids;
    for (const auto& w : words (query))
    {
        const auto it = std::lower_bound (vocabulary.begin(), vocabulary.end(), w);
        if (it != vocabulary.end() && *it == w)
            ids.push_back (static_cast<int> (it - vocabulary.begin()));
    }
    std::sort (ids.begin(), ids.end());
    ids.erase (std::unique (ids.begin(), ids.end()), ids.end());
    if (ids.empty() || docs.empty())
        return hits;

    // The question's language: sections in it come first (by a little), the other manual still answers.
    int italian = 0, english = 0;
    {
        static const std::set<std::string> it { "come", "il", "lo", "la", "un", "una", "di", "del", "della", "che", "per", "con", "piu", "nel", "si", "e" };
        static const std::set<std::string> en { "how", "the", "a", "an", "to", "of", "with", "do", "i", "is", "and", "in", "my" };
        std::string w;
        for (const char c : fold (query) + " ")
        {
            if (std::isalnum (static_cast<unsigned char> (c)))
                w.push_back (c);
            else if (! w.empty())
            {
                italian += it.count (w) > 0 ? 1 : 0;
                english += en.count (w) > 0 ? 1 : 0;
                w.clear();
            }
        }
    }
    const std::string preferred = italian > english ? "it" : english > italian ? "en" : "";

    const double k1 = 1.4, b = 0.75, n = static_cast<double> (docs.size());
    for (size_t d = 0; d < docs.size(); ++d)
    {
        const auto& doc = docs[d];
        double score = 0.0;
        int matched = 0;
        for (const int id : ids)
        {
            const auto it = std::lower_bound (doc.terms.begin(), doc.terms.end(), std::make_pair (id, 0));
            if (it == doc.terms.end() || it->first != id)
                continue;
            ++matched;
            const double df = documentFrequency[static_cast<size_t> (id)];
            const double idf = std::log (1.0 + (n - df + 0.5) / (df + 0.5));
            const double tf = it->second;
            score += idf * tf * (k1 + 1.0) / (tf + k1 * (1.0 - b + b * doc.length / std::max (1.0, averageLength)));
            if (std::find (doc.titleTerms.begin(), doc.titleTerms.end(), id) != doc.titleTerms.end())
                score += 2.0 * idf; // a word of the title counts a lot
        }
        if (matched == 0)
            continue;
        score *= 0.5 + 0.5 * matched / static_cast<double> (ids.size()); // sections with all the words first
        if (! preferred.empty() && all[d].language == preferred)
            score *= 1.25;
        hits.push_back ({ static_cast<int> (d), score, {} });
    }
    std::sort (hits.begin(), hits.end(), [] (const Hit& x, const Hit& y) { return x.score > y.score; });
    if (static_cast<int> (hits.size()) > limit)
        hits.resize (static_cast<size_t> (std::max (0, limit)));

    // Snippets: the sentence with the most query words.
    for (auto& h : hits)
    {
        const auto& plain = all[static_cast<size_t> (h.section)].plain;
        size_t best = 0, bestStart = 0, bestEnd = std::min<size_t> (plain.size(), 200);
        size_t start = 0;
        while (start < plain.size())
        {
            size_t end = plain.find (". ", start);
            end = end == std::string::npos ? plain.size() : end + 1;
            size_t count = 0;
            for (const auto& w : words (plain.substr (start, end - start)))
            {
                const auto it = std::lower_bound (vocabulary.begin(), vocabulary.end(), w);
                if (it != vocabulary.end() && *it == w && std::binary_search (ids.begin(), ids.end(), static_cast<int> (it - vocabulary.begin())))
                    ++count;
            }
            if (count > best)
            {
                best = count;
                bestStart = start;
                bestEnd = end;
            }
            start = end;
        }
        auto snippet = plain.substr (bestStart, std::min<size_t> (bestEnd - bestStart, 260));
        const auto first = snippet.find_first_not_of (' ');
        h.snippet = first == std::string::npos ? std::string() : snippet.substr (first);
    }
    return hits;
}
} // namespace sls
