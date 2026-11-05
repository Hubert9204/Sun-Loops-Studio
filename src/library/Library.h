#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "library/BrowserModel.h"
#include "plugins/PluginScanner.h"

namespace sls
{
// Where the browser looks for the user's own sounds: ~/Music/Sun Loops Studio/{Samples, Drum Kits, Presets, Projects}.
struct LibraryPaths
{
    juce::File root, samples, drumKits, presets, projects;

    static LibraryPaths forRoot (const juce::File& root);
    static LibraryPaths defaults(); // ~/Music/Sun Loops Studio

    // Creates the four folders if they are missing. Returns false if that was not possible.
    bool ensureExists() const;
};

enum class PluginScanState { NotScanned, Scanning, Done };

bool isAudioFileName (const juce::String& fileName); // wav, flac, ogg, aif, aiff

// Scans a directory tree into folder/leaf nodes. Hidden entries (starting with '.') are skipped, folders with
// nothing usable inside are left out, and at most `nodeBudget` nodes are created in total (a note says so when
// the budget runs out).
BrowserNode scanDirectory (const juce::File& directory, NodeKind leafKind, const juce::String& wildcardOrExtension,
                           int maxDepth, int& nodeBudget);

// The whole browser tree: Samples, Drum Kits, Instruments, Presets, Plugin.
BrowserNode buildBrowserTree (const LibraryPaths& paths, const std::vector<PluginInfo>& plugins, PluginScanState scanState);

constexpr int kMaxBrowserNodes = 5000;
constexpr int kMaxBrowserDepth = 6;
} // namespace sls
