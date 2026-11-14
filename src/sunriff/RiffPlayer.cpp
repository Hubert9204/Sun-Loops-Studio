#include "sunriff/RiffPlayer.h"

#include <algorithm>
#include <cmath>

namespace sunriff
{
namespace
{
constexpr double kGroupSeconds = 0.012;
}

void Player::prepare (double sampleRate) noexcept
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void Player::reset() noexcept
{
    count = 0;
    groupSize = 0;
    groupStart = -1;
    stackSize = 0;
    soloNote = -1;
    for (auto& s : sounding)
        s.key = -1;
}

void Player::configure (PlayMode m, float s, double tempo) noexcept
{
    if (m != mode)
    {
        // Another mode: the keys held so far keep their notes, but the solo stack and a pending group start again.
        stackSize = 0;
        soloNote = -1;
        groupSize = 0;
        groupStart = -1;
    }
    mode = m;
    speed = std::clamp (s, 0.0f, 1.0f);
    bpm = std::clamp (tempo, 20.0, 999.0);
}

void Player::push (StringEvent e) noexcept
{
    if (count < static_cast<int> (queue.size()))
        queue[static_cast<size_t> (count++)] = e;
}

bool Player::next (std::int64_t until, StringEvent& out) noexcept
{
    // A pending group of keys is complete when its 12 ms are over.
    if (groupSize > 0 && groupStart + static_cast<std::int64_t> (kGroupSeconds * rate) < until)
        flushGroup (groupStart + static_cast<std::int64_t> (kGroupSeconds * rate));
    int best = -1;
    for (int i = 0; i < count; ++i)
        if (queue[static_cast<size_t> (i)].time < until && (best < 0 || queue[static_cast<size_t> (i)].time < queue[static_cast<size_t> (best)].time))
            best = i;
    if (best < 0)
        return false;
    out = queue[static_cast<size_t> (best)];
    queue[static_cast<size_t> (best)] = queue[static_cast<size_t> (--count)];
    return true;
}

int Player::powerchord (int key, int* out) noexcept
{
    // FL Slayer's rule: above C5 (MIDI 72) root, fifth, octave; below it root, fourth, octave.
    out[0] = key;
    out[1] = key + (key >= 72 ? 7 : 5);
    out[2] = key + 12;
    return 3;
}

int Player::autochord (const int* keys, int numKeys, int* out) noexcept
{
    // The root is the lowest key; the other keys tell the chord's colour (minor, seventh, major seventh, suspended).
    int root = 127;
    for (int i = 0; i < numKeys; ++i)
        root = std::min (root, keys[i]);
    bool minor = false, seventh = false, major7 = false, sus = false, third = false;
    for (int i = 0; i < numKeys; ++i)
    {
        const int iv = (keys[i] - root) % 12;
        minor = minor || iv == 3;
        third = third || iv == 3 || iv == 4;
        seventh = seventh || iv == 10;
        major7 = major7 || iv == 11;
        sus = sus || iv == 5;
    }
    sus = sus && ! third;
    // An E-shaped barre chord on six strings: root, fifth, octave, third, fifth, octave.
    const int t = sus ? 17 : minor ? 15 : 16;
    const int voicing[6] { 0, 7, seventh ? 10 : major7 ? 11 : 12, t, 19, 24 };
    int n = 0;
    for (const int v : voicing)
        if (root + v <= 127)
            out[n++] = root + v;
    return n;
}

void Player::strum (const int* notes, int n, float velocity, std::int64_t when, int key) noexcept
{
    // Speed: 60 ms between strings at the slowest, 1 ms at the fastest. A downstroke goes from the low string up.
    const double step = (1.0 + 59.0 * (1.0 - speed) * (1.0 - speed)) * 0.001 * rate;
    Sounding* slot = nullptr;
    for (auto& s : sounding)
        if (s.key < 0 || s.key == key)
        {
            slot = &s;
            break;
        }
    if (slot != nullptr)
    {
        slot->key = key;
        slot->n = std::min (n, static_cast<int> (slot->notes.size()));
        slot->lastOn = when + static_cast<std::int64_t> (step * std::max (0, n - 1));
    }
    for (int i = 0; i < n; ++i)
    {
        const int idx = downStroke ? i : n - 1 - i;
        StringEvent e;
        e.type = StringEvent::On;
        e.note = notes[idx];
        e.velocity = std::clamp (velocity * (downStroke ? 1.0f : 0.85f) * (1.0f - 0.03f * static_cast<float> (i)), 0.05f, 1.0f);
        e.time = when + static_cast<std::int64_t> (step * i);
        push (e);
        if (slot != nullptr && idx < static_cast<int> (slot->notes.size()))
            slot->notes[static_cast<size_t> (idx)] = notes[idx];
    }
    if (n > 1)
        downStroke = ! downStroke;
}

void Player::flushGroup (std::int64_t when) noexcept
{
    const int n = groupSize;
    groupSize = 0;
    groupStart = -1;
    if (n == 0)
        return;
    float vel = 0.0f;
    for (int i = 0; i < n; ++i)
        vel = std::max (vel, groupVel[static_cast<size_t> (i)]);
    std::array<int, 16> keys {};
    std::copy (group.begin(), group.begin() + n, keys.begin());
    std::sort (keys.begin(), keys.begin() + n);
    if (mode == PlayMode::Autochords)
    {
        int notes[8];
        const int m = autochord (keys.data(), n, notes);
        strum (notes, m, vel, when, keys[0]);
        // the other keys of the group belong to the chord: their releases do nothing on their own
        return;
    }
    // Strumming: one key is a note; several are a strum.
    if (n == 1)
    {
        strum (keys.data(), 1, vel, when, keys[0]);
        return;
    }
    strum (keys.data(), n, vel, when, keys[0]);
    // Each key of the strum stops its own note when it is released.
    std::int64_t last = when;
    for (auto& s : sounding)
        if (s.key == keys[0])
        {
            s.n = 1;
            last = s.lastOn;
        }
    for (int i = 1; i < n; ++i)
        for (auto& s : sounding)
            if (s.key < 0)
            {
                s.key = keys[static_cast<size_t> (i)];
                s.n = 1;
                s.notes[0] = keys[static_cast<size_t> (i)];
                s.lastOn = last;
                break;
            }
}

int Player::glideTime (int from, int to) const noexcept
{
    // SoloFixed: 1/64 .. 1/4 beat; SoloDynamic: 1/128 .. 1/8 beat per semitone. Faster with more Speed.
    const double beat = 60.0 / bpm * rate;
    const double unit = std::pow (16.0, 1.0 - speed);
    if (mode == PlayMode::SoloDynamic)
        return static_cast<int> (beat / 128.0 * unit * std::abs (to - from));
    return static_cast<int> (beat / 64.0 * unit);
}

void Player::keyOn (int key, float velocity, std::int64_t when) noexcept
{
    switch (mode)
    {
        case PlayMode::None:
        {
            StringEvent e;
            e.note = key;
            e.velocity = velocity;
            e.time = when;
            push (e);
            return;
        }
        case PlayMode::Powerchords:
        {
            int notes[3];
            const int n = powerchord (key, notes);
            strum (notes, n, velocity, when, key);
            downStroke = true; // power chords are downstrokes
            return;
        }
        case PlayMode::Autochords:
        case PlayMode::Strumming:
            if (groupSize > 0 && when - groupStart > static_cast<std::int64_t> (kGroupSeconds * rate))
                flushGroup (when);
            if (groupSize == 0)
                groupStart = when;
            if (groupSize < static_cast<int> (group.size()))
            {
                group[static_cast<size_t> (groupSize)] = key;
                groupVel[static_cast<size_t> (groupSize++)] = velocity;
            }
            return;
        case PlayMode::SoloFixed:
        case PlayMode::SoloDynamic:
        {
            const bool legato = stackSize > 0 && soloNote >= 0;
            if (stackSize < static_cast<int> (stack.size()))
                stack[static_cast<size_t> (stackSize++)] = key;
            StringEvent e;
            e.note = key;
            e.velocity = velocity;
            e.time = when;
            if (legato)
            {
                e.type = StringEvent::Glide;
                e.glideSamples = glideTime (soloNote, key);
            }
            else if (soloNote >= 0)
            {
                StringEvent off;
                off.type = StringEvent::Off;
                off.note = soloNote;
                off.time = when;
                push (off);
            }
            soloNote = key;
            push (e);
            return;
        }
    }
}

void Player::noteOffsFor (int key, std::int64_t when) noexcept
{
    for (auto& s : sounding)
        if (s.key == key)
        {
            for (int i = 0; i < s.n; ++i)
            {
                StringEvent e;
                e.type = StringEvent::Off;
                e.note = s.notes[static_cast<size_t> (i)];
                e.time = std::max (when, s.lastOn + 1);
                push (e);
            }
            s.key = -1;
        }
}

void Player::keyOff (int key, std::int64_t when) noexcept
{
    switch (mode)
    {
        case PlayMode::None:
        {
            StringEvent e;
            e.type = StringEvent::Off;
            e.note = key;
            e.time = when;
            push (e);
            return;
        }
        case PlayMode::Autochords:
        case PlayMode::Strumming:
        case PlayMode::Powerchords:
        {
            // A key released before its group was played: play the group now, so the release has notes to stop.
            for (int i = 0; i < groupSize; ++i)
                if (group[static_cast<size_t> (i)] == key)
                {
                    flushGroup (when);
                    break;
                }
            noteOffsFor (key, when);
            return;
        }
        case PlayMode::SoloFixed:
        case PlayMode::SoloDynamic:
        {
            int at = -1;
            for (int i = 0; i < stackSize; ++i)
                if (stack[static_cast<size_t> (i)] == key)
                    at = i;
            if (at < 0)
                return;
            for (int i = at; i + 1 < stackSize; ++i)
                stack[static_cast<size_t> (i)] = stack[static_cast<size_t> (i + 1)];
            --stackSize;
            if (key != soloNote)
                return;
            StringEvent e;
            e.time = when;
            if (stackSize > 0)
            {
                // Back to the key still held, sliding.
                const int previous = stack[static_cast<size_t> (stackSize - 1)];
                e.type = StringEvent::Glide;
                e.note = previous;
                e.glideSamples = glideTime (soloNote, previous);
                soloNote = previous;
            }
            else
            {
                e.type = StringEvent::Off;
                e.note = key;
                soloNote = -1;
            }
            push (e);
            return;
        }
    }
}

void Player::allOff (std::int64_t when) noexcept
{
    for (auto& s : sounding)
        if (s.key >= 0)
            noteOffsFor (s.key, when);
    if (soloNote >= 0)
    {
        StringEvent e;
        e.type = StringEvent::Off;
        e.note = soloNote;
        e.time = when;
        push (e);
    }
    groupSize = 0;
    groupStart = -1;
    stackSize = 0;
    soloNote = -1;
}
} // namespace sunriff
