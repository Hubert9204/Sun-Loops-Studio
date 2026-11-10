#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sls::flp
{
// FL Studio project files (.flp), read and written by code written for Sun Loops Studio (0.34.0, AGPLv3): no FL Studio code or
// library is used, only the layout of the file as documented by the free-software community (the PyFLP project in particular).
//
// A .flp file is a header chunk ("FLhd": format, channel count, pulses per quarter note) and a data chunk ("FLdt") holding a flat
// list of events. An event is an id byte and a value whose size the id decides: 0-63 one byte, 64-127 two, 128-191 four, 192-255
// a length (7-bit groups, low first) and that many bytes. Which channel, pattern or playlist an event belongs to is set by the
// "new channel" / "new pattern" / "new arrangement" events before it. Texts are UTF-16 (little-endian, with a terminating zero)
// from FL Studio 11.5 on, plain 8-bit before.
//
// Only what Sun Loops Studio can use is read: the tempo, the title, the channels (name, kind, sample file, root note), the
// patterns (name, notes), the first arrangement's playlist (pattern and audio clips on tracks) and its tracks' names and colours.
// Everything else (plugin states, the mixer, automation, other arrangements) is skipped and counted. Plain C++, so it is tested
// headless.

constexpr int kPatternBase = 20480;          // a playlist item's "item index" above this is a pattern (1-based), below a channel
constexpr const char* kWrittenVersion = "20.8.4.2576"; // the FL Studio version written files claim (32-byte playlist items)
constexpr int kWrittenPpq = 960;             // ticks per quarter note of written files (2 x Sun Loops Studio's 480: exact)

enum class ChannelType
{
    Sampler = 0,    // FL's Sampler (a sample file)
    Native = 2,     // a generator: FL's own synths and wrapped plugins
    Layer = 3,
    Instrument = 4, // audio clip channels and other instruments
    Automation = 5
};

struct Channel
{
    int iid = 0;              // the channel's number in the file (notes and playlist items refer to it)
    int type = 0;             // ChannelType, or another value from a newer version
    std::string name;
    std::string plugin;       // the generator's internal name ("3x Osc", "Fruity Wrapper"), empty for the Sampler
    std::string samplePath;   // as stored (may hold FL's own variables such as %FLStudioFactoryData%)
    int rootNote = 60;        // the key at which the sample plays at its own pitch
};

struct Note
{
    int position = 0;   // ticks (the file's PPQ)
    int length = 0;     // ticks; 0 for a "step" that plays its whole sample
    int channel = 0;    // the channel's iid
    int key = 60;       // MIDI note number (FL Studio's C5 = 60)
    int velocity = 100; // 0..128
    int pan = 64;       // 0..128, 64 = centre
};

struct Pattern
{
    int index = 1;      // 1-based, as the playlist refers to it
    std::string name;
    std::vector<Note> notes;
    int length = 0;     // ticks, when the pattern has a fixed length; 0 = as long as its notes
};

struct Item
{
    int position = 0;   // ticks
    int length = 0;     // ticks
    int track = 0;      // 0-based playlist track (0..499)
    int pattern = 0;    // 1-based pattern, or 0 for an audio clip
    int channel = -1;   // an audio clip's channel iid, or -1
    bool muted = false;
};

struct Track
{
    int index = 0;           // 0-based
    std::string name;
    std::uint32_t rgb = 0;   // 0xRRGGBB
    bool hasColour = false;  // a colour other than FL Studio's default grey
};

struct Song
{
    int format = 0;
    int ppq = 96;
    double bpm = 140.0;
    std::string version;    // "20.8.4.2576"
    std::string title;
    std::vector<Channel> channels;
    std::vector<Pattern> patterns;
    std::vector<Item> items;     // of the first arrangement
    std::vector<Track> tracks;   // of the first arrangement, only those with a name or a colour
    int arrangements = 0;
    int skippedEvents = 0;       // events of things that are not read (plugin states, mixer, automation...)
};

struct ParseResult
{
    std::optional<Song> song;
    std::string error;
};

ParseResult parse (const std::uint8_t* data, std::size_t size);
inline ParseResult parse (const std::vector<std::uint8_t>& bytes) { return parse (bytes.data(), bytes.size()); }

// Writes `song` as a .flp file of FL Studio 20.8 (the version and PPQ in `song` are ignored: kWrittenVersion, kWrittenPpq; the
// positions and lengths must already be in kWrittenPpq ticks).
std::vector<std::uint8_t> write (const Song& song);

// UTF-8 <-> UTF-16LE (with the terminating zero FL Studio writes).
std::vector<std::uint8_t> utf16 (const std::string& utf8);
std::string fromUtf16 (const std::uint8_t* data, std::size_t size);
} // namespace sls::flp
