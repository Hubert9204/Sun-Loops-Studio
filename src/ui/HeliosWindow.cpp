#include "ui/HeliosWindow.h"

#include <algorithm>
#include <cmath>

namespace sls
{
namespace
{
const juce::Colour kBack (0xff1b1e24), kText (0xffd0d4dc), kSun (0xffe8943a), kCommand (0xff4fb3a9), kUser (0xff8fb8e8);

const char* const kSearchHint = "Ask how to do something, in English or Italian. Helios searches the manual and the program's commands, here on "
                                "your computer (no AI, no internet).";
const char* const kChatHint = "LLM mode: chat with Helios, a language model (Gemma 4) that runs on this computer and answers from the manual. "
                              "It explains; it cannot click or change your project.";

// The manual's Markdown made readable as plain text: markup removed, the lines of a paragraph joined (the manual is wrapped at
// 130 columns), list items and table rows kept on lines of their own.
juce::String readable (const std::string& markdown)
{
    juce::StringArray out;
    bool joinable = false; // the last line can take the next one
    for (auto line : juce::StringArray::fromLines (juce::String::fromUTF8 (markdown.c_str())))
    {
        line = line.replace ("**", "").replace ("*", "").replace ("`", "");
        const auto trimmed = line.trim();
        if (trimmed.isEmpty())
        {
            out.add ({});
            joinable = false;
            continue;
        }
        if (trimmed.startsWith ("|"))
        {
            joinable = false;
            if (trimmed.containsOnly ("|-: "))
                continue; // the line under a table's header
            auto cells = juce::StringArray::fromTokens (trimmed.trimCharactersAtStart ("|").trimCharactersAtEnd ("|"), "|", "");
            cells.trim();
            out.add ("  " + cells.joinIntoString ("  -  "));
            continue;
        }
        const auto number = trimmed.initialSectionContainingOnly ("0123456789");
        const bool item = trimmed.startsWith ("- ") || (number.isNotEmpty() && trimmed.substring (number.length()).startsWith (". "));
        if (joinable && ! item)
            out.getReference (out.size() - 1) += " " + trimmed;
        else
            out.add (item ? "  " + trimmed : trimmed);
        joinable = true;
    }
    return out.joinIntoString ("\n").trim();
}
} // namespace

HeliosPanel::HeliosPanel()
{
    title.setText ("Helios", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, kSun);
    addAndMakeVisible (title);
    hint.setText (kSearchHint, juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, kText.withAlpha (0.75f));
    hint.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (hint);

    box.setTextToShowWhenEmpty ("e.g. how do I record two microphones?  /  come aggiungo un riverbero?", juce::Colours::grey);
    box.onTextChange = [this] { if (! llmModeOn()) search(); };
    box.onReturnKey = [this]
    {
        if (llmModeOn())
            sendQuestion();
        else if (! rows.empty())
            runRow (0);
    };
    box.setFont (juce::FontOptions (15.0f));
    addAndMakeVisible (box);

    list.setModel (this);
    list.setRowHeight (40);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff15171c));
    addAndMakeVisible (list);

    reader.setMultiLine (true, true);
    reader.setReadOnly (true);
    reader.setScrollbarsShown (true);
    reader.setCaretVisible (false);
    reader.setFont (juce::FontOptions (13.5f));
    reader.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff15171c));
    reader.setColour (juce::TextEditor::textColourId, kText);
    addAndMakeVisible (reader);

    doButton.onClick = [this] { runRow (list.getSelectedRow()); };
    addAndMakeVisible (doButton);
    doButton.setVisible (false);

    // LLM mode (0.64.0)
    llmMode.setTooltip ("Chat with a language model that runs on this computer (it is downloaded once, 2.8 or 4.6 GB). "
                        "Off: the plain search of the manual and the commands.");
    llmMode.setColour (juce::ToggleButton::textColourId, kSun);
    llmMode.setColour (juce::ToggleButton::tickColourId, kSun);
    llmMode.onClick = [this]
    {
        setLlmMode (llmMode.getToggleState());
        if (onLlmModeChanged)
            onLlmModeChanged (llmMode.getToggleState());
    };
    addAndMakeVisible (llmMode);
    int id = 1;
    for (const auto& m : helios::models())
        modelBox.addItem (juce::String::fromUTF8 (m.label.c_str()), id++);
    modelBox.setTooltip ("Light runs on any computer (also slow ones, slowly); Better answers better but needs about 6 GB of free memory");
    modelBox.onChange = [this]
    {
        const auto index = modelBox.getSelectedItemIndex();
        if (assistant == nullptr || index < 0 || index >= static_cast<int> (helios::models().size()))
            return;
        const auto chosen = juce::String::fromUTF8 (helios::models()[static_cast<size_t> (index)].id.c_str());
        assistant->chooseModel (chosen);
        if (onModelChosen)
            onModelChosen (chosen);
    };
    downloadButton.onClick = [this]
    {
        if (assistant == nullptr)
            return;
        if (assistant->state() == HeliosAssistant::State::downloading)
            assistant->cancelDownload();
        else
            assistant->download();
    };
    deleteButton.setTooltip ("Delete the model from this computer (it can be downloaded again)");
    deleteButton.onClick = [this]
    {
        if (assistant == nullptr)
            return;
        juce::Component::SafePointer<HeliosPanel> safe (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete the model?",
                                            "The model file is deleted from this computer (" + assistant->modelFile().getFullPathName()
                                                + "). LLM mode downloads it again when you want it back.",
                                            "Delete", "Keep", this, juce::ModalCallbackFunction::create ([safe] (int result)
        {
            if (safe != nullptr && safe->assistant != nullptr && result == 1)
                safe->assistant->deleteModel();
        }));
    };
    sendButton.onClick = [this] { sendQuestion(); };
    stopButton.onClick = [this] { if (assistant != nullptr) assistant->stopAnswer(); };
    newChatButton.setTooltip ("Forget this conversation and start again");
    newChatButton.onClick = [this] { if (assistant != nullptr) assistant->newChat(); };
    status.setColour (juce::Label::textColourId, kText.withAlpha (0.85f));
    status.setFont (juce::FontOptions (12.5f));
    status.setMinimumHorizontalScale (0.7f);
    progress.setColour (juce::ProgressBar::foregroundColourId, kSun);
    chat.setMultiLine (true, true);
    chat.setReadOnly (true);
    chat.setScrollbarsShown (true);
    chat.setCaretVisible (false);
    chat.setFont (juce::FontOptions (14.0f));
    chat.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff15171c));
    chat.setColour (juce::TextEditor::textColourId, kText);
    for (auto* c : std::initializer_list<juce::Component*> { &modelBox, &downloadButton, &deleteButton, &sendButton, &stopButton, &newChatButton,
                                                             &status, &progress, &chat })
        addChildComponent (c);
    setLlmMode (false);
    setSize (980, 600);
}

