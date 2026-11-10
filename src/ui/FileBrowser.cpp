#include "ui/FileBrowser.h"

namespace sls
{
namespace
{
const juce::Colour kBg (0xff262a31);
const juce::Colour kAccent (0xffe8943a);
const juce::Colour kText (0xffd5d9e0);
const juce::Colour kDim (0xff8a919d);

juce::String kindName (NodeKind k)
{
    switch (k)
    {
        case NodeKind::AudioFile:     return "file";
        case NodeKind::BuiltinSample: return "builtin";
        case NodeKind::Instrument:    return "instrument";
        case NodeKind::Preset:        return "preset";
        case NodeKind::Plugin:        return "plugin";
        case NodeKind::Folder:
        case NodeKind::Action:
        case NodeKind::Note:          break;
    }
    return {};
}

juce::Colour dotColour (NodeKind k)
{
    switch (k)
    {
        case NodeKind::AudioFile:
        case NodeKind::BuiltinSample: return juce::Colour (0xff4cc38a);
        case NodeKind::Instrument:    return kAccent;
        case NodeKind::Preset:        return juce::Colour (0xff6aa7e8);
        case NodeKind::Plugin:        return juce::Colour (0xffb38cf0);
        case NodeKind::Folder:
        case NodeKind::Action:
        case NodeKind::Note:          break;
    }
    return juce::Colours::transparentBlack;
}
} // namespace

class FileBrowser::Item : public juce::TreeViewItem
{
public:
    Item (const BrowserNode& n, FileBrowser& o, const juce::String& parentPath)
        : node (n), owner (o), path (parentPath + "/" + juce::String (n.name))
    {
    }

    bool mightContainSubItems() override { return ! node.children.empty(); }
    juce::String getUniqueName() const override { return path; }
    int getItemHeight() const override { return 22; }
    bool canBeSelected() const override { return node.kind != NodeKind::Note; }

    void itemOpennessChanged (bool isNowOpen) override
    {
        if (isNowOpen && getNumSubItems() == 0)
            for (const auto& c : node.children)
                addSubItem (new Item (c, owner, path));
        else if (! isNowOpen)
            clearSubItems();
    }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        if (isSelected())
        {
            g.setColour (juce::Colour (0xff3b4150));
            g.fillRect (0, 0, width, height);
        }

        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (4, 0);

        const auto dot = dotColour (node.kind);
        if (! dot.isTransparent())
        {
            g.setColour (dot);
            g.fillEllipse (static_cast<float> (area.getX()), static_cast<float> (height) / 2.0f - 3.0f, 6.0f, 6.0f);
            area.removeFromLeft (12);
        }

        const bool folder = node.kind == NodeKind::Folder;
        const bool dim = node.kind == NodeKind::Note;
        g.setColour (node.kind == NodeKind::Action ? kAccent : (dim ? kDim : kText));
        g.setFont (juce::FontOptions (13.0f, folder ? juce::Font::bold : (node.kind == NodeKind::Note ? juce::Font::italic : juce::Font::plain)));
        g.drawText (juce::String (node.name), area, juce::Justification::centredLeft, true);
    }

    juce::var getDragSourceDescription() override
    {
        if (! node.isDraggable())
            return {};
        return drag::Item { kindName (node.kind), juce::String (node.payload) }.toString();
    }

    // The callbacks below may rebuild the whole tree (this item included), so they must not run while
    // this item is still handling the mouse event: they are posted to the message loop instead.
    void itemClicked (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;
        if ((node.kind == NodeKind::AudioFile && ! juce::String (node.payload).endsWithIgnoreCase (".sf2")) || node.kind == NodeKind::BuiltinSample)
        {
            // One click on a sound plays it (the same click still selects it and can start a drag).
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<FileBrowser> (&owner),
                                              item = drag::Item { kindName (node.kind), juce::String (node.payload) }]
                                             { if (safe != nullptr && safe->onItemPreview) safe->onItemPreview (item); });
            return;
        }
        if (node.kind != NodeKind::Action)
            return;

        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<FileBrowser> (&owner), action = juce::String (node.payload)]
                                         { if (safe != nullptr && safe->onAction) safe->onAction (action); });
    }

    void itemDoubleClicked (const juce::MouseEvent&) override
    {
        if (node.kind == NodeKind::Folder)
        {
            setOpen (! isOpen());
            return;
        }
        if (! node.isDraggable())
            return;

        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<FileBrowser> (&owner),
                                          item = drag::Item { kindName (node.kind), juce::String (node.payload) }]
                                         { if (safe != nullptr && safe->onItemActivated) safe->onItemActivated (item); });
    }

private:
    BrowserNode node;
    FileBrowser& owner;
    juce::String path;
};

