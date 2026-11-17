#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace sls
{
// The data behind the file browser's tree. Free of JUCE and of the GUI so that searching can be tested headless.
enum class NodeKind
{
    Folder,        // a category or a directory
    AudioFile,     // payload: absolute path
    BuiltinSample, // payload: id (kick, snare, hat)
    Instrument,    // payload: synth | sampler
    Preset,        // payload: absolute path of the .slspreset file, or "builtin:<id>"
    Plugin,        // payload: "instrument|<identifier>" or "effect|<identifier>" (JUCE's PluginDescription identifier string)
    Action,        // payload: the action name (e.g. "scan-plugins"); activated by a click
    Note,          // an explanatory line, nothing happens
    Project        // 0.65.0: a project to open with a double-click; payload: absolute path of the .slp file, or "demo:<id>"
};

struct BrowserNode
{
    std::string name;
    NodeKind kind = NodeKind::Folder;
    std::string payload;
    std::vector<BrowserNode> children;

    // Only these can be dragged onto a channel.
    bool isDraggable() const noexcept
    {
        return kind == NodeKind::AudioFile || kind == NodeKind::BuiltinSample || kind == NodeKind::Instrument
            || kind == NodeKind::Preset || kind == NodeKind::Plugin;
    }
};

inline std::string lowerCase (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return s;
}

inline bool containsIgnoreCase (const std::string& text, const std::string& lowerQuery)
{
    return lowerQuery.empty() || lowerCase (text).find (lowerQuery) != std::string::npos;
}

// 0.72.0, the browser's tags: a leaf's key (its kind and payload), the key its tags are kept under.
inline std::string tagKey (const BrowserNode& n) { return std::to_string (static_cast<int> (n.kind)) + ":" + n.payload; }

// The tags as typed ("drums, dark kick ,lo-fi" -> "drums", "dark kick", "lo-fi"): trimmed, lower case, no repeats, at most 8.
inline std::vector<std::string> parseTags (const std::string& text)
{
    std::vector<std::string> out;
    std::string current;
    const auto flush = [&]
    {
        const auto first = current.find_first_not_of (" \t#");
        const auto last = current.find_last_not_of (" \t");
        if (first != std::string::npos && last != std::string::npos && last >= first)
        {
            auto tag = lowerCase (current.substr (first, last - first + 1));
            if (tag.size() > 24)
                tag.resize (24);
            if (std::find (out.begin(), out.end(), tag) == out.end() && out.size() < 8)
                out.push_back (tag);
        }
        current.clear();
    };
    for (const char c : text)
    {
        if (c == ',' || c == ';')
            flush();
        else
            current += c;
    }
    flush();
    return out;
}

// Quick search. A node stays if its own name contains the query (a folder then keeps everything below it) or if
// something below it stays. An empty query keeps everything. Notes and actions are hidden while searching.
// 0.72.0: a leaf also stays when one of its tags (looked up with `tagsOf`) contains the query; a query that starts with # looks
// at the tags only ("#drums").
using TagLookup = std::vector<std::string> (*) (const BrowserNode&, const void* context);
inline BrowserNode filterTree (const BrowserNode& node, const std::string& query, TagLookup tagsOf = nullptr, const void* context = nullptr)
{
    auto q = lowerCase (query);
    const bool tagsOnly = ! q.empty() && q[0] == '#';
    if (tagsOnly)
        q = q.substr (1);
    if (q.empty())
        return node;

    struct Local
    {
        bool tagsOnly;
        TagLookup tagsOf;
        const void* context;

        bool tagged (const BrowserNode& n, const std::string& lq) const
        {
            if (tagsOf == nullptr || n.kind == NodeKind::Folder)
                return false;
            for (const auto& tag : tagsOf (n, context))
                if (containsIgnoreCase (tag, lq))
                    return true;
            return false;
        }

        bool keep (const BrowserNode& n, const std::string& lq, BrowserNode& out) const
        {
            if (n.kind == NodeKind::Note || n.kind == NodeKind::Action)
                return false;

            if ((! tagsOnly && containsIgnoreCase (n.name, lq)) || tagged (n, lq))
            {
                out = n; // the whole subtree comes along
                return true;
            }

            out = n;
            out.children.clear();
            for (const auto& c : n.children)
            {
                BrowserNode kept;
                if (keep (c, lq, kept))
                    out.children.push_back (std::move (kept));
            }
            return ! out.children.empty();
        }
    };

    const Local local { tagsOnly, tagsOf, context };
    BrowserNode result = node;
    result.children.clear();
    for (const auto& c : node.children)
    {
        BrowserNode kept;
        if (local.keep (c, q, kept))
            result.children.push_back (std::move (kept));
    }
    return result;
}

// Number of leaf (non-folder) nodes below `node`.
inline int countLeaves (const BrowserNode& node)
{
    if (node.children.empty())
        return node.kind == NodeKind::Folder ? 0 : 1;

    int n = 0;
    for (const auto& c : node.children)
        n += countLeaves (c);
    return n;
}
} // namespace sls
