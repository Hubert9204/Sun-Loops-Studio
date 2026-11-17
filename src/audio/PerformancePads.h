#pragma once

namespace sls
{
// Performance mode's MIDI pads (0.70.0, as FL Studio's performance mode played from a pad controller): the notes from the first
// pad note (C1 = 36 unless chosen otherwise, 0.71.0) up are the cells of the Performance window, `perRow` notes per row from the
// top row down (8 unless chosen otherwise, 0.71.0). The pads of a row but the last launch its first clips; the last stops the
// row. So a pad controller that sends notes from the first pad note plays the window without any set-up.
struct PadAction
{
    enum class Kind { None, Launch, StopRow };
    Kind kind = Kind::None;
    int row = 0, column = 0;
};

constexpr int kFirstPadNote = 36, kPadsPerRow = 8, kPadRows = 8;

inline PadAction performancePad (int note, int firstNote = kFirstPadNote, int perRow = kPadsPerRow) noexcept
{
    perRow = perRow < 2 ? 2 : (perRow > 16 ? 16 : perRow);
    const int pad = note - firstNote;
    if (pad < 0 || pad >= perRow * kPadRows || note > 127)
        return {};
    const int row = pad / perRow, column = pad % perRow;
    return { column == perRow - 1 ? PadAction::Kind::StopRow : PadAction::Kind::Launch, row, column };
}

// The cells of the Performance window name their clip: a pattern clip by its index in the arrangement, an audio clip (0.71.0)
// by kAudioCell + its index.
constexpr int kAudioCell = 1 << 20;
} // namespace sls