FileBrowser::FileBrowser()
{
    titleLabel.setText ("Browser", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    titleLabel.setInterceptsMouseClicks (false, false);

    collapseButton.setTooltip ("Fold the browser away (F8)");
    folderButton.setTooltip ("Open the library folder (Samples, Drum Kits, Presets) in the file manager");
    refreshButton.setTooltip ("Look for new files");
    for (auto* b : { &collapseButton, &folderButton, &refreshButton })
        b->setMouseClickGrabsKeyboardFocus (false);

    collapseButton.onClick = [this] { if (onCollapse) onCollapse(); };
    folderButton.onClick = [this] { if (onOpenFolder) onOpenFolder(); };
    refreshButton.onClick = [this] { if (onRefresh) onRefresh(); };

    search.setTextToShowWhenEmpty ("Search sounds...", kDim);
    search.setTooltip ("Quick search through every folder");
    search.setFont (juce::FontOptions (13.0f));
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1b1d22));
    search.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff4a505c));
    search.setColour (juce::TextEditor::focusedOutlineColourId, kAccent);
    search.setColour (juce::TextEditor::textColourId, kText);
    search.onTextChange = [this] { applyFilter(); };
    auto giveFocusBack = [this]
    {
        if (auto* parent = getParentComponent())
            parent->grabKeyboardFocus();
    };
    search.onReturnKey = giveFocusBack;
    search.onEscapeKey = [this, giveFocusBack]
    {
        search.setText ({}, true); // true: notify, so the tree comes back (clear() would stay silent)
        giveFocusBack();
    };

    statusLabel.setFont (juce::FontOptions (11.0f));
    statusLabel.setColour (juce::Label::textColourId, kDim);
    statusLabel.setInterceptsMouseClicks (false, false);

    tree.setRootItemVisible (false);
    tree.setDefaultOpenness (false);
    tree.setIndentSize (14);
    tree.setMultiSelectEnabled (false);
    tree.setWantsKeyboardFocus (false); // Space and the F-keys must keep working after a click in the tree
    tree.setColour (juce::TreeView::backgroundColourId, juce::Colour (0xff1f2329));
    tree.setColour (juce::TreeView::linesColourId, juce::Colour (0xff3a3f4a));

    for (auto* c : std::initializer_list<juce::Component*> { &titleLabel, &collapseButton, &folderButton, &refreshButton,
                                                              &search, &statusLabel, &tree })
        addAndMakeVisible (c);
}

FileBrowser::~FileBrowser()
{
    tree.setRootItem (nullptr); // before the items are destroyed
}

void FileBrowser::paint (juce::Graphics& g)
{
    g.setColour (kBg);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
}

void FileBrowser::resized()
{
    auto area = getLocalBounds().reduced (6);
    auto head = area.removeFromTop (26);
    collapseButton.setBounds (head.removeFromLeft (26));
    head.removeFromLeft (6);
    refreshButton.setBounds (head.removeFromRight (56));
    head.removeFromRight (4);
    folderButton.setBounds (head.removeFromRight (50));
    titleLabel.setBounds (head);

    area.removeFromTop (6);
    search.setBounds (area.removeFromTop (26));
    area.removeFromTop (2);
    statusLabel.setBounds (area.removeFromTop (16));
    tree.setBounds (area);
}

void FileBrowser::setTree (BrowserNode root)
{
    fullTree = std::move (root);
    applyFilter();
}

void FileBrowser::openAll (juce::TreeViewItem& item)
{
    item.setOpen (true);
    for (int i = 0; i < item.getNumSubItems(); ++i)
        if (auto* sub = item.getSubItem (i))
            openAll (*sub);
}

void FileBrowser::applyFilter()
{
    const auto query = search.getText().trim().toStdString();
    const bool nowSearching = ! query.empty();

    // Remember the folders the user had opened, so clearing the search brings the same view back.
    std::unique_ptr<juce::XmlElement> openness;
    if (rootItem != nullptr)
        openness = tree.getOpennessState (true);
    if (nowSearching && ! searching && openness != nullptr)
        opennessBeforeSearch = std::make_unique<juce::XmlElement> (*openness);

    const auto shown = filterTree (fullTree, query);
    shownLeaves = countLeaves (shown);

    tree.setRootItem (nullptr);
    rootItem = std::make_unique<Item> (shown, *this, juce::String());
    tree.setRootItem (rootItem.get());
    rootItem->setOpen (true);

    if (nowSearching)
    {
        openAll (*rootItem);
        statusLabel.setText (shownLeaves == 0 ? "No results" : juce::String (shownLeaves) + (shownLeaves == 1 ? " result" : " results"),
                             juce::dontSendNotification);
    }
    else
    {
        statusLabel.setText ({}, juce::dontSendNotification);
        if (firstTree)
        {
            for (int i = 0; i < rootItem->getNumSubItems(); ++i)
                if (auto* top = rootItem->getSubItem (i))
                    top->setOpen (true);
        }
        else if (searching && opennessBeforeSearch != nullptr)
            tree.restoreOpennessState (*opennessBeforeSearch, true);
        else if (openness != nullptr)
            tree.restoreOpennessState (*openness, true);
    }

    firstTree = false;
    searching = nowSearching;
}
} // namespace sls
