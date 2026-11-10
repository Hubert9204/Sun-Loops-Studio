#include "library/FlpFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace sls::flp
{
namespace
{
// Event ids (the names follow the community's documentation of the format).
constexpr int kWord = 64, kDword = 128, kText = 192, kData = 208;

constexpr int evChannelEnabled = 0;          // byte
constexpr int evChannelType = 21;            // byte
constexpr int evChannelRoutedTo = 22;        // byte: the mixer insert
constexpr int evChannelGroup = kDword + 17;  // 145: the channel's display group
constexpr int evDisplayGroupName = kText + 39; // 231
constexpr int evChannelNew = kWord + 0;      // 64, word: the channel's iid
constexpr int evPatternNew = kWord + 1;      // 65, word: 1-based pattern
constexpr int evTempoCoarse = kWord + 2;     // 66, word: whole BPM (old files)
constexpr int evTempoFine = kWord + 29;      // 93, word: thousandths of a BPM (old files)
constexpr int evArrangementNew = kWord + 35; // 99, word
constexpr int evArrangementCurrent = kWord + 36; // 100, word: closes the list of arrangements
constexpr int evTimeSigNum = 17, evTimeSigBeat = 18;
constexpr int evChannelRoot = kDword + 7;    // 135
constexpr int evTempo = kDword + 28;         // 156: BPM x 1000
constexpr int evBuild = kDword + 31;         // 159
constexpr int evPatternLength = kDword + 36; // 164
constexpr int evChannelName = kText + 0;     // 192 (old files)
constexpr int evPatternName = kText + 1;     // 193
constexpr int evTitle = kText + 2;           // 194
constexpr int evSamplePath = kText + 4;      // 196
constexpr int evVersion = kText + 7;         // 199 (always 8-bit)
constexpr int evInternalName = kText + 9;    // 201
constexpr int evPluginName = kText + 11;     // 203 (a channel's name since 3.3; also a mixer slot's)
constexpr int evNotes = kData + 16;          // 224
constexpr int evPlaylist = kData + 25;       // 233
constexpr int evTrackData = kData + 30;      // 238
constexpr int evTrackName = kText + 47;      // 239
constexpr int evArrangementName = kText + 49; // 241
constexpr int evInsertFirst = kData + 28;    // 236: the mixer starts (insert flags)

constexpr std::uint32_t kDefaultTrackColour = 0x565148; // FL Studio's grey, as stored (R in the low byte)

std::uint32_t le (const std::uint8_t* p, int bytes) noexcept
{
    std::uint32_t v = 0;
    for (int i = 0; i < bytes; ++i)
        v |= static_cast<std::uint32_t> (p[i]) << (8 * i);
    return v;
}

float leFloat (const std::uint8_t* p) noexcept
{
    const auto bits = le (p, 4);
    float f = 0.0f;
    std::memcpy (&f, &bits, 4);
    return f;
}

void put (std::vector<std::uint8_t>& out, std::uint32_t v, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out.push_back (static_cast<std::uint8_t> ((v >> (8 * i)) & 0xff));
}

void putFloat (std::vector<std::uint8_t>& out, float f)
{
    std::uint32_t bits = 0;
    std::memcpy (&bits, &f, 4);
    put (out, bits, 4);
}

// 8-bit text (files before FL Studio 11.5): bytes taken as Latin-1.
std::string fromLatin1 (const std::uint8_t* data, std::size_t size)
{
    std::string out;
    for (std::size_t i = 0; i < size && data[i] != 0; ++i)
    {
        const auto c = data[i];
        if (c < 0x80)
            out += static_cast<char> (c);
        else
        {
            out += static_cast<char> (0xc0 | (c >> 6));
            out += static_cast<char> (0x80 | (c & 0x3f));
        }
    }
    return out;
}

void appendUtf8 (std::string& out, std::uint32_t cp)
{
    if (cp < 0x80)
        out += static_cast<char> (cp);
    else if (cp < 0x800)
    {
        out += static_cast<char> (0xc0 | (cp >> 6));
        out += static_cast<char> (0x80 | (cp & 0x3f));
    }
    else if (cp < 0x10000)
    {
        out += static_cast<char> (0xe0 | (cp >> 12));
        out += static_cast<char> (0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char> (0x80 | (cp & 0x3f));
    }
    else
    {
        out += static_cast<char> (0xf0 | (cp >> 18));
        out += static_cast<char> (0x80 | ((cp >> 12) & 0x3f));
        out += static_cast<char> (0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char> (0x80 | (cp & 0x3f));
    }
}

bool versionAtLeast (const std::string& version, int major, int minor)
{
    int parts[2] = { 0, 0 };
    int which = 0;
    bool any = false;
    for (const char c : version)
    {
        if (c >= '0' && c <= '9')
        {
            parts[which] = std::min (parts[which] * 10 + (c - '0'), 100000);
            any = true;
        }
        else if (c == '.')
        {
            if (++which > 1)
                break;
        }
        else
            break;
    }
    return any && (parts[0] > major || (parts[0] == major && parts[1] >= minor));
}

bool validPpq (int ppq)
{
    for (const int p : { 24, 48, 72, 96, 120, 144, 168, 192, 384, 768, 960 })
        if (p == ppq)
            return true;
    return false;
}
} // namespace

std::string fromUtf16 (const std::uint8_t* data, std::size_t size)
{
    std::string out;
    for (std::size_t i = 0; i + 1 < size; i += 2)
    {
        std::uint32_t unit = static_cast<std::uint32_t> (data[i]) | (static_cast<std::uint32_t> (data[i + 1]) << 8);
        if (unit == 0)
            break;
        if (unit >= 0xd800 && unit < 0xdc00 && i + 3 < size)
        {
            const std::uint32_t low = static_cast<std::uint32_t> (data[i + 2]) | (static_cast<std::uint32_t> (data[i + 3]) << 8);
            if (low >= 0xdc00 && low < 0xe000)
            {
                appendUtf8 (out, 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00));
                i += 2;
                continue;
            }
        }
        if (unit >= 0xd800 && unit < 0xe000)
            unit = 0xfffd; // a lone surrogate
        appendUtf8 (out, unit);
    }
    return out;
}

std::vector<std::uint8_t> utf16 (const std::string& utf8)
{
    std::vector<std::uint8_t> out;
    const auto unit = [&out] (std::uint32_t u) { out.push_back (static_cast<std::uint8_t> (u & 0xff)); out.push_back (static_cast<std::uint8_t> (u >> 8)); };
    for (std::size_t i = 0; i < utf8.size();)
    {
        const auto c = static_cast<std::uint8_t> (utf8[i]);
        std::uint32_t cp = 0xfffd;
        int extra = 0;
        if (c < 0x80) { cp = c; }
        else if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; extra = 1; }
        else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; extra = 2; }
        else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; extra = 3; }
        ++i;
        for (int k = 0; k < extra; ++k, ++i)
        {
            if (i >= utf8.size() || (static_cast<std::uint8_t> (utf8[i]) & 0xc0) != 0x80)
            {
                cp = 0xfffd;
                break;
            }
            cp = (cp << 6) | (static_cast<std::uint8_t> (utf8[i]) & 0x3f);
        }
        if (cp >= 0x10000 && cp <= 0x10ffff)
        {
            cp -= 0x10000;
            unit (0xd800 + (cp >> 10));
            unit (0xdc00 + (cp & 0x3ff));
        }
        else
            unit (cp > 0x10ffff ? 0xfffd : cp);
    }
    unit (0);
    return out;
}

