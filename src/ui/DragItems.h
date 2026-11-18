#pragma once

#include <juce_core/juce_core.h>

namespace sls::drag
{
// Everything that can be dragged from the browser onto the channel rack is a string "sls:<kind>:<payload>":
//   sls:file:/home/me/kick.wav        an audio file, becomes the channel's sample
//   sls:builtin:kick                  a built-in sample (kick, snare, hat)
//   sls:instrument:synth              turns the channel into a synth / sampler
//   sls:preset:/path/to/x.slspreset   a channel preset file
//   sls:preset:builtin:<id>           a factory channel preset
struct Item
{
    juce::String kind;    // "file", "builtin", "instrument", "preset", "plugin" ("instrument|<id>" or "effect|<id>")
    juce::String payload;

    juce::String toString() const { return "sls:" + kind + ":" + payload; }

    static bool parse (const juce::var& description, Item& out)
    {
        if (! description.isString())
            return false;

        const auto text = description.toString();
        if (! text.startsWith ("sls:"))
            return false;

        const auto rest = text.substring (4);
        const int colon = rest.indexOfChar (':');
        if (colon <= 0 || colon == rest.length() - 1)
            return false;

        out.kind = rest.substring (0, colon);
        out.payload = rest.substring (colon + 1);
        return out.kind == "file" || out.kind == "builtin" || out.kind == "instrument" || out.kind == "preset" || out.kind == "plugin";
    }
};

// 0.82.0: moving something inside the program by dragging it (a channel of the rack, a mixer track): "sls-move:<what>:<index>". Not an
// Item, so the browser's drop targets (the playlist, an empty rack) never take it for one of theirs.
inline juce::String moveDescription (const char* what, int index)
{
    return juce::String ("sls-move:") + what + ":" + juce::String (index);
}
// The index a move of `what` carries, or -1 when the description is not one.
inline int parseMove (const juce::var& description, const char* what)
{
    if (! description.isString())
        return -1;
    const auto prefix = juce::String ("sls-move:") + what + ":";
    const auto text = description.toString();
    if (! text.startsWith (prefix) || ! text.substring (prefix.length()).containsOnly ("0123456789") || text.length() == prefix.length())
        return -1;
    return text.substring (prefix.length()).getIntValue();
}
} // namespace sls::drag
