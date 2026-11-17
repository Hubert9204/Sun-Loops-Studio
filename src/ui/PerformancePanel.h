#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

namespace sls
{
// What the performance window shows: one row per playlist track that holds pattern clips, one cell per clip (in the order they
// stand on the track), and which of them plays or waits for its turn.
struct PerformanceModel
{
    struct Cell
    {
        int clip = -1; // index of the clip in the arrangement (0.71.0: kAudioCell + the index of an audio clip)
        juce::String name;
        juce::Colour colour;
        bool audio = false;
        int mode = 0;      // 0.71.0: 0 loop, 1 one shot, 2 hold
        bool sync = false;
    };
    struct Row
    {
        int track = 0;
        juce::String name;
        std::vector<Cell> cells;
        int playing = -1;      // clip index, -1: nothing
        int queued = -1;       // clip index that starts at the next boundary, -1: none
        bool stopQueued = false;
    };
    std::vector<Row> rows;
    bool on = false;
    int quantiseId = 1; // 1 bar, 2 bars, 4 bars, 1 beat, a 16th
    bool pads = false;  // 0.70.0: MIDI notes from C1 up launch the cells
    int padFirst = 36, padsPerRow = 8; // 0.71.0
};

// Performance mode (0.62.0, after FL Studio's): the Playlist's pattern clips become cells to launch. A click on a cell starts
// that clip at the next boundary (a bar by default) and loops it; another cell of the same track takes over at the following
// boundary; the square at the end of a row stops the track. The numbers along the top launch one column of every track (a scene).
class PerformancePanel : public juce::Component, private juce::Timer
{
public:
    PerformancePanel();

    void setModel (PerformanceModel newModel);

    std::function<void (bool)> onToggle;      // performance mode on / off
    std::function<void (int)> onLaunch;       // a cell: the clip's index
    std::function<void (int)> onStopTrack;    // a row's stop square: the playlist track
    std::function<void()> onStopAll;
    std::function<void (int)> onScene;        // a column (from 0)
    std::function<void (int)> onQuantise;     // the id of the quantise choice
    std::function<void (bool)> onPads;        // 0.70.0: the MIDI pads switch
    std::function<void (int)> onRelease;      // 0.71.0: a cell let go (for the clips that play while held)
    std::function<void (int, int, bool)> onCellMode; // a cell's launch mode (0 loop, 1 one shot, 2 hold) and sync
    std::function<void (int, int)> onPadLayout;      // the first pad's note, the pads per row

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<int> cellRect (int row, int column) const noexcept;
    juce::Rectangle<int> stopRect (int row) const noexcept;
    int columns() const noexcept;

    static constexpr int kHeader = 34, kSceneRow = 22, kRowHeight = 36, kNameWidth = 130, kCellWidth = 112, kStopWidth = 34;

    PerformanceModel model;
    juce::TextButton onButton { "Performance mode" };
    juce::TextButton stopAllButton { "Stop all" };
    juce::TextButton padsButton { "MIDI pads" };
    juce::TextButton padLayoutButton { "Pads..." };
    int heldCell = -1; // the cell under a mouse button that is down
    juce::ComboBox quantiseBox;
    juce::Label quantiseLabel { {}, "Launch on" };
    bool blink = false;
};

class PerformanceWindow : public juce::DocumentWindow
{
public:
    PerformanceWindow();
    ~PerformanceWindow() override { clearContentComponent(); }

    PerformancePanel& panel() noexcept { return content; }
    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    PerformancePanel content;
};
} // namespace sls
