#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace sls
{
// One channel of decoded audio (0.81.0). The samples live in memory (as a std::vector, the only kind before 0.81.0) or, for a
// long file, in a decoded cache file mapped into memory (see AudioImport: mapFloatFile), whose pages the operating system reads
// from disk as they are needed - a background thread reads ahead of the playhead (see DiskPrefetch) so the audio thread does
// not wait for the disk. Reading is the same either way; changing a mapped buffer first copies it into memory.
class SampleBuffer
{
public:
    using value_type = float;
    using size_type = std::size_t;
    using iterator = float*;
    using const_iterator = const float*;

    SampleBuffer() = default;
    SampleBuffer (std::initializer_list<float> values) : owned (values) {}
    explicit SampleBuffer (size_type n, float value = 0.0f) : owned (n, value) {}
    template <typename It, typename = typename std::iterator_traits<It>::iterator_category>
    SampleBuffer (It first, It last) : owned (first, last) {}
    SampleBuffer (const std::vector<float>& v) : owned (v) {}           // NOLINT: as a vector was before
    SampleBuffer (std::vector<float>&& v) noexcept : owned (std::move (v)) {} // NOLINT

    // A buffer reading `count` floats at `samples`, kept valid by `keepAlive` (the mapped file).
    static SampleBuffer mapped (std::shared_ptr<const void> keepAlive, const float* samples, size_type count)
    {
        SampleBuffer b;
        b.mapping = std::move (keepAlive);
        b.external = samples;
        b.externalSize = samples != nullptr ? count : 0;
        return b;
    }
    bool isMapped() const noexcept { return external != nullptr; }

    // reading
    const float* data() const noexcept { return external != nullptr ? external : owned.data(); }
    size_type size() const noexcept { return external != nullptr ? externalSize : owned.size(); }
    bool empty() const noexcept { return size() == 0; }
    const float& operator[] (size_type i) const noexcept { return data()[i]; }
    const_iterator begin() const noexcept { return data(); }
    const_iterator end() const noexcept { return data() + size(); }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }
    const float& front() const noexcept { return data()[0]; }
    const float& back() const noexcept { return data()[size() - 1]; }
    // a copy in memory (for code that wants a vector)
    std::vector<float> toVector() const { return std::vector<float> (begin(), end()); }
    // (no implicit conversion to a vector: a copy of a long file must be asked for, with toVector)

    // changing (a mapped buffer is copied into memory first)
    float* data() { own(); return owned.data(); }
    float& operator[] (size_type i) { own(); return owned[i]; }
    iterator begin() { own(); return owned.data(); }
    iterator end() { own(); return owned.data() + owned.size(); }
    float& front() { own(); return owned.front(); }
    float& back() { own(); return owned.back(); }
    void resize (size_type n, float value = 0.0f)
    {
        if (external != nullptr && n <= externalSize) // shorter: only what stays is copied (not a whole long file)
        {
            std::vector<float> kept (external, external + n);
            drop();
            owned = std::move (kept);
            return;
        }
        own();
        owned.resize (n, value);
    }
    void reserve (size_type n) { own(); owned.reserve (n); }
    void clear() noexcept { drop(); owned.clear(); }
    void push_back (float v) { own(); owned.push_back (v); }
    void assign (size_type n, float value) { drop(); owned.assign (n, value); }
    template <typename It, typename = typename std::iterator_traits<It>::iterator_category>
    void assign (It first, It last)
    {
        std::vector<float> fresh (first, last); // (the source may be this buffer)
        drop();
        owned = std::move (fresh);
    }
    template <typename It>
    iterator insert (const_iterator pos, It first, It last)
    {
        own();
        const auto at = pos - owned.data();
        std::vector<float> extra (first, last);
        owned.insert (owned.begin() + at, extra.begin(), extra.end());
        return owned.data() + at;
    }
    iterator insert (const_iterator pos, size_type n, float value)
    {
        own();
        const auto at = pos - owned.data();
        owned.insert (owned.begin() + at, n, value);
        return owned.data() + at;
    }
    iterator erase (const_iterator first, const_iterator last)
    {
        own();
        const auto from = first - owned.data(), to = last - owned.data();
        owned.erase (owned.begin() + from, owned.begin() + to);
        return owned.data() + from;
    }
    void swap (SampleBuffer& other) noexcept
    {
        owned.swap (other.owned);
        mapping.swap (other.mapping);
        std::swap (external, other.external);
        std::swap (externalSize, other.externalSize);
    }

    bool operator== (const SampleBuffer& o) const noexcept { return size() == o.size() && std::equal (begin(), end(), o.begin()); }

private:
    void own()
    {
        if (external == nullptr)
            return;
        owned.assign (external, external + externalSize);
        drop();
    }
    void drop() noexcept
    {
        mapping.reset();
        external = nullptr;
        externalSize = 0;
    }

    std::vector<float> owned;
    std::shared_ptr<const void> mapping;
    const float* external = nullptr;
    size_type externalSize = 0;
};

// 0.81.0: a long file's waveform overview, made while it is decoded (and kept next to its cache file), so drawing it never reads
// the whole file: the lowest and highest of (left + right) / 2 in every kOverviewFramesPerPeak frames.
constexpr int kOverviewFramesPerPeak = 256;
struct SampleOverview
{
    std::vector<float> lows, highs;
};

// Decoded audio. Immutable once shared.
struct SampleData
{
    SampleBuffer left, right;
    std::shared_ptr<const SampleOverview> overview; // 0.81.0: long files only (see above); a copy that is changed must drop it
    double sampleRate = 44100.0;
    std::string name;
    std::string source; // where it came from: an absolute file path, or "builtin:<id>"; used by channel presets

    int numFrames() const noexcept { return static_cast<int> (left.size()); }
    bool isMapped() const noexcept { return left.isMapped(); } // 0.81.0: read from a cache file on disk (a long file)
};

inline float finiteOr (float v, float fallback) noexcept { return std::isfinite (v) ? v : fallback; }
} // namespace sls
