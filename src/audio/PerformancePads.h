#pragma once

namespace sls
{
// Performance mode's MIDI pads (0.70.0, as FL Studio's performance mode played from a pad controller): the notes from C1 (36) up
// are the cells of the Performance window, eight notes per row from the top row down. The first seven pads of a row launch its
// first seven clips; the eighth stops the row. So a 4 x 4 or 8 x 8 pad controller that sends notes from C1 plays the window
// without any set-up.
struct PadAction
{
    enum class Kind { None, Launch, StopRow };
    Kind kind = Kind::None;
    int row = 0, column = 0;
};

constexpr int kFirstPadNote = 36, kPadsPerRow = 8, kPadRows = 8;

inline PadAction performancePad (int note) noexcept
{
    const int pad = note - kFirstPadNote;
    if (pad < 0 || pad >= kPadsPerRow * kPadRows)
        return {};
    const int row = pad / kPadsPerRow, column = pad % kPadsPerRow;
    return { column == kPadsPerRow - 1 ? PadAction::Kind::StopRow : PadAction::Kind::Launch, row, column };
}
} // namespace sls