ParseResult parse (const std::uint8_t* data, std::size_t size)
{
    ParseResult result;
    if (data == nullptr || size < 22 || std::memcmp (data, "FLhd", 4) != 0)
    {
        result.error = "This is not an FL Studio project (.flp) file.";
        return result;
    }
    if (le (data + 4, 4) != 6 || std::memcmp (data + 14, "FLdt", 4) != 0)
    {
        result.error = "The FL Studio project file is damaged (its header is not as expected).";
        return result;
    }
    Song song;
    song.format = static_cast<std::int16_t> (le (data + 8, 2));
    song.ppq = static_cast<int> (le (data + 12, 2));
    if (song.format != 0)
    {
        result.error = "This .flp file is not a song project (it is a score, an automation or a channel preset).";
        return result;
    }
    if (! validPpq (song.ppq))
    {
        result.error = "The FL Studio project file is damaged (impossible timebase " + std::to_string (song.ppq) + ").";
        return result;
    }
    const std::size_t declared = le (data + 18, 4);
    std::size_t end = size;
    if (declared + 22 < size)
        end = declared + 22; // something after the data chunk: ignored
    else if (declared + 22 > size)
    {
        result.error = "The FL Studio project file is cut short (it was not saved completely).";
        return result;
    }

    bool unicode = true;
    Channel* channel = nullptr;
    Pattern* pattern = nullptr;
    Track* track = nullptr;
    bool channelsOver = false; // after the arrangements or the mixer start, names and paths are not a channel's
    int arrangement = -1;
    bool haveTempo = false;
    int coarse = -1, fine = 0;
    std::map<int, size_t> patternAt; // 1-based index -> position in song.patterns
    std::vector<Track> tracks;

    const auto text = [&] (const std::uint8_t* p, std::size_t n) { return unicode ? fromUtf16 (p, n) : fromLatin1 (p, n); };
    const auto patternFor = [&] (int index) -> Pattern*
    {
        if (const auto it = patternAt.find (index); it != patternAt.end())
            return &song.patterns[it->second];
        Pattern p;
        p.index = index;
        patternAt[index] = song.patterns.size();
        song.patterns.push_back (p);
        return &song.patterns.back();
    };

    std::size_t pos = 22;
    while (pos < end)
    {
        const int id = data[pos++];
        std::size_t length = 0;
        if (id < kWord)
            length = 1;
        else if (id < kDword)
            length = 2;
        else if (id < kText)
            length = 4;
        else
        {
            int shift = 0;
            bool done = false;
            while (pos < end && shift < 35)
            {
                const auto b = data[pos++];
                length |= static_cast<std::size_t> (b & 0x7f) << shift;
                shift += 7;
                if ((b & 0x80) == 0)
                {
                    done = true;
                    break;
                }
            }
            if (! done)
            {
                result.error = "The FL Studio project file is damaged (an event's size cannot be read).";
                return result;
            }
        }
        if (length > end - pos)
        {
            result.error = "The FL Studio project file is damaged (an event runs past the end of the file).";
            return result;
        }
        const std::uint8_t* v = data + pos;
        pos += length;
        const std::uint32_t number = id < kText ? le (v, static_cast<int> (length)) : 0;

        switch (id)
        {
            case evVersion:
                song.version = fromLatin1 (v, length);
                unicode = versionAtLeast (song.version, 11, 5);
                break;
            case evTempo:
                song.bpm = number / 1000.0;
                haveTempo = true;
                break;
            case evTempoCoarse: coarse = static_cast<int> (number); break;
            case evTempoFine: fine = static_cast<int> (number); break;
            case evBuild: break;
            case evTitle: song.title = text (v, length); break;
            case evChannelNew:
                if (channelsOver)
                {
                    ++song.skippedEvents;
                    break;
                }
                song.channels.push_back ({});
                channel = &song.channels.back();
                channel->iid = static_cast<int> (number);
                break;
            case evChannelType: if (channel != nullptr && ! channelsOver) channel->type = static_cast<int> (number); break;
            case evChannelRoot: if (channel != nullptr && ! channelsOver) channel->rootNote = static_cast<int> (std::min<std::uint32_t> (number, 127)); break;
            case evChannelName:
            case evPluginName:
                if (channel != nullptr && ! channelsOver)
                    channel->name = text (v, length);
                else
                    ++song.skippedEvents;
                break;
            case evInternalName:
                if (channel != nullptr && ! channelsOver)
                    channel->plugin = text (v, length);
                else
                    ++song.skippedEvents;
                break;
            case evSamplePath: if (channel != nullptr && ! channelsOver) channel->samplePath = text (v, length); break;
            case evPatternNew: pattern = patternFor (static_cast<int> (number)); break;
            case evPatternName: if (pattern != nullptr) pattern->name = text (v, length); break;
            case evPatternLength: if (pattern != nullptr) pattern->length = static_cast<int> (std::min<std::uint32_t> (number, 1u << 28)); break;
            case evNotes:
                if (pattern == nullptr)
                    break;
                for (std::size_t o = 0; o + 24 <= length; o += 24)
                {
                    const std::uint8_t* n = v + o;
                    Note note;
                    note.position = static_cast<int> (std::min<std::uint32_t> (le (n, 4), 1u << 28));
                    note.channel = static_cast<int> (le (n + 6, 2));
                    note.length = static_cast<int> (std::min<std::uint32_t> (le (n + 8, 4), 1u << 28));
                    note.key = static_cast<int> (le (n + 12, 2));
                    note.pan = n[20];
                    note.velocity = n[21];
                    pattern->notes.push_back (note);
                }
                break;
            case evArrangementNew:
                ++arrangement;
                ++song.arrangements;
                channelsOver = true;
                channel = nullptr;
                track = nullptr;
                break;
            case evArrangementName: break;
            case evPlaylist:
            {
                channelsOver = true;
                if (arrangement > 0)
                {
                    ++song.skippedEvents;
                    break;
                }
                // 60-byte items from FL Studio 21, 32-byte before.
                const std::size_t itemSize = versionAtLeast (song.version, 21, 0) ? 60 : (length % 32 == 0 ? 32 : 60);
                for (std::size_t o = 0; o + itemSize <= length; o += itemSize)
                {
                    const std::uint8_t* p = v + o;
                    Item item;
                    item.position = static_cast<int> (std::min<std::uint32_t> (le (p, 4), 1u << 28));
                    const int base = static_cast<int> (le (p + 4, 2));
                    const int index = static_cast<int> (le (p + 6, 2));
                    item.length = static_cast<int> (std::min<std::uint32_t> (le (p + 8, 4), 1u << 28));
                    item.track = std::clamp (499 - static_cast<int> (le (p + 12, 2)), 0, 499);
                    if (index > base)
                        item.pattern = index - base;
                    else
                        item.channel = index;
                    (void) leFloat (p + 24); // the offsets (into the clip's content) are not used
                    song.items.push_back (item);
                }
                break;
            }
            case evTrackData:
                if (arrangement > 0 || length < 8)
                {
                    track = nullptr;
                    break;
                }
                {
                    Track t;
                    t.index = std::clamp (static_cast<int> (le (v, 4)) - 1, 0, 499);
                    const auto colour = le (v + 4, 4) & 0xffffff;
                    t.hasColour = colour != kDefaultTrackColour;
                    t.rgb = ((colour & 0xff) << 16) | (colour & 0xff00) | ((colour >> 16) & 0xff);
                    tracks.push_back (t);
                    track = &tracks.back();
                }
                break;
            case evTrackName:
                if (track != nullptr && arrangement <= 0)
                    track->name = text (v, length);
                break;
            default:
                if (id == evInsertFirst)
                {
                    channelsOver = true;
                    channel = nullptr;
                }
                ++song.skippedEvents;
                break;
        }
    }

    if (! haveTempo && coarse > 0)
        song.bpm = coarse + fine / 1000.0;
    if (! std::isfinite (song.bpm) || song.bpm < 10.0 || song.bpm > 999.0)
        song.bpm = 140.0;
    for (auto& t : tracks)
        if (t.hasColour || ! t.name.empty())
            song.tracks.push_back (t);
    std::sort (song.patterns.begin(), song.patterns.end(), [] (const Pattern& a, const Pattern& b) { return a.index < b.index; });
    result.song = std::move (song);
    return result;
}

