#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "sunsymphony/LibraryManager.h"

// SunSymphony's Library page (0.67.0): every downloadable instrument with a tick box, its articulations, size and state; buttons to
// tick the essentials or everything, Download (with a progress bar, Stop, and continuing later), Remove and Open folder.
namespace sunsymphony
{
class LibraryPanel : public juce::Component, private juce::Timer
{
public:
    explicit LibraryPanel (LibraryManager&);
    ~LibraryPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;

private:
    struct Row
    {
        const LibraryManager::Item* item = nullptr; // nullptr: a family heading
        juce::String heading;
    };
    struct Model;

    void timerCallback() override;
    void rebuild();
    void refresh();
    void toggle (int row);
    juce::int64 selectedBytes() const;

    LibraryManager& lib;
    std::vector<Row> rows;
    juce::StringArray ticked;
    std::unique_ptr<Model> model;
    juce::ListBox list;
    juce::TextButton essentials { "Essentials" }, all { "All" }, none { "None" }, download { "Download" }, stop { "Stop" },
        remove { "Remove" }, openFolder { "Open folder" }, close { "Close" };
    juce::Label title, status, credit;
    double progressValue = 0.0;
    juce::ProgressBar bar { progressValue };
    int lastGeneration = -1;
    bool wasBusy = false;
};
} // namespace sunsymphony
