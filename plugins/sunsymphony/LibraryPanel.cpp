#include "sunsymphony/LibraryPanel.h"

namespace sunsymphony
{
namespace
{
const juce::Colour kBackground (0xff15171c), kPanel (0xff1e2128), kGold (0xffe8b04a), kText (0xffe6e6e6), kDim (0xff8a8f99),
    kGreen (0xff6cc070);

juce::String megabytes (juce::int64 bytes)
{
    const double mb = static_cast<double> (bytes) / 1.0e6;
    return mb >= 1000.0 ? juce::String (mb / 1000.0, 2) + " GB" : juce::String (juce::roundToInt (mb)) + " MB";
}

void styleButton (juce::TextButton& b, bool primary = false)
{
    b.setColour (juce::TextButton::buttonColourId, primary ? kGold : kPanel.brighter (0.1f));
    b.setColour (juce::TextButton::textColourOffId, primary ? juce::Colours::black : kText);
}
} // namespace

struct LibraryPanel::Model : public juce::ListBoxModel
{
    explicit Model (LibraryPanel& p) : panel (p) {}
    int getNumRows() override { return static_cast<int> (panel.rows.size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        if (row < 0 || row >= getNumRows())
            return;
        const auto& r = panel.rows[static_cast<size_t> (row)];
        if (r.item == nullptr)
        {
            g.fillAll (kBackground);
            g.setColour (kGold);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (r.heading, 8, 0, width - 16, height, juce::Justification::bottomLeft);
            return;
        }
        const auto& item = *r.item;
        const bool installed = panel.lib.isInstalled (item);
        const bool on = panel.ticked.contains (item.id);
        g.fillAll (row % 2 == 0 ? kPanel : kPanel.brighter (0.03f));
        // the tick box
        const juce::Rectangle<float> box (10.0f, (height - 14) * 0.5f, 14.0f, 14.0f);
        g.setColour (installed ? kGreen : on ? kGold : kDim);
        g.drawRoundedRectangle (box, 2.5f, 1.5f);
        if (installed || on)
        {
            juce::Path tick;
            tick.startNewSubPath (box.getX() + 3.0f, box.getCentreY());
            tick.lineTo (box.getX() + 6.0f, box.getBottom() - 3.5f);
            tick.lineTo (box.getRight() - 3.0f, box.getY() + 3.0f);
            g.strokePath (tick, juce::PathStrokeType (2.0f));
        }
        g.setColour (kText);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (item.name + (item.essential ? "  *" : ""), 34, 0, 220, height, juce::Justification::centredLeft, true);
        juce::StringArray arts;
        for (const auto& a : item.articulations)
            arts.add (a.name);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (arts.joinIntoString (", "), 258, 0, width - 258 - 190, height, juce::Justification::centredLeft, true);
        g.drawText (megabytes (item.bytes), width - 186, 0, 70, height, juce::Justification::centredRight);
        const auto p = panel.lib.progress();
        juce::String state = installed ? "Installed" : "-";
        if (! installed && p.busy && p.current == item.name)
            state = "Downloading...";
        else if (! installed && p.busy && on)
            state = "Waiting";
        g.setColour (installed ? kGreen : kDim);
        g.drawText (state, width - 106, 0, 98, height, juce::Justification::centredRight);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { panel.toggle (row); }
    LibraryPanel& panel;
};

LibraryPanel::LibraryPanel (LibraryManager& m) : lib (m)
{
    model = std::make_unique<Model> (*this);
    list.setModel (model.get());
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, kPanel);
    addAndMakeVisible (list);

    title.setText ("Orchestral library", juce::dontSendNotification);
    title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, kGold);
    addAndMakeVisible (title);
    status.setColour (juce::Label::textColourId, kText);
    status.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (status);
    credit.setText ("Tick the instruments and press Download. * = the essential set. Sounds: " + lib.sourcesCredit()
                        + ", free for any use. Folder: " + lib.folder().getFullPathName(),
                    juce::dontSendNotification);
    credit.setColour (juce::Label::textColourId, kDim);
    credit.setFont (juce::FontOptions (12.0f));
    credit.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (credit);

    bar.setColour (juce::ProgressBar::foregroundColourId, kGold);
    bar.setColour (juce::ProgressBar::backgroundColourId, kPanel);
    addAndMakeVisible (bar);

    for (auto* b : { &essentials, &all, &none, &download, &stop, &remove, &openFolder, &close })
    {
        styleButton (*b, b == &download);
        addAndMakeVisible (*b);
    }
    essentials.setTooltip ("Tick a first orchestra: sections of strings, woodwinds, brass, timpani, cymbals and harp");
    remove.setTooltip ("Delete the ticked instruments that are installed (to free disk space)");