HeliosPanel::~HeliosPanel()
{
    setAssistant (nullptr);
}

void HeliosPanel::setAssistant (HeliosAssistant* a)
{
    if (assistant != nullptr)
        assistant->removeChangeListener (this);
    assistant = a;
    if (assistant != nullptr)
    {
        assistant->addChangeListener (this);
        const auto& all = helios::models();
        for (size_t i = 0; i < all.size(); ++i)
            if (juce::String::fromUTF8 (all[i].id.c_str()) == assistant->modelId())
                modelBox.setSelectedItemIndex (static_cast<int> (i), juce::dontSendNotification);
    }
    llmMode.setVisible (assistant != nullptr);
    shownMessages = 0;
    refreshChat();
}

void HeliosPanel::setLlmMode (bool on)
{
    on = on && assistant != nullptr;
    llmMode.setToggleState (on, juce::dontSendNotification);
    hint.setText (on ? kChatHint : kSearchHint, juce::dontSendNotification);
    box.setTextToShowWhenEmpty (on ? "Ask Helios...  e.g. come registro due microfoni insieme?  /  how do I sidechain a kick?"
                                   : "e.g. how do I record two microphones?  /  come aggiungo un riverbero?",
                                juce::Colours::grey);
    for (auto* c : std::initializer_list<juce::Component*> { &list, &reader })
        c->setVisible (! on);
    if (on)
        doButton.setVisible (false);
    for (auto* c : std::initializer_list<juce::Component*> { &modelBox, &status, &chat, &newChatButton })
        c->setVisible (on);
    if (! on)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &downloadButton, &deleteButton, &sendButton, &stopButton, &progress })
            c->setVisible (false);
        search();
    }
    shownMessages = 0;
    refreshChat();
    resized();
}

void HeliosPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshChat();
}

void HeliosPanel::sendQuestion()
{
    if (assistant == nullptr || ! llmModeOn())
        return;
    const auto question = box.getText().trim();
    if (question.isEmpty())
        return;
    const auto before = assistant->messages().size();
    assistant->ask (question);
    if (assistant->messages().size() != before)
        box.clear(); // taken (not while a previous answer is still being written)
}

