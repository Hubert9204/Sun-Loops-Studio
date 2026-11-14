#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "assistant/HeliosAssistant.h"
#include "library/HelpIndex.h"

namespace sls
{
// Helios (0.40.0): ask how to do something, and read the manual's answer or let Helios do it. Offline: it searches the program's
// own user guide (English and Italian) and a list of the program's commands; no language model, no network.
// LLM mode (0.64.0, the "LLM mode" tick box): the same window becomes a chat with a language model that runs on this computer
// (HeliosAssistant) and answers from the manual. It only talks: the commands ("Do it") belong to the plain search.
struct HeliosCommand
{
    juce::String name;      // "Open the Mixer (F9)"
    juce::String keywords;  // extra words it is found by, in both languages
    std::function<void()> run;
};

class HeliosPanel : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener
{
public:
    HeliosPanel();
    ~HeliosPanel() override;

    void setIndex (const HelpIndex* index) { help = index; search(); }
    void setCommands (std::vector<HeliosCommand> all) { commands = std::move (all); search(); }
    void ask (const juce::String& question); // fills the box and searches
    void focusBox() { box.grabKeyboardFocus(); }

    // LLM mode (0.64.0). The assistant belongs to the main window (it outlives this one); nullptr: no LLM mode.
    void setAssistant (HeliosAssistant* a);
    void setLlmMode (bool on);
    bool llmModeOn() const { return llmMode.getToggleState(); }
    std::function<void (bool)> onLlmModeChanged;            // to remember the choice
    std::function<void (const juce::String&)> onModelChosen; // "light" / "quality"

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
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshChat();
    void sendQuestion();
    void layoutChat (juce::Rectangle<int> area);

    const HelpIndex* help = nullptr;
    std::vector<HeliosCommand> commands;
    std::vector<Row> rows;
    juce::Label title, hint;
    juce::TextEditor box, reader;
    juce::ListBox list;
    juce::TextButton doButton { "Do it" };

    // LLM mode
    HeliosAssistant* assistant = nullptr;
    juce::ToggleButton llmMode { "LLM mode" };
    juce::ComboBox modelBox;
    juce::TextButton downloadButton { "Download" }, deleteButton { "Delete model" }, sendButton { "Send" }, stopButton { "Stop" },
        newChatButton { "New chat" };
    juce::Label status;
    double progressValue = 0.0;
    juce::ProgressBar progress { progressValue };
    juce::TextEditor chat;
    size_t shownMessages = 0;
    juce::String shownLastText;
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
