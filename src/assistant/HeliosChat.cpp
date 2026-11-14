#include "assistant/HeliosChat.h"

#include <algorithm>
#include <cctype>

#include "library/HelpIndex.h"

namespace sls::helios
{
namespace
{
std::string trim (std::string_view s)
{
    const auto isSpace = [] (char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };
    size_t b = 0, e = s.size();
    while (b < e && isSpace (s[b]))
        ++b;
    while (e > b && isSpace (s[e - 1]))
        --e;
    return std::string (s.substr (b, e - b));
}

void replaceAll (std::string& s, std::string_view from, std::string_view to)
{
    for (size_t at = s.find (from); at != std::string::npos; at = s.find (from, at + to.size()))
        s.replace (at, from.size(), to);
}

// The manual's Markdown as plain lines: no emphasis, code marks or table rules; table cells separated by " - ".
std::string plainText (const std::string& markdown)
{
    std::string out;
    size_t pos = 0;
    while (pos < markdown.size())
    {
        auto end = markdown.find ('\n', pos);
        if (end == std::string::npos)
            end = markdown.size();
        auto line = trim (std::string_view (markdown).substr (pos, end - pos));
        pos = end + 1;
        if (line.rfind ("|", 0) == 0)
        {
            if (line.find_first_not_of ("|-: ") == std::string::npos)
                continue; // the rule under a table's header
            line = trim (std::string_view (line).substr (1, line.size() - (line.back() == '|' ? 2 : 1)));
            replaceAll (line, " | ", " - ");
        }
        while (! line.empty() && line.front() == '#')
            line.erase (0, 1);
        replaceAll (line, "**", "");
        replaceAll (line, "`", "");
        line = trim (line);
        if (line.empty() && (out.empty() || (out.size() >= 2 && out.compare (out.size() - 2, 2, "\n\n") == 0)))
            continue; // one empty line at most, none at the start
        out += line;
        out += '\n';
    }
    return trim (out);
}

// At most `limit` bytes, cut after a whole sentence or line where possible and never inside a UTF-8 character.
std::string shorten (const std::string& text, std::size_t limit)
{
    if (text.size() <= limit)
        return text;
    auto cut = limit;
    while (cut > 0 && (static_cast<unsigned char> (text[cut]) & 0xC0) == 0x80)
        --cut;
    const auto sentence = std::max (text.rfind (". ", cut), text.rfind ('\n', cut));
    if (sentence != std::string::npos && sentence > limit / 2)
        cut = sentence + 1;
    return trim (std::string_view (text).substr (0, cut)) + " [...]";
}
} // namespace

const std::vector<ModelInfo>& models()
{
    static const std::vector<ModelInfo> all {
        { "light", "Gemma 4 E2B", "Light - Gemma 4 E2B (2.8 GB, any computer)", "gemma-4-E2B-it-Q4_0.gguf",
          "https://huggingface.co/ggml-org/gemma-4-E2B-it-GGUF/resolve/main/gemma-4-E2B-it-Q4_0.gguf", 2841481184ull },
        { "quality", "Gemma 4 E4B", "Better - Gemma 4 E4B (4.6 GB, 16 GB of memory or more)", "gemma-4-E4B-it-Q4_0.gguf",
          "https://huggingface.co/ggml-org/gemma-4-E4B-it-GGUF/resolve/main/gemma-4-E4B-it-Q4_0.gguf", 4590807392ull },
    };
    return all;
}

const ModelInfo* findModel (std::string_view id)
{
    for (const auto& m : models())
        if (m.id == id)
            return &m;
    return nullptr;
}

bool catchphraseAllowed (double random01) noexcept
{
    return random01 >= 0.0 && random01 < kCatchphraseChance;
}

std::string systemPrompt()
{
    return "Sei Helios, l'assistente di Sun Loops Studio, un programma gratuito per fare musica (una DAW ispirata a FL Studio).\n"
           "Carattere: solare, caloroso e disponibile.\n"
           "\n"
           "Il tuo compito: aiutare l'utente a capire e usare Sun Loops Studio.\n"
           "- Rispondi nella lingua in cui ti scrive l'utente (italiano o inglese, o un'altra lingua se la usa), in modo chiaro e "
           "conciso: passi brevi, i nomi di pulsanti, menu e tasti scritti come nel programma.\n"
           "- Basa le risposte sui passi del manuale che trovi nel messaggio dell'utente, sotto \"Dal manuale\". Se il manuale non "
           "copre la domanda, dillo onestamente e suggerisci dove guardare o come chiedere in un altro modo: non inventare funzioni, "
           "menu, pulsanti o scorciatoie.\n"
           "- Non puoi fare azioni nel programma (non puoi cliccare, aprire finestre o cambiare il progetto): spiega all'utente come "
           "farle.\n"
           "- Puoi dare consigli generali di produzione musicale, ma ricorda che il tuo compito principale è il programma.\n"
           "- Se ti chiedono che modello sei o su cosa sei basato: sei Helios, basato su Gemma 4 di Google, e giri in locale sul "
           "computer dell'utente, senza internet.\n"
           "- Non chiudere le risposte con slogan o frasi fatte, salvo quando una nota nel messaggio dell'utente te lo permette.";
}

std::vector<Excerpt> excerptsFor (const HelpIndex& index, const std::string& question, int count, std::size_t maxCharacters)
{
    std::vector<Excerpt> out;
    const auto hits = index.search (question, 12);
    if (hits.empty())
        return out;
    // The manual in the language of the best match only (the two manuals say the same things).
    const auto language = index.sections()[static_cast<size_t> (hits.front().section)].language;
    std::size_t used = 0;
    for (const auto& hit : hits)
    {
        if (static_cast<int> (out.size()) >= count || used >= maxCharacters)
            break;
        const auto& section = index.sections()[static_cast<size_t> (hit.section)];
        if (section.language != language)
            continue;
        const auto room = std::min<std::size_t> (maxCharacters - used, std::max<std::size_t> (maxCharacters / static_cast<size_t> (std::max (1, count)), 1200));
        Excerpt e { sanitize (section.title), sanitize (shorten (plainText (section.markdown), room)) };
        if (e.text.empty())
            continue;
        used += e.title.size() + e.text.size();
        out.push_back (std::move (e));
    }
    return out;
}

std::string userTurn (const std::string& question, const std::vector<Excerpt>& excerpts, bool catchphrase)
{
    std::string s;
    if (excerpts.empty())
        s += "Dal manuale: (nessun passo trovato per questa domanda)\n\n";
    else
    {
        s += "Dal manuale:\n";
        for (size_t i = 0; i < excerpts.size(); ++i)
            s += "[" + std::to_string (i + 1) + "] " + excerpts[i].title + "\n" + excerpts[i].text + "\n\n";
    }
    s += "Domanda dell'utente:\n" + trim (sanitize (question));
    if (catchphrase)
        s += "\n\n(Nota per Helios: in questa risposta puoi concludere con la frase \"" + std::string (kCatchphrase)
             + "\", da sola sull'ultima riga, ma SOLO se l'utente non sembra frustrato o arrabbiato e la domanda non riguarda un "
               "errore grave, un crash, un lavoro perso o un'emergenza. Altrimenti non usarla.)";
    return s;
}

std::string sanitize (std::string_view text)
{
    std::string s (text);
    // Gemma's control marks all begin with "<|" or end with "|>" (<|turn>, <turn|>, <|channel>, <|"|>...), or are <bos> and the like.
    replaceAll (s, "<|", "< |");
    replaceAll (s, "|>", "| >");
    for (const char* mark : { "<bos>", "<eos>", "<pad>", "<unk>", "<mask>" })
    {
        const std::string m (mark);
        replaceAll (s, m, "< " + m.substr (1));
    }
    return s;
}

std::string buildPrompt (const std::vector<Turn>& history, const std::string& newUserTurn, std::size_t maxCharacters)
{
    const auto head = "<bos><|turn>system\n" + trim (systemPrompt()) + "<turn|>\n";
    const auto tail = "<|turn>user\n" + trim (newUserTurn) + "<turn|>\n<|turn>model\n<|channel>thought\n<channel|>";
    const auto turnText = [] (const Turn& t)
    {
        return std::string (t.fromUser ? "<|turn>user\n" : "<|turn>model\n") + trim (t.text) + "<turn|>\n";
    };

    // Keep the newest turns that fit, starting with a question (a lone answer at the start would mean nothing).
    std::size_t first = history.size(), size = head.size() + tail.size();
    while (first > 0 && size + turnText (history[first - 1]).size() <= maxCharacters)
        size += turnText (history[--first]).size();
    while (first < history.size() && ! history[first].fromUser)
        ++first;

    std::string prompt = head;
    for (auto i = first; i < history.size(); ++i)
        prompt += turnText (history[i]);
    return prompt + tail;
}

std::string tidyAnswer (std::string answer, bool catchphrase)
{
    answer = trim (answer);
    if (! catchphrase)
    {
        // The catchphrase on the last line (with or without stars, dots or exclamation marks): taken away.
        const auto lastLine = answer.rfind ('\n');
        const auto start = lastLine == std::string::npos ? 0 : lastLine + 1;
        std::string last;
        for (const char c : answer.substr (start))
            if (std::isalpha (static_cast<unsigned char> (c)) || c == ' ')
                last += static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        if (trim (last) == "lode al sole" && start > 0)
            answer = trim (std::string_view (answer).substr (0, start));
    }
    return answer;
}
} // namespace sls::helios