void HeliosPanel::refreshChat()
{
    if (assistant == nullptr || ! llmModeOn())
        return;
    using State = HeliosAssistant::State;
    const auto state = assistant->state();
    const bool hasModel = assistant->modelPresent();
    status.setText (assistant->status(), juce::dontSendNotification);
    status.setTooltip (assistant->lastAnswerInfo());
    downloadButton.setButtonText (state == State::downloading ? "Cancel download" : "Download");
    downloadButton.setVisible (state == State::noModel || state == State::downloading || (state == State::failed && ! hasModel));
    deleteButton.setVisible (hasModel && state != State::downloading);
    deleteButton.setEnabled (state != State::answering && state != State::loading);
    progress.setVisible (state == State::downloading);
    progressValue = assistant->downloadProgress();
    const bool canAsk = hasModel && (state == State::ready || state == State::idle || state == State::failed);
    sendButton.setVisible (state != State::answering);
    sendButton.setEnabled (canAsk);
    stopButton.setVisible (state == State::answering);
    modelBox.setEnabled (state != State::downloading && state != State::answering && state != State::loading);
    newChatButton.setEnabled (! assistant->messages().empty());

    // The conversation: only the end of the last answer is added while it is being written; anything else is drawn again.
    const auto& messages = assistant->messages();
    const auto heading = [] (const HeliosAssistant::Message& m) { return juce::String (m.fromUser ? "You\n" : "Helios\n"); };
    const bool growing = shownMessages == messages.size() && ! messages.empty() && ! messages.back().fromUser
                         && messages.back().text.startsWith (shownLastText) && shownLastText.isNotEmpty();
    if (growing)
    {
        chat.moveCaretToEnd();
        chat.setColour (juce::TextEditor::textColourId, kText);
        chat.insertTextAtCaret (messages.back().text.substring (shownLastText.length()));
    }
    else
    {
        chat.clear();
        if (messages.empty())
        {
            chat.setColour (juce::TextEditor::textColourId, kText.withAlpha (0.6f));
            chat.insertTextAtCaret (hasModel || state == State::downloading
                                        ? "Ask Helios how to do something in Sun Loops Studio, in Italian or English. It answers from the manual, "
                                          "here on your computer: nothing is sent anywhere.\n\nThe first answer takes longer (the model is loaded); "
                                          "on an older computer, a few minutes."
                                        : "LLM mode needs a language model on this computer. Choose Light or Better above and press Download "
                                          "(once). Untick LLM mode to use the plain search instead.");
        }
        for (size_t i = 0; i < messages.size(); ++i)
        {
            const auto& m = messages[i];
            chat.setColour (juce::TextEditor::textColourId, m.fromUser ? kUser : kSun);
            chat.insertTextAtCaret ((i > 0 ? "\n\n" : "") + heading (m));
            chat.setColour (juce::TextEditor::textColourId, kText);
            chat.insertTextAtCaret (m.text.isEmpty() && ! m.done ? juce::String ("...") : m.text);
        }
    }
    shownMessages = messages.size();
    shownLastText = messages.empty() || messages.back().fromUser || (messages.back().text.isEmpty() && ! messages.back().done) ? juce::String()
                                                                                                                                : messages.back().text;
    // While the answer is being written "..." stands for it until its first word comes.
    if (! messages.empty() && ! messages.back().fromUser && messages.back().text.isEmpty())
        shownMessages = 0;
    chat.moveCaretToEnd();
    resized();
}

void HeliosPanel::ask (const juce::String& question)
{
    box.setText (question, juce::sendNotification);
    box.grabKeyboardFocus();
}

void HeliosPanel::search()
{
    rows.clear();
    const auto query = box.getText().trim();
    if (query.isNotEmpty())
    {
        // Commands whose name or keywords hold every word of the question that the index knows as a word (or any word, failing that).
        const auto words = HelpIndex::words (query.toStdString());
        std::vector<std::pair<double, int>> found;
        for (size_t c = 0; c < commands.size(); ++c)
        {
            const auto text = HelpIndex::words ((commands[c].name + " " + commands[c].keywords).toStdString());
            int matched = 0;
            for (const auto& w : words)
                matched += std::find (text.begin(), text.end(), w) != text.end() ? 1 : 0;
            if (matched > 0)
                found.emplace_back (matched / static_cast<double> (words.size()), static_cast<int> (c));
        }
        std::stable_sort (found.begin(), found.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t i = 0; i < found.size() && i < 3; ++i)
            if (found[i].first >= 0.5)
                rows.push_back ({ found[i].second, -1, commands[static_cast<size_t> (found[i].second)].name, "A command: double-click or Enter to do it" });

        if (help != nullptr)
            for (const auto& h : help->search (query.toStdString(), 12))
            {
                const auto& s = help->sections()[static_cast<size_t> (h.section)];
                rows.push_back ({ -1, h.section, juce::String::fromUTF8 (s.title.c_str()) + (s.language == "it" ? "  (italiano)" : ""),
                                  juce::String::fromUTF8 (h.snippet.c_str()) });
            }
    }
    list.updateContent();
    list.repaint();
    if (! rows.empty())
    {
        list.selectRow (0);
        show (0);
    }
    else
    {
        doButton.setVisible (false);
        reader.setText (query.isEmpty() ? juce::String ("Type a question above. Examples:\n\n  record several tracks at once\n  slice a drum loop\n"
                                                        "  automate a plugin\n  esportare in mp3\n  correggere l'intonazione della voce")
                                        : "Nothing found for \"" + query + "\". Try other words, or fewer.",
                        juce::dontSendNotification);
    }
}

