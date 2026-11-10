#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include "audio/Arrangement.h"

namespace sls
{
// Multitrack recording (0.36.0): which playlist tracks Record writes to, and which input each one takes. Several tracks can be
// armed at once; each gets its own file from its own input (one input as a mono file, or two neighbouring inputs as a stereo
// file), all started in the same audio block so the takes line up with each other and with the song. Plain C++, tested.

constexpr int kMaxRecordInputs = 64; // input channels a track can name (an interface rarely has more)

struct TrackInput
{
    bool armed = false;
    int firstInput = 0;  // the device's input channel, from 0
    bool stereo = false; // firstInput and firstInput + 1

    bool operator== (const TrackInput&) const = default;
};

struct RecordSetup
{
    std::array<TrackInput, kPlaylistTracks> tracks {};

    std::vector<int> armedTracks() const
    {
        std::vector<int> result;
        for (int t = 0; t < kPlaylistTracks; ++t)
            if (tracks[static_cast<std::size_t> (t)].armed)
                result.push_back (t);
        return result;
    }

    // Arms only `track` (a click on a track's name, as before 0.36.0).
    void armOnly (int track)
    {
        for (int t = 0; t < kPlaylistTracks; ++t)
            tracks[static_cast<std::size_t> (t)].armed = t == track;
    }

    // Toggles one track. The last armed track cannot be disarmed: Record always has somewhere to write.
    void toggle (int track)
    {
        if (track < 0 || track >= kPlaylistTracks)
            return;
        auto& input = tracks[static_cast<std::size_t> (track)];
        if (input.armed && armedTracks().size() <= 1)
            return;
        input.armed = ! input.armed;
    }

    // The device channels the armed tracks need (sorted, no repeats).
    std::vector<int> neededInputs() const
    {
        std::vector<int> result;
        for (const auto& input : tracks)
            if (input.armed)
            {
                result.push_back (input.firstInput);
                if (input.stereo)
                    result.push_back (input.firstInput + 1);
            }
        std::sort (result.begin(), result.end());
        result.erase (std::unique (result.begin(), result.end()), result.end());
        return result;
    }

    // "t:first:stereo;" for every track that differs from the default: armed tracks and tracks with an input of their own.
    std::string toString() const
    {
        std::ostringstream out;
        for (int t = 0; t < kPlaylistTracks; ++t)
        {
            const auto& input = tracks[static_cast<std::size_t> (t)];
            if (input != TrackInput {})
                out << t << ':' << (input.armed ? 1 : 0) << ':' << input.firstInput << ':' << (input.stereo ? 1 : 0) << ';';
        }
        return out.str();
    }

    // Reads what toString wrote; anything malformed or out of range is skipped. With no track armed, `fallbackTrack` is.
    static RecordSetup fromString (const std::string& text, int fallbackTrack)
    {
        RecordSetup setup;
        std::size_t pos = 0;
        while (pos < text.size())
        {
            const auto end = std::min (text.find (';', pos), text.size());
            const auto item = text.substr (pos, end - pos);
            pos = end + 1;

            int values[4] { -1, -1, -1, -1 };
            int count = 0;
            std::size_t p = 0;
            bool ok = ! item.empty();
            while (ok && p <= item.size() && count < 4)
            {
                const auto colon = std::min (item.find (':', p), item.size());
                const auto field = item.substr (p, colon - p);
                if (field.empty() || field.size() > 4 || ! std::all_of (field.begin(), field.end(), [] (char c) { return c >= '0' && c <= '9'; }))
                    ok = false;
                else
                    values[count++] = std::stoi (field);
                p = colon + 1;
                if (colon == item.size())
                    break;
            }
            if (! ok || count != 4 || p <= item.size())
                continue;
            const int track = values[0];
            if (track >= kPlaylistTracks || values[1] > 1 || values[3] > 1 || values[2] >= kMaxRecordInputs)
                continue;
            auto& input = setup.tracks[static_cast<std::size_t> (track)];
            input.armed = values[1] == 1;
            input.firstInput = values[2];
            input.stereo = values[3] == 1 && values[2] + 1 < kMaxRecordInputs;
        }
        if (setup.armedTracks().empty())
            setup.tracks[static_cast<std::size_t> (std::clamp (fallbackTrack, 0, kPlaylistTracks - 1))].armed = true;
        return setup;
    }
};

// The audio callback receives only the switched-on input channels, packed together: device channel `channel` is at the index
// equal to the number of switched-on channels below it. -1 when it is off.
inline int callbackInputIndex (const std::vector<bool>& switchedOn, int channel)
{
    if (channel < 0 || channel >= static_cast<int> (switchedOn.size()) || ! switchedOn[static_cast<std::size_t> (channel)])
        return -1;
    return static_cast<int> (std::count (switchedOn.begin(), switchedOn.begin() + channel, true));
}
} // namespace sls
