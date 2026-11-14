#pragma once

#include <functional>
#include <array>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "library/BrowserModel.h"
#include "ui/DragItems.h"

namespace sls
{
// The left sidebar: a search box above a tree (Demo Projects, My Projects, Samples, Drum Kits, Instruments, Presets, Plugin).
// Audio files, built-in samples, instruments and presets can be dragged out of it onto channels; a double-click
// sends the item to the selected channel. The MainComponent (a DragAndDropContainer) carries the drag.
class FileBrowser : public juce::Component
{
public:
    FileBrowser();
    ~FileBrowser() override;

    // Replaces the content. The search text and which folders are open are kept.
    void setTree (BrowserNode root);

    std::function<void (const drag::Item&)> onItemActivated; // double-click on a draggable item
    std::function<void (const juce::String&)> onAction;      // click on an action ("scan-plugins")
    std::function<void (const juce::String&)> onProjectOpen; // 0.65.0: double-click on a project: its file's path, or "demo:<id>"
    std::function<void (const drag::Item&)> onItemPreview;   // left click on a sound (an audio file or a built-in sample): listen to it (0.31.0)
    // 0.62.0: the tabs (All, the current project, the plugins, the favourites) and the favourites themselves (leaves the user
    // starred from an item's right-click menu; the owner keeps them between sessions)
    enum Tab { All, Project, Plugins, Favourites };
    void setTab (int newTab);
    int currentTab() const noexcept { return tab; }
    void setFavourites (std::vector<BrowserNode> list);
    const std::vector<BrowserNode>& getFavourites() const noexcept { return favourites; }
    bool isFavourite (const BrowserNode& node) const noexcept;
    void toggleFavourite (const BrowserNode& node);
    std::function<void()> onFavouritesChanged;
    std::function<void (int)> onTabChanged;
    std::function<void()> onRefresh;
    std::function<void()> onOpenFolder;
    std::function<void()> onCollapse;

    void paint (juce::Graphics&) override;
    void resized() override;

    // For the tests of the GUI: how many leaves are currently shown.

private:
    class Item;
    void applyFilter();
    void openAll (juce::TreeViewItem&);

    BrowserNode fullTree;
    int tab = All;
    std::vector<BrowserNode> favourites;
    std::array<juce::TextButton, 4> tabButtons;
    juce::Label titleLabel;
    juce::TextButton collapseButton { juce::String::fromUTF8 ("\xe2\x97\x80") };
    juce::TextButton folderButton { "Folder" };
    juce::TextButton refreshButton { "Refresh" };
    juce::TextEditor search;
    juce::Label statusLabel;
    juce::TreeView tree;
    std::unique_ptr<Item> rootItem;
    std::unique_ptr<juce::XmlElement> opennessBeforeSearch;
    bool searching = false;
    bool firstTree = true;
    int shownLeaves = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileBrowser)
};
} // namespace sls
