#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "library/BrowserModel.h"
#include "ui/DragItems.h"

namespace sls
{
// The left sidebar: a search box above a tree (Samples, Drum Kits, Instruments, Presets, Plugin).
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