namespace
{
void event (std::vector<std::uint8_t>& out, int id, std::uint32_t value)
{
    out.push_back (static_cast<std::uint8_t> (id));
    put (out, value, id < kWord ? 1 : id < kDword ? 2 : 4);
}

void event (std::vector<std::uint8_t>& out, int id, const std::vector<std::uint8_t>& bytes)
{
    out.push_back (static_cast<std::uint8_t> (id));
    std::size_t n = bytes.size();
    do
    {
        auto b = static_cast<std::uint8_t> (n & 0x7f);
        n >>= 7;
        if (n != 0)
            b |= 0x80;
        out.push_back (b);
    } while (n != 0);
    out.insert (out.end(), bytes.begin(), bytes.end());
}

std::vector<std::uint8_t> ascii (const std::string& s)
{
    std::vector<std::uint8_t> out (s.begin(), s.end());
    out.push_back (0);
    return out;
}
} // namespace

std::vector<std::uint8_t> write (const Song& song)
{
    std::vector<std::uint8_t> events;
    event (events, evVersion, ascii (kWrittenVersion));
    event (events, evBuild, 2576);
    event (events, evTempo, static_cast<std::uint32_t> (std::lround (std::clamp (song.bpm, 10.0, 999.0) * 1000.0)));
    event (events, evTimeSigNum, 4);
    event (events, evTimeSigBeat, 4);
    if (! song.title.empty())
        event (events, evTitle, utf16 (song.title));
    // The channel rack's display groups FL Studio starts with; every channel is put in "Unsorted".
    for (const char* group : { "Audio", "Generators", "Unsorted" })
        event (events, evDisplayGroupName, utf16 (group));

    // The notes of each pattern come before the channels, as FL Studio writes them.
    for (const auto& p : song.patterns)
    {
        if (p.notes.empty())
            continue;
        event (events, evPatternNew, static_cast<std::uint32_t> (p.index));
        std::vector<std::uint8_t> notes;
        for (const auto& n : p.notes)
        {
            put (notes, static_cast<std::uint32_t> (std::max (0, n.position)), 4);
            put (notes, 0x4000, 2);                                       // flags
            put (notes, static_cast<std::uint32_t> (n.channel), 2);       // rack channel
            put (notes, static_cast<std::uint32_t> (std::max (0, n.length)), 4);
            put (notes, static_cast<std::uint32_t> (std::clamp (n.key, 0, 131)), 2);
            put (notes, 0, 2);                                            // group
            notes.push_back (120);                                        // fine pitch (centre)
            notes.push_back (0);
            notes.push_back (64);                                         // release
            notes.push_back (0);                                          // MIDI channel
            notes.push_back (static_cast<std::uint8_t> (std::clamp (n.pan, 0, 128)));
            notes.push_back (static_cast<std::uint8_t> (std::clamp (n.velocity, 0, 128)));
            notes.push_back (128);                                        // mod X
            notes.push_back (128);                                        // mod Y
        }
        event (events, evNotes, notes);
    }

    for (const auto& c : song.channels)
    {
        event (events, evChannelNew, static_cast<std::uint32_t> (c.iid));
        event (events, evChannelType, static_cast<std::uint32_t> (c.type));
        event (events, evInternalName, utf16 (c.plugin));
        event (events, evPluginName, utf16 (c.name));
        event (events, evChannelEnabled, 1);
        event (events, evChannelRoutedTo, 0);
        event (events, evChannelGroup, 2);
        if (! c.samplePath.empty())
            event (events, evSamplePath, utf16 (c.samplePath));
        if (c.rootNote != 60)
            event (events, evChannelRoot, static_cast<std::uint32_t> (std::clamp (c.rootNote, 0, 127)));
    }

    for (const auto& p : song.patterns)
    {
        event (events, evPatternNew, static_cast<std::uint32_t> (p.index));
        event (events, evPatternName, utf16 (p.name));
        if (p.length > 0)
            event (events, evPatternLength, static_cast<std::uint32_t> (p.length));
    }

    event (events, evArrangementNew, 0);
    event (events, evArrangementName, utf16 ("Arrangement"));
    {
        auto items = song.items;
        std::stable_sort (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.position < b.position; });
        std::vector<std::uint8_t> list;
        for (const auto& item : items)
        {
            put (list, static_cast<std::uint32_t> (std::max (0, item.position)), 4);
            put (list, kPatternBase, 2);
            put (list, static_cast<std::uint32_t> (item.pattern > 0 ? kPatternBase + item.pattern : std::max (0, item.channel)), 2);
            put (list, static_cast<std::uint32_t> (std::max (1, item.length)), 4);
            put (list, static_cast<std::uint32_t> (499 - std::clamp (item.track, 0, 499)), 2);
            put (list, 0, 2);                                   // group
            list.push_back (120);
            list.push_back (0);
            put (list, item.muted ? 64 | 0x2000 : 64, 2);       // flags
            list.push_back (64);
            list.push_back (100);
            list.push_back (128);
            list.push_back (128);
            putFloat (list, -1.0f);                             // no start offset
            putFloat (list, -1.0f);                             // no end offset
        }
        event (events, evPlaylist, list);
    }
    // All 500 tracks, in order, as FL Studio writes them; the ones of `song.tracks` with their name and colour.
    for (int index = 0; index < 500; ++index)
    {
        Track t;
        t.index = index;
        for (const auto& named : song.tracks)
            if (named.index == index)
                t = named;
        // 66 bytes: index (1-based), colour (R in the low byte), icon, enabled, height 100 %, locked height, content locked,
        // motion, press, trigger sync, queued, tolerant, position sync, grouped, locked, then reserved values.
        std::vector<std::uint8_t> d;
        put (d, static_cast<std::uint32_t> (std::clamp (t.index, 0, 499) + 1), 4);
        const std::uint32_t stored = t.hasColour ? (((t.rgb >> 16) & 0xff) | (t.rgb & 0xff00) | ((t.rgb & 0xff) << 16)) : kDefaultTrackColour;
        put (d, stored, 4);
        put (d, 0, 4);
        d.push_back (1);
        putFloat (d, 1.0f);
        put (d, 0xfffffff0u, 4);
        d.push_back (0);
        put (d, 0, 4);
        put (d, 0, 4);
        put (d, 5, 4);
        put (d, 0, 4);
        put (d, 1, 4);
        put (d, 0, 4);
        d.push_back (0);
        d.push_back (0);
        while (d.size() < 53)
            d.push_back (0);
        for (int i = 0; i < 8; ++i)
            d.push_back (0xff);
        d.push_back (1);
        while (d.size() < 66)
            d.push_back (0);
        event (events, evTrackData, d);
        if (! t.name.empty())
            event (events, evTrackName, utf16 (t.name));
    }
    event (events, evArrangementCurrent, 0);

    std::vector<std::uint8_t> out;
    out.insert (out.end(), { 'F', 'L', 'h', 'd' });
    put (out, 6, 4);
    put (out, 0, 2); // a song
    put (out, static_cast<std::uint32_t> (std::min<std::size_t> (song.channels.size(), 0xffff)), 2);
    put (out, kWrittenPpq, 2);
    out.insert (out.end(), { 'F', 'L', 'd', 't' });
    put (out, static_cast<std::uint32_t> (events.size()), 4);
    out.insert (out.end(), events.begin(), events.end());
    return out;
}
} // namespace sls::flp