    essentials.onClick = [this]
    {
        ticked.clear();
        for (const auto& i : lib.items())
            if (i.essential)
                ticked.add (i.id);
        refresh();
    };
    all.onClick = [this]
    {
        ticked.clear();
        for (const auto& i : lib.items())
            ticked.add (i.id);
        refresh();
    };
    none.onClick = [this]
    {
        ticked.clear();
        refresh();
    };
    download.onClick = [this]
    {
        if (! lib.download (ticked))
            status.setText (lib.progress().busy ? "A download is already running." : "The ticked instruments are already installed.",
                            juce::dontSendNotification);
        refresh();
    };
    stop.onClick = [this] { lib.cancel(); };
    remove.onClick = [this]
    {
        juce::StringArray installed;
        for (const auto& id : ticked)
            if (const auto* i = lib.find (id); i != nullptr && lib.isInstalled (*i))
                installed.add (i->name);
        if (installed.isEmpty())
            return;
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Remove instruments",
                                                                      "Delete these instruments from the disk?\n\n" + installed.joinIntoString (", ")
                                                                          + "\n\nThey can be downloaded again at any time.",
                                                                      "Remove", "Cancel", this);
        juce::AlertWindow::showAsync (options, [this] (int result)
        {
            if (result != 1)
                return;
            for (const auto& id : ticked)
                if (const auto* i = lib.find (id); i != nullptr && lib.isInstalled (*i))
                    lib.remove (id);
            refresh();
        });
    };
    openFolder.onClick = [this]
    {
        lib.folder().createDirectory();
        lib.folder().startAsProcess();
    };
    close.onClick = [this] { if (onClose) onClose(); };

    rebuild();
    // A first visit: the essentials are ticked, ready for one click.
    bool any = false;
    for (const auto& i : lib.items())
        any = any || lib.isInstalled (i);
    if (! any)
        essentials.onClick();
    timerCallback();
    startTimerHz (5);
}

LibraryPanel::~LibraryPanel()
{
    stopTimer();
    list.setModel (nullptr);
}

void LibraryPanel::rebuild()
{
    rows.clear();
    juce::String family;
    for (const auto& i : lib.items())
    {
        if (i.family != family)
        {
            family = i.family;
            rows.push_back ({ nullptr, family });
        }
        rows.push_back ({ &i, {} });
    }
    list.updateContent();
}

void LibraryPanel::toggle (int row)
{
    if (row < 0 || row >= static_cast<int> (rows.size()) || rows[static_cast<size_t> (row)].item == nullptr)
        return;
    const auto& id = rows[static_cast<size_t> (row)].item->id;
    if (ticked.contains (id))
        ticked.removeString (id);
    else
        ticked.add (id);
    refresh();
}

juce::int64 LibraryPanel::selectedBytes() const
{
    juce::int64 bytes = 0;
    for (const auto& id : ticked)
        if (const auto* i = lib.find (id); i != nullptr && ! lib.isInstalled (*i))
            bytes += i->bytes;
    return bytes;
}

void LibraryPanel::refresh()
{
    const auto p = lib.progress();
    const auto bytes = selectedBytes();
    download.setButtonText (bytes > 0 ? "Download (" + megabytes (bytes) + ")" : "Download");
    download.setEnabled (! p.busy && bytes > 0);
    stop.setEnabled (p.busy);
    remove.setEnabled (! p.busy);
    for (auto* b : { &essentials, &all, &none })
        b->setEnabled (! p.busy);
    list.repaint();
}

void LibraryPanel::timerCallback()
{
    const auto p = lib.progress();
    progressValue = p.busy && p.total > 0 ? juce::jlimit (0.0, 1.0, static_cast<double> (p.done) / static_cast<double> (p.total)) : (p.busy ? -1.0 : 0.0);
    bar.setVisible (p.busy);
    if (p.busy)
        status.setText ("Downloading " + p.current + "...  " + megabytes (p.done) + " of " + megabytes (p.total), juce::dontSendNotification);
    else if (p.message.isNotEmpty())
        status.setText (p.message, juce::dontSendNotification);
    else
    {
        int installed = 0;
        juce::int64 disk = 0;
        for (const auto& i : lib.items())
            if (lib.isInstalled (i))
            {
                ++installed;
                disk += i.bytes;
            }
        status.setText (installed == 0 ? juce::String ("No instrument installed yet.")
                                       : juce::String (installed) + " of " + juce::String (static_cast<int> (lib.items().size()))
                                             + " instruments installed (" + megabytes (disk) + ").",
                        juce::dontSendNotification);
    }
    if (p.busy || p.busy != wasBusy || p.generation != lastGeneration)
    {
        wasBusy = p.busy;
        lastGeneration = p.generation;
        refresh();
    }
}

void LibraryPanel::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
}

void LibraryPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    auto top = area.removeFromTop (28);
    close.setBounds (top.removeFromRight (80));
    title.setBounds (top);
    area.removeFromTop (4);
    credit.setBounds (area.removeFromTop (32));
    auto bottom = area.removeFromBottom (28);
    download.setBounds (bottom.removeFromRight (170));
    bottom.removeFromRight (6);
    stop.setBounds (bottom.removeFromRight (64));
    for (auto* b : { &essentials, &all, &none, &remove, &openFolder })
    {
        b->setBounds (bottom.removeFromLeft (b == &openFolder ? 100 : b == &essentials ? 88 : 64));
        bottom.removeFromLeft (6);
    }
    area.removeFromBottom (6);
    auto info = area.removeFromBottom (20);
    bar.setBounds (info.removeFromRight (220));
    status.setBounds (info);
    area.removeFromBottom (4);
    list.setBounds (area);
}
} // namespace sunsymphony
