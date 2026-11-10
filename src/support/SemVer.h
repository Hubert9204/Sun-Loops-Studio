#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sls::semver
{
// Semantic Versioning 2.0.0 (https://semver.org) for the update check (0.43.0). A tag such as "v0.43.0", "0.43.0-beta.2" or
// "1.2.3+build.7" is read; a leading "v" is allowed, build metadata (after "+") is ignored when comparing, and - more lenient
// than the specification, since release tags are often written so - "1.2" means 1.2.0 and "1" means 1.0.0. Plain C++, tested.
struct Version
{
    unsigned long long major = 0, minor = 0, patch = 0;
    std::vector<std::string> preRelease; // "beta.2" -> { "beta", "2" }; empty for a release
    std::string build;                   // kept for display, never compared

    static std::optional<Version> parse (std::string_view text);
    std::string toString() const;
};

namespace detail
{
inline bool isDigits (std::string_view s) noexcept
{
    if (s.empty())
        return false;
    for (const char c : s)
        if (c < '0' || c > '9')
            return false;
    return true;
}

inline bool isIdentifier (std::string_view s) noexcept
{
    if (s.empty())
        return false;
    for (const char c : s)
        if (! ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-'))
            return false;
    return true;
}

inline std::optional<unsigned long long> number (std::string_view s) noexcept
{
    if (! isDigits (s) || (s.size() > 1 && s.front() == '0') || s.size() > 18)
        return std::nullopt; // no leading zeros (the specification), no overflow
    unsigned long long v = 0;
    for (const char c : s)
        v = v * 10 + static_cast<unsigned long long> (c - '0');
    return v;
}

inline std::vector<std::string_view> split (std::string_view s, char separator)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (;;)
    {
        const auto at = s.find (separator, start);
        parts.push_back (s.substr (start, at == std::string_view::npos ? std::string_view::npos : at - start));
        if (at == std::string_view::npos)
            return parts;
        start = at + 1;
    }
}

inline std::string_view trim (std::string_view s) noexcept
{
    while (! s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' || s.front() == '\r'))
        s.remove_prefix (1);
    while (! s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r'))
        s.remove_suffix (1);
    return s;
}
} // namespace detail

inline std::optional<Version> Version::parse (std::string_view text)
{
    text = detail::trim (text);
    if (! text.empty() && (text.front() == 'v' || text.front() == 'V'))
        text.remove_prefix (1);

    Version v;
    if (const auto plus = text.find ('+'); plus != std::string_view::npos)
    {
        const auto build = text.substr (plus + 1);
        for (const auto part : detail::split (build, '.'))
            if (! detail::isIdentifier (part))
                return std::nullopt;
        v.build = std::string (build);
        text = text.substr (0, plus);
    }
    if (const auto dash = text.find ('-'); dash != std::string_view::npos)
    {
        for (const auto part : detail::split (text.substr (dash + 1), '.'))
        {
            if (! detail::isIdentifier (part) || (detail::isDigits (part) && part.size() > 1 && part.front() == '0'))
                return std::nullopt;
            v.preRelease.emplace_back (part);
        }
        text = text.substr (0, dash);
    }
    const auto core = detail::split (text, '.');
    if (core.empty() || core.size() > 3)
        return std::nullopt;
    unsigned long long* fields[] = { &v.major, &v.minor, &v.patch };
    for (std::size_t i = 0; i < core.size(); ++i)
    {
        const auto n = detail::number (core[i]);
        if (! n)
            return std::nullopt;
        *fields[i] = *n;
    }
    return v;
}

inline std::string Version::toString() const
{
    auto s = std::to_string (major) + "." + std::to_string (minor) + "." + std::to_string (patch);
    for (std::size_t i = 0; i < preRelease.size(); ++i)
        s += (i == 0 ? "-" : ".") + preRelease[i];
    if (! build.empty())
        s += "+" + build;
    return s;
}

// <0 when a is older than b, 0 when they have the same precedence, >0 when a is newer (semver.org, section 11).
inline int compare (const Version& a, const Version& b) noexcept
{
    const auto cmp = [] (unsigned long long x, unsigned long long y) { return x < y ? -1 : (x > y ? 1 : 0); };
    if (const int c = cmp (a.major, b.major); c != 0)
        return c;
    if (const int c = cmp (a.minor, b.minor); c != 0)
        return c;
    if (const int c = cmp (a.patch, b.patch); c != 0)
        return c;
    // A pre-release is older than the release itself.
    if (a.preRelease.empty() || b.preRelease.empty())
        return a.preRelease.empty() == b.preRelease.empty() ? 0 : (a.preRelease.empty() ? 1 : -1);
    const auto n = std::min (a.preRelease.size(), b.preRelease.size());
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto& x = a.preRelease[i];
        const auto& y = b.preRelease[i];
        const bool xNum = detail::isDigits (x), yNum = detail::isDigits (y);
        if (xNum && yNum)
        {
            // Numeric identifiers compare as numbers (no leading zeros, so the longer one is the bigger).
            if (x.size() != y.size())
                return x.size() < y.size() ? -1 : 1;
            if (const int c = x.compare (y); c != 0)
                return c < 0 ? -1 : 1;
        }
        else if (xNum != yNum)
            return xNum ? -1 : 1; // numeric identifiers are older than alphanumeric ones
        else if (const int c = x.compare (y); c != 0)
            return c < 0 ? -1 : 1; // ASCII order
    }
    return a.preRelease.size() == b.preRelease.size() ? 0 : (a.preRelease.size() < b.preRelease.size() ? -1 : 1);
}

// Compares two version strings; nothing when either cannot be read.
inline std::optional<int> compare (std::string_view a, std::string_view b)
{
    const auto x = Version::parse (a);
    const auto y = Version::parse (b);
    if (! x || ! y)
        return std::nullopt;
    return compare (*x, *y);
}

// True only when `candidate` reads as a version strictly newer than `current`.
inline bool isNewer (std::string_view candidate, std::string_view current)
{
    const auto c = compare (candidate, current);
    return c.has_value() && *c > 0;
}
} // namespace sls::semver
