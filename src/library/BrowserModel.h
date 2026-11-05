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
    Plugin,        // payload: path of the VST3 bundle (listed only: plugin hosting is not implemented)
    Action,        // payload: the action name (e.g. "scan-plugins"); activated by a click
    Note           // an explanatory line, nothing happens
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
            || kind == NodeKind::Preset;
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

// Quick search. A node stays if its own name contains the query (a folder then keeps everything below it) or if
// something below it stays. An empty query keeps everything. Notes and actions are hidden while searching.
inline BrowserNode filterTree (const BrowserNode& node, const std::string& query)
{
    const auto q = lowerCase (query);
    if (q.empty())
        return node;

    struct Local
    {
        static bool keep (const BrowserNode& n, const std::string& lq, BrowserNode& out)
        {
            if (n.kind == NodeKind::Note || n.kind == NodeKind::Action)
                return false;

            if (containsIgnoreCase (n.name, lq))
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

    BrowserNode result = node;
    result.children.clear();
    for (const auto& c : node.children)
    {
        BrowserNode kept;
        if (Local::keep (c, q, kept))
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
