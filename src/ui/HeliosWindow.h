#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "library/HelpIndex.h"

namespace sls
{
// Helios (0.40.0): ask how to do something, and read the manual's answer or let Helios do it. Offline: it searches the program's
// own user guide (English and Italian) and a list of the program's commands; no language model, no network.
struct HeliosCommand
{
    juce::String name;      // "Open the Mixer (F9)"
    juce::String keywords;  // extra words it is found by, in both languages
    std::function<void()> run;
};

class HeliosPanel : public juce::Component, private juce::ListBoxModel
{
public:
    HeliosPanel();

    void setIndex (const HelpIndex* index) { help = index; search(); }
    void setCommands (std::vector<HeliosCommand> all) { commands = std::move (all); search(); }
    void ask (const juce::String& question); // fills the box and searches
    void focusBox() { box.grabKeyboardFocus(); }

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    struct Row
    {
        int command = -1; // or
        int section = -1;
        juce::String title, snippet;
    };
    int getNumRows() override { return static_cast<int> (rows.size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void selectedRowsChanged (int row) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    void search();
    void show (int row);
    void runRow (int row);

    const HelpIndex* help = nullptr;
    std::vector<HeliosCommand> commands;
    std::vector<Row> rows;
    juce::Label title, hint;
    juce::TextEditor box, reader;
    juce::ListBox list;
    juce::TextButton doButton { "Do it" };
};

class HeliosWindow : public juce::DocumentWindow
{
public:
    HeliosWindow();
    ~HeliosWindow() override { clearContentComponent(); }
    HeliosPanel& panel() noexcept { return content; }
    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    HeliosPanel content;
};
} // namespace sls
