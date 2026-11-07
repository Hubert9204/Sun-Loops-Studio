#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

#include "audio/NoteSequence.h"

namespace sls
{
// The score log (0.28.0, like FL Studio's): everything played on a MIDI keyboard in the last five minutes is kept, whether the
// song was playing or recording or not, so an idea played while just noodling can be put into a pattern afterwards. The audio
// thread stamps each note on and off with the time it was played (seconds of the audio clock, corrected for latency); the message
// thread keeps them here. Free of JUCE and of the GUI, so it can be tested headless.

struct ScoreEvent
{
    bool on = false;
    std::uint8_t channel = 0; // the channel the note played (the one selected when it was played)
    std::uint8_t pitch = 60;
    float velocity = 0.8f;
    double seconds = 0.0;     // when it was played, on a clock that only goes forwards
};

class ScoreLog
{
public:
    static constexpr double kKeepSeconds = 300.0; // five minutes
    static constexpr size_t kMaxEvents = 20000;   // a safety limit (about 30 notes a second for five minutes)

    // Events arrive in the order they were played. Older ones than five minutes before the newest are forgotten.
    void add (const ScoreEvent& e)
    {
        if (! std::isfinite (e.seconds) || e.pitch > 127)
            return;
        events.push_back (e);
        if (events.size() > 1 && events.back().seconds < events[events.size() - 2].seconds)
            events.back().seconds = events[events.size() - 2].seconds; // never backwards
        const double oldest = events.back().seconds - kKeepSeconds;
        while (! events.empty() && (events.front().seconds < oldest || events.size() > kMaxEvents))
            events.pop_front();
    }

    void clear() noexcept { events.clear(); }
    bool empty() const noexcept { return events.empty(); }
    size_t size() const noexcept { return events.size(); }

    struct LoggedNote
    {
        int channel = 0;
        Note note;
    };

    // The logged notes as notes of a pattern at `bpm`: the first one starts at tick 0, the rest keep their timing from there.
    //   quantizeTicks : starts rounded to this grid (0 = exact timing); the first note then still starts at 0
    //   phraseGapSeconds : > 0 keeps only the last phrase: what was played after the last pause of at least this long in which no
    //                      key was held. 0 keeps everything in the log.
    // A note still held at the end of the log ends with the last event. Sorted by start, then pitch.
    std::vector<LoggedNote> notes (double bpm, int quantizeTicks = 0, double phraseGapSeconds = 0.0) const
    {
        struct Timed
        {
            int channel;
            int pitch;
            float velocity;
            double start, end;
        };
        std::vector<Timed> timed;
        std::vector<int> open (256 * 128, 0); // index + 1 into `timed` of the note held on (channel, pitch)
        const double last = events.empty() ? 0.0 : events.back().seconds;
        for (const auto& e : events)
        {
            auto& slot = open[static_cast<size_t> (e.channel) * 128 + e.pitch];
            if (slot > 0) // an off, or a repeated on, ends the held note
            {
                timed[static_cast<size_t> (slot - 1)].end = e.seconds;
                slot = 0;
            }
            if (e.on)
            {
                timed.push_back ({ e.channel, e.pitch, std::clamp (e.velocity, 0.0f, 1.0f), e.seconds, last });
                slot = static_cast<int> (timed.size());
            }
        }
        if (timed.empty() || ! (bpm > 0.0))
            return {};
        std::stable_sort (timed.begin(), timed.end(), [] (const Timed& a, const Timed& b) { return a.start < b.start; });

        size_t first = 0;
        if (phraseGapSeconds > 0.0)
        {
            double coveredUntil = timed[0].end;
            for (size_t i = 1; i < timed.size(); ++i)
            {
                if (timed[i].start - coveredUntil >= phraseGapSeconds)
                    first = i;
                coveredUntil = std::max (coveredUntil, timed[i].end);
            }
        }

        const double ticksPerSecond = bpm / 60.0 * kTicksPerBeat;
        const double origin = timed[first].start;
        std::vector<LoggedNote> out;
        out.reserve (timed.size() - first);
        for (size_t i = first; i < timed.size(); ++i)
        {
            const auto& t = timed[i];
            const double startTicks = (t.start - origin) * ticksPerSecond;
            const double endTicks = (t.end - origin) * ticksPerSecond;
            int start = static_cast<int> (std::lround (startTicks));
            if (quantizeTicks > 0)
                start = static_cast<int> (std::lround (startTicks / quantizeTicks)) * quantizeTicks;
            const int length = std::max (1, static_cast<int> (std::lround (endTicks - startTicks)));
            out.push_back ({ t.channel, Note { t.pitch, start, length, t.velocity } });
        }
        std::stable_sort (out.begin(), out.end(), [] (const LoggedNote& a, const LoggedNote& b)
                          { return a.note.startTick != b.note.startTick ? a.note.startTick < b.note.startTick : a.note.pitch < b.note.pitch; });
        return out;
    }

private:
    std::deque<ScoreEvent> events;
};
} // namespace sls
