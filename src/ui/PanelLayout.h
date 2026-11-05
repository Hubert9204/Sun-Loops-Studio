#pragma once

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>

namespace sls
{
// Position, size and visibility of one floating panel, in workspace coordinates.
// Deliberately free of JUCE so it can be unit-tested headless.
struct PanelLayout
{
    int x = 0;
    int y = 0;
    int width = 300;
    int height = 200;
    bool visible = true;

    bool operator== (const PanelLayout& o) const noexcept
    {
        return x == o.x && y == o.y && width == o.width && height == o.height && visible == o.visible;
    }

    // "x y width height visible" - the format stored in the settings file.
    std::string toString() const
    {
        return std::to_string (x) + " " + std::to_string (y) + " " + std::to_string (width) + " "
             + std::to_string (height) + " " + (visible ? "1" : "0");
    }

    // Strict: anything malformed (wrong token count, non-numbers, non-positive size,
    // visible other than 0/1, trailing garbage) gives nullopt, so a corrupted settings
    // file falls back to the default layout instead of producing a broken panel.
    static std::optional<PanelLayout> parse (const std::string& text)
    {
        std::istringstream in (text);
        PanelLayout l;
        int v = 0;
        if (! (in >> l.x >> l.y >> l.width >> l.height >> v))
            return std::nullopt;

        std::string rest;
        if (in >> rest)
            return std::nullopt;

        if (l.width <= 0 || l.height <= 0 || (v != 0 && v != 1))
            return std::nullopt;

        l.visible = (v == 1);
        return l;
    }

    // Size within [min, workspace] (the workspace wins when it is smaller than the minimum),
    // position such that the whole panel is inside the workspace.
    PanelLayout clampedTo (int workW, int workH, int minW, int minH) const
    {
        const int maxW = std::max (1, workW);
        const int maxH = std::max (1, workH);

        PanelLayout r = *this;
        r.width = std::clamp (width, std::min (minW, maxW), maxW);
        r.height = std::clamp (height, std::min (minH, maxH), maxH);
        r.x = std::clamp (x, 0, maxW - r.width);
        r.y = std::clamp (y, 0, maxH - r.height);
        return r;
    }
};
} // namespace sls
