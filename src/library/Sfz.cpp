#include "library/Sfz.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>

namespace sls::sfz
{
namespace
{
std::string lower (std::string s)
{
    for (auto& c : s)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return s;
}

std::string trim (const std::string& s)
{
    const auto a = s.find_first_not_of (" \t\r\n");
    if (a == std::string::npos)
        return {};
    const auto b = s.find_last_not_of (" \t\r\n");
    return s.substr (a, b - a + 1);
}

bool number (const std::string& text, double& out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    const double v = std::strtod (text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || ! std::isfinite (v))
        return false;
    out = v;
    return true;
}

using Opcodes = std::map<std::string, std::string>;
} // namespace

int keyFromText (const std::string& textIn)
{
    const auto text = lower (trim (textIn));
    double v = 0.0;
    if (number (text, v))
        return v >= 0.0 && v <= 127.0 ? static_cast<int> (std::lround (v)) : -1;
    if (text.size() < 2)
        return -1;
    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
    if (text[0] < 'a' || text[0] > 'g')
        return -1;
    int pitch = base[text[0] - 'a'];
    size_t i = 1;
    if (text[i] == '#')
    {
        ++pitch;
        ++i;
    }
    else if (text[i] == 'b' && i + 1 < text.size() && (std::isdigit (static_cast<unsigned char> (text[i + 1])) || text[i + 1] == '-'))
    {
        --pitch;
        ++i;
    }
    double octave = 0.0;
    if (! number (text.substr (i), octave) || std::abs (octave - std::floor (octave)) > 1.0e-9)
        return -1;
    const int key = static_cast<int> ((octave + 1.0) * 12.0) + pitch; // SFZ: c4 = 60
    return key >= 0 && key <= 127 ? key : -1;
}

Instrument parse (const std::string& textIn)
{
    Instrument result;

    // Comments out (// to the end of the line, /* ... */), lines kept for the "sample=" values that may hold spaces.
    std::string text;
    text.reserve (textIn.size());
    for (size_t i = 0; i < textIn.size(); ++i)
    {
        if (textIn.compare (i, 2, "//") == 0)
        {
            while (i < textIn.size() && textIn[i] != '\n')
                ++i;
            text.push_back ('\n');
            continue;
        }
        if (textIn.compare (i, 2, "/*") == 0)
        {
            const auto end = textIn.find ("*/", i + 2);
            i = end == std::string::npos ? textIn.size() : end + 1;
            text.push_back (' ');
            continue;
        }
        text.push_back (textIn[i]);
    }

    std::string defaultPath;
    Opcodes global, master, group, region;
    enum class Header : std::uint8_t { None, Control, Global, Master, Group, Region, Other } header = Header::None;

    const auto emitRegion = [&]
    {
        if (header != Header::Region)
            return;
        Opcodes all = global;
        // 0.66.0: key= stands for lokey, hikey and pitch_keycenter together, so a key= on an inner level replaces an outer level's
        // three (a <region> key=a5 inside a <group> lokey=c4 hikey=b4 plays a5 only).
        const auto merge = [&all] (const Opcodes& level)
        {
            if (level.count ("key") != 0)
                for (const char* k : { "lokey", "hikey", "pitch_keycenter" })
                    if (level.count (k) == 0)
                        all.erase (k);
            for (const auto& [k, v] : level)
                all[k] = v;
        };
        merge (master);
        merge (group);
        merge (region);
        Region r;
        const auto get = [&all] (std::initializer_list<const char*> names) -> const std::string*
        {
            for (const char* n : names)
                if (const auto it = all.find (n); it != all.end())
                    return &it->second;
            return nullptr;
        };
        if (const auto* s = get ({ "sample" }))
        {
            if (! s->empty() && s->front() == '*') // "*sine" and other built-in generators: nothing to load
                return;
            r.sample = defaultPath + *s;
            std::replace (r.sample.begin(), r.sample.end(), '\\', '/');
        }
        if (r.sample.empty())
            return;
        if (const auto* s = get ({ "key" }); s != nullptr && keyFromText (*s) >= 0)
            r.loKey = r.hiKey = r.rootKey = keyFromText (*s);
        if (const auto* s = get ({ "lokey" }); s != nullptr && keyFromText (*s) >= 0)
            r.loKey = keyFromText (*s);
        if (const auto* s = get ({ "hikey" }); s != nullptr && keyFromText (*s) >= 0)
            r.hiKey = keyFromText (*s);
        if (const auto* s = get ({ "pitch_keycenter" }); s != nullptr && keyFromText (*s) >= 0)
            r.rootKey = keyFromText (*s);
        double v = 0.0;
        if (const auto* s = get ({ "lovel" }); s != nullptr && number (*s, v))
            r.loVel = std::clamp (static_cast<int> (v), 1, 127);
        if (const auto* s = get ({ "hivel" }); s != nullptr && number (*s, v))
            r.hiVel = std::clamp (static_cast<int> (v), 1, 127);
        if (const auto* s = get ({ "tune", "pitch" }); s != nullptr && number (*s, v))
            r.tuneCents += std::clamp (v, -9600.0, 9600.0);
        if (const auto* s = get ({ "transpose" }); s != nullptr && number (*s, v))
            r.tuneCents += 100.0 * std::clamp (v, -127.0, 127.0);
        if (const auto* s = get ({ "volume" }); s != nullptr && number (*s, v))
            r.volumeDb = std::clamp (v, -144.0, 24.0);
        if (const auto* s = get ({ "pan" }); s != nullptr && number (*s, v))
            r.pan = std::clamp (v, -100.0, 100.0);
        if (const auto* s = get ({ "loop_mode", "loopmode" }))
            r.loop = lower (*s) == "loop_continuous" || lower (*s) == "loop_sustain";
        if (const auto* s = get ({ "loop_start", "loopstart" }); s != nullptr && number (*s, v) && v >= 0.0)
            r.loopStart = static_cast<long long> (v);
        if (const auto* s = get ({ "loop_end", "loopend" }); s != nullptr && number (*s, v) && v >= 0.0)
            r.loopEnd = static_cast<long long> (v);
        if (r.hiKey < r.loKey)
            std::swap (r.loKey, r.hiKey);
        if (r.hiVel < r.loVel)
            std::swap (r.loVel, r.hiVel);
        result.regions.push_back (std::move (r));
    };

    static const char* known[] = { "sample", "key", "lokey", "hikey", "pitch_keycenter", "lovel", "hivel", "tune", "pitch", "transpose",
                                   "volume", "pan", "loop_mode", "loopmode", "loop_start", "loopstart", "loop_end", "loopend", "default_path" };

    size_t pos = 0;
    while (pos < text.size())
    {
        const char c = text[pos];
        if (std::isspace (static_cast<unsigned char> (c)))
        {
            ++pos;
            continue;
        }
        if (c == '<')
        {
            const auto close = text.find ('>', pos);
            if (close == std::string::npos)
                break;
            emitRegion();
            const auto name = lower (trim (text.substr (pos + 1, close - pos - 1)));
            pos = close + 1;
            if (name == "control")
                header = Header::Control;
            else if (name == "global")
            {
                header = Header::Global;
                global.clear();
                master.clear();
                group.clear();
            }
            else if (name == "master")
            {
                header = Header::Master;
                master.clear();
                group.clear();
            }
            else if (name == "group")
            {
                header = Header::Group;
                group.clear();
            }
            else if (name == "region")
            {
                header = Header::Region;
                region.clear();
            }
            else
                header = Header::Other; // <curve>, <effect>, <midi>...: their opcodes are not for regions
            continue;
        }
        if (c == '#')
        {
            const auto end = std::min (text.find ('\n', pos), text.size());
            const auto line = trim (text.substr (pos, end - pos));
            if (line.rfind ("#include", 0) == 0)
                result.includes.push_back (trim (line.substr (8)));
            pos = end;
            continue;
        }
        // opcode=value; the value runs to the next "name=" on the same line (sample paths may hold spaces) or the line's end.
        const auto eq = text.find ('=', pos);
        const auto lineEnd = std::min (text.find ('\n', pos), text.size());
        if (eq == std::string::npos || eq > lineEnd)
        {
            pos = lineEnd;
            continue;
        }
        const auto key = lower (trim (text.substr (pos, eq - pos)));
        size_t valueEnd = lineEnd;
        for (size_t i = eq + 1; i < lineEnd; ++i)
            if (text[i] == '<')
            {
                valueEnd = i;
                break;
            }
            else if (std::isspace (static_cast<unsigned char> (text[i])))
            {
                size_t j = i;
                while (j < lineEnd && std::isspace (static_cast<unsigned char> (text[j])))
                    ++j;
                size_t k = j;
                while (k < lineEnd && (std::isalnum (static_cast<unsigned char> (text[k])) || text[k] == '_'))
                    ++k;
                if (k > j && k < lineEnd && text[k] == '=')
                {
                    valueEnd = i;
                    break;
                }
            }
        const auto value = trim (text.substr (eq + 1, valueEnd - eq - 1));
        pos = valueEnd;

        if (std::find_if (std::begin (known), std::end (known), [&key] (const char* k) { return key == k; }) == std::end (known))
        {
            if (header != Header::Other)
                ++result.skippedOpcodes; // a <curve>'s or <effect>'s own opcodes are not missed
            continue;
        }
        switch (header)
        {
            case Header::Control: if (key == "default_path") defaultPath = value; break;
            case Header::Global: global[key] = value; break;
            case Header::Master: master[key] = value; break;
            case Header::Group: group[key] = value; break;
            case Header::Region: region[key] = value; break;
            case Header::None:
            case Header::Other: break;
        }
    }
    emitRegion();
    return result;
}
} // namespace sls::sfz