void HeliosPanel::show (int row)
{
    if (row < 0 || row >= static_cast<int> (rows.size()))
        return;
    const auto& r = rows[static_cast<size_t> (row)];
    doButton.setVisible (r.command >= 0);
    if (r.command >= 0)
        reader.setText (r.title + "\n\nPress \"Do it\" (or Enter, or double-click it in the list) and Helios does it for you.", juce::dontSendNotification);
    else if (help != nullptr)
    {
        const auto& s = help->sections()[static_cast<size_t> (r.section)];
        reader.setText (juce::String::fromUTF8 (s.title.c_str()) + "\n\n" + readable (s.markdown), juce::dontSendNotification);
        reader.moveCaretToTop (false);
    }
}

void HeliosPanel::runRow (int row)
{
    if (row < 0 || row >= static_cast<int> (rows.size()))
        return;
    const auto& r = rows[static_cast<size_t> (row)];
    if (r.command >= 0 && commands[static_cast<size_t> (r.command)].run)
        commands[static_cast<size_t> (r.command)].run();
    else
        show (row);
}

void HeliosPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= static_cast<int> (rows.size()))
        return;
    const auto& r = rows[static_cast<size_t> (row)];
    if (selected)
        g.fillAll (kSun.withAlpha (0.25f));
    g.setColour (r.command >= 0 ? kCommand : juce::Colours::white);
    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    g.drawText ((r.command >= 0 ? juce::String ("Do: ") : juce::String()) + r.title, 8, 2, width - 12, 18, juce::Justification::centredLeft, true);
    g.setColour (kText.withAlpha (0.7f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (r.snippet, 8, 20, width - 12, height - 22, juce::Justification::centredLeft, true);
}

void HeliosPanel::selectedRowsChanged (int row)
{
    show (row);
}

void HeliosPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    runRow (row);
}

void HeliosPanel::returnKeyPressed (int row)
{
    runRow (row);
}

void HeliosPanel::paint (juce::Graphics& g)
{
    g.fillAll (kBack);
}

void HeliosPanel::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto top = area.removeFromTop (28);
    title.setBounds (top.removeFromLeft (90));
    if (llmMode.isVisible())
        llmMode.setBounds (top.removeFromRight (100));
    hint.setBounds (top);
    area.removeFromTop (6);
    if (llmModeOn())
    {
        layoutChat (area);
        return;
    }
    box.setBounds (area.removeFromTop (30));
    area.removeFromTop (8);
    auto left = area.removeFromLeft (area.getWidth() * 2 / 5);
    list.setBounds (left);
    area.removeFromLeft (8);
    auto buttons = area.removeFromBottom (28);
    doButton.setBounds (buttons.removeFromRight (100));
    area.removeFromBottom (6);
    reader.setBounds (area);
}

void HeliosPanel::layoutChat (juce::Rectangle<int> area)
{
    auto bar = area.removeFromTop (28);
    modelBox.setBounds (bar.removeFromLeft (340));
    bar.removeFromLeft (8);
    if (downloadButton.isVisible())
    {
        downloadButton.setBounds (bar.removeFromLeft (120));
        bar.removeFromLeft (8);
    }
    if (deleteButton.isVisible())
    {
        deleteButton.setBounds (bar.removeFromRight (100));
        bar.removeFromRight (8);
    }
    if (progress.isVisible())
    {
        progress.setBounds (bar.removeFromLeft (160).reduced (0, 3));
        bar.removeFromLeft (8);
    }
    newChatButton.setBounds (bar.removeFromRight (84));
    area.removeFromTop (4);
    status.setBounds (area.removeFromTop (22));
    area.removeFromTop (4);
    auto input = area.removeFromBottom (32);
    auto buttons = input.removeFromRight (84);
    sendButton.setBounds (buttons);
    stopButton.setBounds (buttons);
    input.removeFromRight (8);
    box.setBounds (input);
    area.removeFromBottom (8);
    chat.setBounds (area);
}

HeliosWindow::HeliosWindow()
    : juce::DocumentWindow ("Helios", juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (false);
    setContentNonOwned (&content, true);
    setResizable (true, false);
    setResizeLimits (600, 360, 2400, 1600);
    centreWithSize (content.getWidth(), content.getHeight());
}
} // namespace sls
