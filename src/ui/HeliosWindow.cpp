#include "ui/HeliosWindow.h"

#include <algorithm>
#include <cmath>

namespace sls
{
namespace
{
const juce::Colour kBack (0xff1b1e24), kText (0xffd0d4dc), kSun (0xffe8943a), kCommand (0xff4fb3a9);

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
    hint.setText ("Ask how to do something, in English or Italian. Helios searches the manual and the program's commands, here on your "
                  "computer (no AI, no internet).",
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, kText.withAlpha (0.75f));
    hint.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (hint);

    box.setTextToShowWhenEmpty ("e.g. how do I record two microphones?  /  come aggiungo un riverbero?", juce::Colours::grey);
    box.onTextChange = [this] { search(); };
    box.onReturnKey = [this] { if (! rows.empty()) runRow (0); };
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
    setSize (980, 600);
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
    hint.setBounds (top);
    area.removeFromTop (6);
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
